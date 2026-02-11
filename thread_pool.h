#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>

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

// Fork-join style fixed thread pool with per-thread deques + work stealing.
//
// Behavior:
// - If a worker thread submits tasks, they go to that worker's local deque
//   (push_front / pop_front => LIFO) to maximize cache locality in fork-join patterns.
// - If an external thread submits tasks, they are distributed round-robin across workers.
// - When a worker runs out of local work, it steals from the *back* of another worker's deque
//   (oldest tasks) to reduce contention with the victim's LIFO behavior.
// - Destructor performs a graceful shutdown: drain queued tasks, then stop.
class WorkStealingThreadPool {
public:
    explicit WorkStealingThreadPool(size_t num_threads);

    WorkStealingThreadPool(const WorkStealingThreadPool&) = delete;
    WorkStealingThreadPool& operator=(const WorkStealingThreadPool&) = delete;

    // Submit a task. If called from inside a worker, it's treated as a "spawn" and
    // goes to the caller's local deque.
    void submit(std::function<void()> task);

    ~WorkStealingThreadPool();

private:
    struct WorkerQueue {
        std::deque<std::function<void()>> dq;
        std::mutex m;
    };

    void worker_loop(size_t worker_id);

    bool pop_local(size_t worker_id, std::function<void()>& out);
    bool steal_from_others(size_t thief_id, std::function<void()>& out);

    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<WorkerQueue>> queues;

    // Used only for sleeping/wakeup when there's no work.
    std::mutex cv_mutex;
    std::condition_variable cv;

    std::atomic<bool> stop{false};

    // Count of *queued* tasks (not including tasks currently executing).
    std::atomic<size_t> queued_tasks{0};

    // Round-robin index for external submissions.
    std::atomic<size_t> rr{0};
};
