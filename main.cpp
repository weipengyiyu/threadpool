#include <iostream>
#include "threadpool_deep.h"

// 任务函数
void task(int id) {
    std::cout << "Task " << id << " started" << std::endl;
    // 模拟任务执行
    for (int i = 0; i < 100000000; ++i) {
        // do something
    }
    std::cout << "Task " << id << " finished" << std::endl;
}

int main() {
    // 创建线程池对象，最小线程数为2，最大线程数为4
    ThreadPool pool(2, 4);

    // 添加任务到线程池
    for (int i = 0; i < 10; ++i) {
        pool.enqueue(task, i);
    }

    // 等待所有任务执行完成
    pool.waitAll();

    // 停止线程池
    // pool.stop();

    return 0;
}
