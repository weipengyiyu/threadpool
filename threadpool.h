#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>
#include <iostream>

class ThreadPool {
public:
    ThreadPool(size_t minThreads, size_t maxThreads);
    ~ThreadPool();

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;

    void setMaxTasks(size_t maxTasks);
    size_t getMaxTasks() const;

    void setMaxThreads(size_t maxThreads);
    size_t getMaxThreads() const;

    size_t getTaskCount() const;
    size_t getThreadCount() const;

    void waitAll();

    void stop();

private:
    void workerThread();

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queueMutex;
    std::condition_variable condition;

    std::atomic<bool> stop_;
    std::atomic<size_t> maxTasks;
    std::atomic<size_t> maxThreads;

    std::atomic<size_t> activeThreads;
    std::atomic<size_t> taskCount;
    std::atomic<size_t> finishedTasks;

    std::condition_variable allTasksFinished;
    std::mutex finishedMutex;
};

inline ThreadPool::ThreadPool(size_t minThreads, size_t maxThreads)
    : stop_(false), maxTasks(100), maxThreads(maxThreads), activeThreads(0), taskCount(0), finishedTasks(0) {

    if (minThreads > maxThreads) {
        throw std::invalid_argument("minThreads cannot be greater than maxThreads");
    }

    size_t numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) {
        numThreads = 1;
    }

    if (minThreads > numThreads) {
        numThreads = minThreads;
    }

    if (maxThreads < numThreads) {
        maxThreads = numThreads;
    }

    workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back(&ThreadPool::workerThread, this);
    }
}

inline ThreadPool::~ThreadPool() {
    stop();
}

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (stop_) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }

        condition.wait(lock, [this] { return tasks.size() < maxTasks.load(); });
        tasks.emplace([task]() { (*task)(); });

        taskCount++;
    }

    condition.notify_one();
    return res;
}

inline void ThreadPool::workerThread() {
    activeThreads++;

    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [this] { return stop_ || !tasks.empty(); });

            if (stop_ && tasks.empty()) {
                activeThreads--;
                return;
            }

            task = std::move(tasks.front());
            tasks.pop();
        }

        task();

        {
            std::unique_lock<std::mutex> lock(finishedMutex);
            finishedTasks++;
            if (finishedTasks == taskCount) {
                allTasksFinished.notify_all();
            }
        }
    }
}

inline void ThreadPool::setMaxTasks(size_t maxTasks) {
    this->maxTasks.store(maxTasks);
}

inline size_t ThreadPool::getMaxTasks() const {
    return maxTasks.load();
}

inline void ThreadPool::setMaxThreads(size_t maxThreads) {
    this->maxThreads.store(maxThreads);
}

inline size_t ThreadPool::getMaxThreads() const {
    return maxThreads.load();
}

inline size_t ThreadPool::getTaskCount() const {
    return taskCount.load();
}

inline size_t ThreadPool::getThreadCount() const {
    return activeThreads.load();
}

inline void ThreadPool::waitAll() {
    std::unique_lock<std::mutex> lock(finishedMutex);
    allTasksFinished.wait(lock, [this] { return finishedTasks == taskCount.load(); });
}

inline void ThreadPool::stop() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop_ = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers) {
      worker.join();
    }
}

#endif
