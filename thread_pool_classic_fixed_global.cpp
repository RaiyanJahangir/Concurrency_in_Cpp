#include "thread_pool.h"

ThreadPool::ThreadPool(size_t num_threads) : stop(false) {
    workers.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back(&ThreadPool::worker_thread, this);
    }
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue.push(std::move(task));
    }
    cv.notify_one();
}

void ThreadPool::worker_thread() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            cv.wait(lock, [&] {
                return stop.load() || !task_queue.empty();
            });

            if (stop.load() && task_queue.empty()) {
                return; // graceful exit
            }

            task = std::move(task_queue.front());
            task_queue.pop();
        }

        // Execute outside the lock
        task();
    }
}

ThreadPool::~ThreadPool() {
    stop.store(true);
    cv.notify_all();

    for (auto& t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }
}
