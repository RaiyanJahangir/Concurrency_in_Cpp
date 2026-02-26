#include "thread_pool.h"

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

    bool await_ready() const noexcept {
        return !coro_ || coro_.done();
    }

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

    bool await_ready() const noexcept {
        return !coro_ || coro_.done();
    }

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

void sync_wait(Task<void> task) {
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

Task<uint64_t> coroutine_sum_squares(uint64_t begin,
                                     uint64_t end,
                                     uint64_t yield_every,
                                     PoolScheduler& sched) {
    uint64_t acc = 0;
    uint64_t i = begin;
    while (i < end) {
        const uint64_t limit = std::min(end, i + yield_every);
        for (; i < limit; ++i) {
            acc += i * i;
        }
        if (i < end) {
            co_await sched.schedule();
        }
    }
    co_return acc;
}

Task<uint64_t> coroutine_fib(unsigned n,
                             unsigned yield_every,
                             PoolScheduler& sched) {
    if (n < 2) {
        co_return n;
    }

    uint64_t a = 0;
    uint64_t b = 1;
    for (unsigned i = 2; i <= n; ++i) {
        const uint64_t c = a + b;
        a = b;
        b = c;
        if (yield_every > 0 && (i % yield_every) == 0) {
            co_await sched.schedule();
        }
    }

    co_return b;
}

Task<uint64_t> coroutine_pipeline_sum(const std::vector<uint64_t>& input,
                                      PoolScheduler& sched) {
    std::vector<uint64_t> stage1;
    stage1.reserve(input.size());

    for (uint64_t x : input) {
        stage1.push_back(x * 3);
    }
    co_await sched.schedule();

    std::vector<uint64_t> stage2;
    stage2.reserve(stage1.size());
    for (uint64_t x : stage1) {
        stage2.push_back(x + 7);
    }
    co_await sched.schedule();

    uint64_t sum = 0;
    for (uint64_t x : stage2) {
        if ((x & 1U) == 0U) {
            sum += x;
        }
    }

    co_return sum;
}

struct DetachedBarrier {
    std::mutex m;
    std::condition_variable cv;
    size_t remaining;

    explicit DetachedBarrier(size_t n) : remaining(n) {}

    void mark_done() {
        std::lock_guard<std::mutex> lock(m);
        if (remaining > 0) {
            --remaining;
            if (remaining == 0) {
                cv.notify_one();
            }
        }
    }

    void wait() {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&] { return remaining == 0; });
    }
};

DetachedTask detached_counter(size_t rounds,
                              PoolScheduler& sched,
                              std::atomic<uint64_t>& shared,
                              DetachedBarrier& barrier) {
    for (size_t i = 0; i < rounds; ++i) {
        shared.fetch_add(1, std::memory_order_relaxed);
        co_await sched.schedule();
    }
    barrier.mark_done();
}

ThreadPool make_pool(const std::string& mode, size_t threads) {
    if (mode == "classic") {
        return ThreadPool(threads, ThreadPool::PoolKind::ClassicFixed);
    }
    if (mode == "ws") {
        return ThreadPool(threads, ThreadPool::PoolKind::WorkStealing);
    }
    if (mode == "elastic") {
        const size_t min_t = std::max<size_t>(1, threads / 2);
        return ThreadPool(min_t, threads, std::chrono::milliseconds(150));
    }
    if (mode == "advws") {
        const size_t min_t = std::max<size_t>(1, threads / 2);
        return ThreadPool(min_t,
                          threads,
                          ThreadPool::PoolKind::AdvancedElasticStealing,
                          std::chrono::milliseconds(150));
    }

    throw std::invalid_argument("mode must be one of: classic | ws | elastic | advws");
}

int main(int argc, char** argv) {
    try {
        const std::string mode = (argc > 1) ? argv[1] : "classic";
        const size_t threads = (argc > 2) ? static_cast<size_t>(std::stoul(argv[2])) : 8;

        if (threads == 0) {
            throw std::invalid_argument("threads must be > 0");
        }

        ThreadPool pool = make_pool(mode, threads);
        PoolScheduler sched(pool);

        std::cout << "Coroutine mode: " << mode << ", threads=" << threads << "\n";

        const uint64_t sum_sq = sync_wait(coroutine_sum_squares(1, 50000, 2048, sched));
        std::cout << "[1] sum_squares(1..49999) = " << sum_sq << "\n";

        const uint64_t fib_50 = sync_wait(coroutine_fib(50, 5, sched));
        std::cout << "[2] fib(50) = " << fib_50 << "\n";

        std::vector<uint64_t> input;
        input.reserve(10000);
        for (uint64_t i = 1; i <= 10000; ++i) {
            input.push_back(i);
        }

        const uint64_t pipeline = sync_wait(coroutine_pipeline_sum(input, sched));
        std::cout << "[3] pipeline_even_sum = " << pipeline << "\n";

        constexpr size_t kWorkers = 24;
        constexpr size_t kRounds = 5000;
        std::atomic<uint64_t> detached_total{0};
        DetachedBarrier barrier(kWorkers);

        for (size_t i = 0; i < kWorkers; ++i) {
            detached_counter(kRounds, sched, detached_total, barrier);
        }
        barrier.wait();

        std::cout << "[4] detached increments = " << detached_total.load(std::memory_order_relaxed)
                  << " (expected " << (kWorkers * kRounds) << ")\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
