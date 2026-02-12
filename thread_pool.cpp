#include "thread_pool.h"

#include <stdexcept>

namespace {
thread_local ThreadPool* tls_pool = nullptr;
thread_local long tls_worker_id = -1;
}

ThreadPool::ThreadPool(size_t num_threads, PoolKind kind)
    : kind_(kind),
      min_threads_(num_threads),
      max_threads_(num_threads),
      active_threads_(0),
      idle_threads_(0),
      ws_min_threads_(num_threads),
      ws_max_threads_(num_threads),
      ws_active_threads_(0),
      ws_idle_threads_(0) {
    if (num_threads == 0) {
        throw std::invalid_argument("ThreadPool: num_threads must be > 0");
    }

    if (kind_ == PoolKind::AdvancedElasticStealing || kind_ == PoolKind::ElasticGlobal) {
        throw std::invalid_argument("ThreadPool: invalid kind for fixed-size constructor");
    }

    if (kind_ == PoolKind::WorkStealing) {
        init_ws_storage(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            spawn_ws_worker(i);
        }
        return;
    }

    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        ++active_threads_;
        workers_.emplace_back(&ThreadPool::worker_global_fixed, this);
    }
}

ThreadPool::ThreadPool(size_t min_threads,
                       size_t max_threads,
                       std::chrono::milliseconds idle_timeout)
    : kind_(PoolKind::ElasticGlobal),
      min_threads_(min_threads),
      max_threads_(max_threads),
      active_threads_(0),
      idle_threads_(0),
      idle_timeout_(idle_timeout) {
    if (min_threads == 0 || max_threads == 0 || min_threads > max_threads) {
        throw std::invalid_argument("ThreadPool elastic: invalid thread bounds");
    }

    workers_.reserve(max_threads_);
    for (size_t i = 0; i < min_threads_; ++i) {
        ++active_threads_;
        workers_.emplace_back(&ThreadPool::worker_global_elastic, this);
    }
}

ThreadPool::ThreadPool(size_t min_threads,
                       size_t max_threads,
                       PoolKind kind,
                       std::chrono::milliseconds idle_timeout)
    : kind_(kind),
      ws_min_threads_(min_threads),
      ws_max_threads_(max_threads),
      ws_active_threads_(0),
      ws_idle_threads_(0),
      ws_idle_timeout_(idle_timeout) {
    if (kind_ != PoolKind::AdvancedElasticStealing) {
        throw std::invalid_argument("ThreadPool: this constructor is only for AdvancedElasticStealing");
    }
    if (min_threads == 0 || max_threads == 0 || min_threads > max_threads) {
        throw std::invalid_argument("ThreadPool advanced elastic: invalid thread bounds");
    }

    init_ws_storage(ws_max_threads_);
    for (size_t i = 0; i < ws_min_threads_; ++i) {
        spawn_ws_worker(i);
    }
}

void ThreadPool::init_ws_storage(size_t max_threads) {
    ws_threads_.resize(max_threads);
    ws_running_.assign(max_threads, false);

    ws_queues_.reserve(max_threads);
    for (size_t i = 0; i < max_threads; ++i) {
        ws_queues_.emplace_back(std::make_unique<WorkerQueue>());
    }
}

void ThreadPool::spawn_ws_worker(size_t worker_id) {
    if (worker_id >= ws_threads_.size()) {
        throw std::runtime_error("ThreadPool: worker id out of range");
    }

    // Reuse an old slot if a prior worker on this slot already exited.
    if (ws_threads_[worker_id].joinable()) {
        ws_threads_[worker_id].join();
    }

    ws_running_[worker_id] = true;
    ++ws_active_threads_;
    ws_threads_[worker_id] = std::thread(&ThreadPool::worker_ws, this, worker_id);
}

size_t ThreadPool::find_inactive_ws_slot() const {
    for (size_t i = 0; i < ws_running_.size(); ++i) {
        if (!ws_running_[i]) {
            return i;
        }
    }
    return ws_running_.size();
}

void ThreadPool::submit(std::function<void()> task) {
    if (!task) {
        return;
    }

    if (kind_ == PoolKind::WorkStealing || kind_ == PoolKind::AdvancedElasticStealing) {
        if (stop_.load(std::memory_order_acquire)) {
            throw std::runtime_error("submit on stopped ThreadPool (work-stealing mode)");
        }

        const long wid = (tls_pool == this) ? tls_worker_id : -1;
        if (wid >= 0 && static_cast<size_t>(wid) < ws_queues_.size()) {
            {
                std::lock_guard<std::mutex> lk(ws_queues_[static_cast<size_t>(wid)]->m);
                ws_queues_[static_cast<size_t>(wid)]->dq.emplace_front(std::move(task));
            }
            ws_queued_tasks_.fetch_add(1, std::memory_order_release);
            ws_cv_.notify_one();
            return;
        }

        size_t spawn_id = ws_running_.size();
        {
            std::lock_guard<std::mutex> lock(ws_cv_mutex_);

            const size_t idx = ws_rr_.fetch_add(1, std::memory_order_relaxed) % ws_queues_.size();
            {
                std::lock_guard<std::mutex> lk(ws_queues_[idx]->m);
                ws_queues_[idx]->dq.emplace_back(std::move(task));
            }
            ws_queued_tasks_.fetch_add(1, std::memory_order_release);

            if (kind_ == PoolKind::AdvancedElasticStealing && ws_idle_threads_ == 0 && ws_active_threads_ < ws_max_threads_) {
                spawn_id = find_inactive_ws_slot();
            }
        }

        if (spawn_id < ws_running_.size()) {
            std::lock_guard<std::mutex> lock(ws_cv_mutex_);
            // Re-check under lock to avoid overspawning.
            if (!ws_running_[spawn_id] && ws_active_threads_ < ws_max_threads_) {
                spawn_ws_worker(spawn_id);
            }
        }

        ws_cv_.notify_one();
        return;
    }

    bool spawn_extra_worker = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_.load(std::memory_order_acquire)) {
            throw std::runtime_error("submit on stopped ThreadPool");
        }

        task_queue_.push(std::move(task));

        if (kind_ == PoolKind::ElasticGlobal && idle_threads_ == 0 && active_threads_ < max_threads_) {
            ++active_threads_;
            spawn_extra_worker = true;
        }
    }

    if (spawn_extra_worker) {
        workers_.emplace_back(&ThreadPool::worker_global_elastic, this);
    }

    queue_cv_.notify_one();
}

void ThreadPool::worker_global_fixed() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [&] { return stop_.load(std::memory_order_acquire) || !task_queue_.empty(); });

            if (stop_.load(std::memory_order_acquire) && task_queue_.empty()) {
                --active_threads_;
                return;
            }

            task = std::move(task_queue_.front());
            task_queue_.pop();
        }

        task();
    }
}

void ThreadPool::worker_global_elastic() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            ++idle_threads_;
            const bool woke = queue_cv_.wait_for(lock, idle_timeout_, [&] {
                return stop_.load(std::memory_order_acquire) || !task_queue_.empty();
            });
            --idle_threads_;

            if (stop_.load(std::memory_order_acquire) && task_queue_.empty()) {
                --active_threads_;
                return;
            }

            if (!woke && task_queue_.empty() && active_threads_ > min_threads_) {
                --active_threads_;
                return;
            }

            if (task_queue_.empty()) {
                continue;
            }

            task = std::move(task_queue_.front());
            task_queue_.pop();
        }

        task();
    }
}

bool ThreadPool::pop_local_ws(size_t worker_id, std::function<void()>& out) {
    WorkerQueue& q = *ws_queues_[worker_id];
    std::lock_guard<std::mutex> lk(q.m);
    if (q.dq.empty()) {
        return false;
    }

    out = std::move(q.dq.front());
    q.dq.pop_front();
    ws_queued_tasks_.fetch_sub(1, std::memory_order_acq_rel);
    return true;
}

bool ThreadPool::steal_from_others_ws(size_t thief_id, std::function<void()>& out) {
    const size_t n = ws_queues_.size();
    if (n <= 1) {
        return false;
    }

    for (size_t k = 1; k < n; ++k) {
        const size_t victim = (thief_id + k) % n;
        WorkerQueue& q = *ws_queues_[victim];

        std::unique_lock<std::mutex> lk(q.m, std::try_to_lock);
        if (!lk.owns_lock() || q.dq.empty()) {
            continue;
        }

        out = std::move(q.dq.back());
        q.dq.pop_back();
        ws_queued_tasks_.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    return false;
}

void ThreadPool::worker_ws(size_t worker_id) {
    tls_pool = this;
    tls_worker_id = static_cast<long>(worker_id);

    while (true) {
        if (stop_.load(std::memory_order_acquire) &&
            ws_queued_tasks_.load(std::memory_order_acquire) == 0) {
            std::lock_guard<std::mutex> lock(ws_cv_mutex_);
            if (ws_running_[worker_id]) {
                ws_running_[worker_id] = false;
                --ws_active_threads_;
            }
            return;
        }

        std::function<void()> task;
        if (pop_local_ws(worker_id, task) || steal_from_others_ws(worker_id, task)) {
            try {
                task();
            } catch (...) {
                // Keep worker alive if a task throws.
            }
            continue;
        }

        std::unique_lock<std::mutex> lk(ws_cv_mutex_);
        ++ws_idle_threads_;

        if (kind_ == PoolKind::AdvancedElasticStealing) {
            const bool woke = ws_cv_.wait_for(lk, ws_idle_timeout_, [&] {
                return stop_.load(std::memory_order_acquire) ||
                       ws_queued_tasks_.load(std::memory_order_acquire) > 0;
            });

            --ws_idle_threads_;

            if (stop_.load(std::memory_order_acquire) &&
                ws_queued_tasks_.load(std::memory_order_acquire) == 0) {
                if (ws_running_[worker_id]) {
                    ws_running_[worker_id] = false;
                    --ws_active_threads_;
                }
                return;
            }

            if (!woke && ws_queued_tasks_.load(std::memory_order_acquire) == 0 &&
                ws_active_threads_ > ws_min_threads_) {
                if (ws_running_[worker_id]) {
                    ws_running_[worker_id] = false;
                    --ws_active_threads_;
                }
                return;
            }
            continue;
        }

        ws_cv_.wait(lk, [&] {
            return stop_.load(std::memory_order_acquire) ||
                   ws_queued_tasks_.load(std::memory_order_acquire) > 0;
        });

        --ws_idle_threads_;
    }
}

ThreadPool::~ThreadPool() {
    stop_.store(true, std::memory_order_release);
    queue_cv_.notify_all();
    ws_cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }

    for (auto& t : ws_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}
