#include "thread_pool.h"

#include <stdexcept>

// Thread-local worker id used to route fork-join spawns to the local deque.
static thread_local long tls_worker_id = -1;

WorkStealingThreadPool::WorkStealingThreadPool(size_t num_threads)
    : stop(false), queued_tasks(0), rr(0) {
    if (num_threads == 0) {
        throw std::invalid_argument("WorkStealingThreadPool: num_threads must be > 0");
    }

    queues.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        queues.emplace_back(std::make_unique<WorkerQueue>());
    }
    workers.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back(&WorkStealingThreadPool::worker_loop, this, i);
    }
}

void WorkStealingThreadPool::submit(std::function<void()> task) {
    if (!task) return;

    // Prevent new work from being enqueued once shutdown begins.
    if (stop.load(std::memory_order_acquire)) {
        throw std::runtime_error("submit on stopped WorkStealingThreadPool");
    }

    // If called from a worker, treat as a fork-join "spawn".
    const long wid = tls_worker_id;
    if (wid >= 0 && static_cast<size_t>(wid) < queues.size()) {
        {
            std::lock_guard<std::mutex> lk(queues[static_cast<size_t>(wid)]->m);
            queues[static_cast<size_t>(wid)]->dq.emplace_front(std::move(task));

        }
        queued_tasks.fetch_add(1, std::memory_order_release);
        cv.notify_one();
        return;
    }

    // External submission: distribute round-robin.
    const size_t idx = rr.fetch_add(1, std::memory_order_relaxed) % queues.size();
    {
        std::lock_guard<std::mutex> lk(queues[idx]->m);
        queues[idx]->dq.emplace_back(std::move(task));

    }
    queued_tasks.fetch_add(1, std::memory_order_release);
    cv.notify_one();
}

bool WorkStealingThreadPool::pop_local(size_t worker_id, std::function<void()>& out) {
    WorkerQueue& q = *queues[worker_id];
    std::lock_guard<std::mutex> lk(q.m);
    if (q.dq.empty()) return false;

    out = std::move(q.dq.front());
    q.dq.pop_front();
    queued_tasks.fetch_sub(1, std::memory_order_acq_rel);
    return true;
}

bool WorkStealingThreadPool::steal_from_others(size_t thief_id, std::function<void()>& out) {
    const size_t n = queues.size();
    if (n <= 1) return false;

    // Simple deterministic probe order to keep overhead low.
    for (size_t k = 1; k < n; ++k) {
        const size_t victim = (thief_id + k) % n;
        WorkerQueue& q = *queues[victim];

        std::unique_lock<std::mutex> lk(q.m, std::try_to_lock);
        if (!lk.owns_lock() || q.dq.empty()) continue;

        out = std::move(q.dq.back());
        q.dq.pop_back();
        queued_tasks.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }
    return false;
}

void WorkStealingThreadPool::worker_loop(size_t worker_id) {
    tls_worker_id = static_cast<long>(worker_id);

    while (true) {
        // Graceful exit: only leave when stop is set AND no queued tasks remain.
        if (stop.load(std::memory_order_acquire) &&
            queued_tasks.load(std::memory_order_acquire) == 0) {
            return;
        }

        std::function<void()> task;
        if (pop_local(worker_id, task) || steal_from_others(worker_id, task)) {
            // Run outside locks.
            try {
                task();
            } catch (...) {
                // Swallow to keep pool alive; customize if you want error reporting.
            }
            continue;
        }

        // Nothing available: sleep until new work arrives or shutdown begins.
        std::unique_lock<std::mutex> lk(cv_mutex);
        cv.wait(lk, [&] {
            return stop.load(std::memory_order_acquire) ||
                   queued_tasks.load(std::memory_order_acquire) > 0;
        });
    }
}

WorkStealingThreadPool::~WorkStealingThreadPool() {
    stop.store(true, std::memory_order_release);
    cv.notify_all();

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }
}
