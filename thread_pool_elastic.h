#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

class ElasticThreadPool {
public:
    // Create a pool that can grow/shrink between min and max threads.
    ElasticThreadPool(size_t min_threads,
                      size_t max_threads,
                      std::chrono::milliseconds idle_timeout = std::chrono::milliseconds(200));

    // Non-copyable
    ElasticThreadPool(const ElasticThreadPool&) = delete;
    ElasticThreadPool& operator=(const ElasticThreadPool&) = delete;

    // Submit a task to the pool
    void submit(std::function<void()> task);

    // Graceful shutdown: finish queued tasks, then stop
    ~ElasticThreadPool();

private:
    void worker_thread();

    void spawn_worker();

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> task_queue;

    std::mutex queue_mutex;
    std::condition_variable cv;

    std::atomic<bool> stop;

    size_t min_threads;
    size_t max_threads;
    size_t active_threads;
    size_t idle_threads;

    std::chrono::milliseconds idle_timeout;
};
