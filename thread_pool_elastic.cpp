#include "thread_pool_elastic.h"

#include <stdexcept>

ElasticThreadPool::ElasticThreadPool(size_t min_threads_,
                                     size_t max_threads_,
                                     std::chrono::milliseconds idle_timeout_)
    : stop(false),
      min_threads(min_threads_),
      max_threads(max_threads_),
      active_threads(0),
      idle_threads(0),
      idle_timeout(idle_timeout_) {
    if (min_threads == 0 || max_threads == 0 || min_threads > max_threads) {
        throw std::invalid_argument("Invalid thread counts");
    }

    workers.reserve(max_threads);
    for (size_t i = 0; i < min_threads; ++i) {
        ++active_threads;
        workers.emplace_back(&ElasticThreadPool::worker_thread, this);
    }
}

void ElasticThreadPool::submit(std::function<void()> task) {
    bool need_spawn = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue.push(std::move(task));

        if (idle_threads == 0 && active_threads < max_threads) {
            ++active_threads;
            need_spawn = true;
        }
    }

    if (need_spawn) {
        workers.emplace_back(&ElasticThreadPool::worker_thread, this);
    }

    cv.notify_one();
}

void ElasticThreadPool::worker_thread() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            ++idle_threads;
            bool woke = cv.wait_for(lock, idle_timeout, [&] {
                return stop.load() || !task_queue.empty();
            });
            --idle_threads;

            if (stop.load() && task_queue.empty()) {
                --active_threads;
                return;
            }

            if (!woke && task_queue.empty()) {
                if (active_threads > min_threads) {
                    --active_threads;
                    return;
                }
            }

            if (!task_queue.empty()) {
                task = std::move(task_queue.front());
                task_queue.pop();
            } else {
                continue;
            }
        }

        task();
    }
}

ElasticThreadPool::~ElasticThreadPool() {
    stop.store(true);
    cv.notify_all();

    for (auto& t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }
}
