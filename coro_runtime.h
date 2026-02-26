#pragma once

#include "thread_pool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace coro {

class PoolScheduler {
public:
    explicit PoolScheduler(ThreadPool& pool) : pool_(pool) {}

    struct ScheduleAwaiter {
        ThreadPool& pool;

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> h) const {
            pool.submit([h]() mutable { h.resume(); });
        }

        void await_resume() const noexcept {}
    };

    ScheduleAwaiter schedule() { return ScheduleAwaiter{pool_}; }

    void post(std::coroutine_handle<> h) const {
        pool_.submit([h]() mutable { h.resume(); });
    }

private:
    ThreadPool& pool_;
};

template <typename T>
class Task {
public:
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;

        Task get_return_object() {
            return Task(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> h) const noexcept {
                auto c = h.promise().continuation;
                return c ? c : std::noop_coroutine();
            }

            void await_resume() const noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }

        template <typename U>
        void return_value(U&& v) {
            value = std::forward<U>(v);
        }

        void unhandled_exception() { exception = std::current_exception(); }
    };

    Task() = default;

    explicit Task(std::coroutine_handle<promise_type> h) : coro_(h) {}

    Task(Task&& other) noexcept : coro_(std::exchange(other.coro_, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro_) {
                coro_.destroy();
            }
            coro_ = std::exchange(other.coro_, {});
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() {
        if (coro_) {
            coro_.destroy();
        }
    }

    bool await_ready() const noexcept { return !coro_ || coro_.done(); }

    void await_suspend(std::coroutine_handle<> awaiting) {
        coro_.promise().continuation = awaiting;
        coro_.resume();
    }

    T await_resume() {
        auto& p = coro_.promise();
        if (p.exception) {
            std::rethrow_exception(p.exception);
        }
        return std::move(*(p.value));
    }

    void start() {
        if (coro_) {
            coro_.resume();
        }
    }

private:
    std::coroutine_handle<promise_type> coro_{};
};

template <>
class Task<void> {
public:
    struct promise_type {
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;

        Task get_return_object() {
            return Task(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> h) const noexcept {
                auto c = h.promise().continuation;
                return c ? c : std::noop_coroutine();
            }

            void await_resume() const noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { exception = std::current_exception(); }
    };

    Task() = default;

    explicit Task(std::coroutine_handle<promise_type> h) : coro_(h) {}

    Task(Task&& other) noexcept : coro_(std::exchange(other.coro_, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro_) {
                coro_.destroy();
            }
            coro_ = std::exchange(other.coro_, {});
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() {
        if (coro_) {
            coro_.destroy();
        }
    }

    bool await_ready() const noexcept { return !coro_ || coro_.done(); }

    void await_suspend(std::coroutine_handle<> awaiting) {
        coro_.promise().continuation = awaiting;
        coro_.resume();
    }

    void await_resume() {
        auto& p = coro_.promise();
        if (p.exception) {
            std::rethrow_exception(p.exception);
        }
    }

    void start() {
        if (coro_) {
            coro_.resume();
        }
    }

private:
    std::coroutine_handle<promise_type> coro_{};
};

struct DetachedTask {
    struct promise_type {
        DetachedTask get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::terminate(); }
    };
};

class DetachedLatch {
public:
    explicit DetachedLatch(size_t initial) : remaining_(initial) {}

    void count_down() {
        const size_t prev = remaining_.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) {
            std::lock_guard<std::mutex> lk(m_);
            cv_.notify_one();
        }
    }

    void wait() {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return remaining_.load(std::memory_order_acquire) == 0; });
    }

private:
    std::atomic<size_t> remaining_;
    std::mutex m_;
    std::condition_variable cv_;
};

struct SleepForAwaiter {
    std::chrono::microseconds us;
    PoolScheduler sched;

    bool await_ready() const noexcept { return us.count() <= 0; }

    void await_suspend(std::coroutine_handle<> h) const {
        std::thread([us = us, sched = sched, h]() mutable {
            std::this_thread::sleep_for(us);
            sched.post(h);
        }).detach();
    }

    void await_resume() const noexcept {}
};

inline SleepForAwaiter sleep_for(std::chrono::microseconds us, PoolScheduler sched) {
    return SleepForAwaiter{us, sched};
}

template <typename T>
T sync_wait(Task<T> task) {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    std::optional<T> out;
    std::exception_ptr ep;

    auto waiter = [&]() -> Task<void> {
        try {
            out = co_await task;
        } catch (...) {
            ep = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> lock(m);
            done = true;
        }
        cv.notify_one();
        co_return;
    };

    Task<void> root = waiter();
    root.start();

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&] { return done; });

    if (ep) {
        std::rethrow_exception(ep);
    }

    return std::move(*out);
}

inline void sync_wait(Task<void> task) {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    std::exception_ptr ep;

    auto waiter = [&]() -> Task<void> {
        try {
            co_await task;
        } catch (...) {
            ep = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> lock(m);
            done = true;
        }
        cv.notify_one();
        co_return;
    };

    Task<void> root = waiter();
    root.start();

    std::unique_lock<std::mutex> lock(m);
    cv.wait(lock, [&] { return done; });

    if (ep) {
        std::rethrow_exception(ep);
    }
}

}  // namespace coro
