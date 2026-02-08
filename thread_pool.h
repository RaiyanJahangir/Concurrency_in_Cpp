#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

class ThreadPool {
public:
    // Create a fixed-size thread pool
    explicit ThreadPool(size_t num_threads);

    // Non-copyable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a task to the pool
    void submit(std::function<void()> task);

    // Graceful shutdown: finish queued tasks, then stop
    ~ThreadPool();

private:
    // Worker loop
    void worker_thread();

    std::vector<std::thread> workers;

    // Global task queue
    std::queue<std::function<void()>> task_queue;

    // Synchronization
    std::mutex queue_mutex;
    std::condition_variable cv;

    // Shutdown flag
    std::atomic<bool> stop;
};
