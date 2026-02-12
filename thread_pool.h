#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    enum class PoolKind {
        ClassicFixed,
        ElasticGlobal,
        WorkStealing,
        AdvancedElasticStealing
    };

    // Fixed-size pool. Use kind=WorkStealing for fork-join style behavior.
    explicit ThreadPool(size_t num_threads,
                        PoolKind kind = PoolKind::ClassicFixed);

    // Elastic global queue pool: grows/shrinks in [min_threads, max_threads].
    ThreadPool(size_t min_threads,
               size_t max_threads,
               std::chrono::milliseconds idle_timeout = std::chrono::milliseconds(200));

    // Advanced elastic stealing pool: dynamic threads + per-thread queues + stealing.
    ThreadPool(size_t min_threads,
               size_t max_threads,
               PoolKind kind,
               std::chrono::milliseconds idle_timeout);

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void submit(std::function<void()> task);

    ~ThreadPool();

private:
    struct WorkerQueue {
        std::deque<std::function<void()>> dq;
        std::mutex m;
    };

    void worker_global_fixed();
    void worker_global_elastic();
    void worker_ws(size_t worker_id);

    bool pop_local_ws(size_t worker_id, std::function<void()>& out);
    bool steal_from_others_ws(size_t thief_id, std::function<void()>& out);

    void init_ws_storage(size_t max_threads);
    void spawn_ws_worker(size_t worker_id);
    size_t find_inactive_ws_slot() const;

    PoolKind kind_;

    // Shared lifecycle state
    std::atomic<bool> stop_{false};

    // Classic + elastic global queue state
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // Global-queue elastic counters/policy
    size_t min_threads_{0};
    size_t max_threads_{0};
    size_t active_threads_{0};
    size_t idle_threads_{0};
    std::chrono::milliseconds idle_timeout_{200};

    // Work-stealing state (used by fixed WS and advanced elastic WS)
    std::vector<std::thread> ws_threads_;
    std::vector<bool> ws_running_;
    std::vector<std::unique_ptr<WorkerQueue>> ws_queues_;

    std::mutex ws_cv_mutex_;
    std::condition_variable ws_cv_;

    std::atomic<size_t> ws_queued_tasks_{0};
    std::atomic<size_t> ws_rr_{0};

    size_t ws_min_threads_{0};
    size_t ws_max_threads_{0};
    size_t ws_active_threads_{0};
    size_t ws_idle_threads_{0};
    std::chrono::milliseconds ws_idle_timeout_{200};
};
