#include "thread_pool.h"
#include "coro_runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

static inline double seconds_since(const Clock::time_point& t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

static uint64_t fib_seq(unsigned n) {
    if (n < 2U) {
        return static_cast<uint64_t>(n);
    }

    uint64_t a = 0;
    uint64_t b = 1;
    for (unsigned i = 2; i <= n; ++i) {
        const uint64_t c = a + b;
        a = b;
        b = c;
    }
    return b;
}

static uint64_t fib_task(unsigned n, unsigned split_threshold) {
    if (n <= split_threshold) {
        return fib_seq(n);
    }
    return fib_task(n - 1, split_threshold) + fib_task(n - 2, split_threshold);
}

template <typename Pool>
static double fib_parallel_batch(Pool& pool,
                                 unsigned n,
                                 unsigned split_threshold,
                                 size_t tasks,
                                 uint64_t& checksum_out) {
    std::atomic<size_t> done{0};
    std::mutex m;
    std::condition_variable cv;

    std::vector<uint64_t> out(tasks, 0);

    auto t0 = Clock::now();

    for (size_t i = 0; i < tasks; ++i) {
        pool.submit([&, i] {
            out[i] = fib_task(n, split_threshold);
            const size_t finished = done.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (finished == tasks) {
                std::lock_guard<std::mutex> lk(m);
                cv.notify_one();
            }
        });
    }

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return done.load(std::memory_order_acquire) == tasks; });
    }

    checksum_out = std::accumulate(out.begin(), out.end(), uint64_t{0});
    return seconds_since(t0);
}

static coro::DetachedTask fib_task_coro(unsigned n,
                                        unsigned split_threshold,
                                        size_t idx,
                                        std::vector<uint64_t>& out,
                                        coro::PoolScheduler sched,
                                        std::atomic<size_t>& done,
                                        const size_t tasks,
                                        std::mutex& m,
                                        std::condition_variable& cv,
                                        std::exception_ptr& ep,
                                        std::mutex& ep_m) {
    co_await sched.schedule();
    try {
        out[idx] = fib_task(n, split_threshold);
    } catch (...) {
        std::lock_guard<std::mutex> lk(ep_m);
        if (!ep) {
            ep = std::current_exception();
        }
    }

    const size_t finished = done.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (finished == tasks) {
        std::lock_guard<std::mutex> lk(m);
        cv.notify_one();
    }
}

static double fib_coroutine_batch(ThreadPool& pool,
                                  unsigned n,
                                  unsigned split_threshold,
                                  size_t tasks,
                                  uint64_t& checksum_out) {
    std::atomic<size_t> done{0};
    std::mutex m;
    std::condition_variable cv;
    std::vector<uint64_t> out(tasks, 0);
    std::exception_ptr ep;
    std::mutex ep_m;
    coro::PoolScheduler sched(pool);

    const auto t0 = Clock::now();

    for (size_t i = 0; i < tasks; ++i) {
        fib_task_coro(n, split_threshold, i, out, sched, done, tasks, m, cv, ep, ep_m);
    }

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return done.load(std::memory_order_acquire) == tasks; });
    }

    if (ep) {
        std::rethrow_exception(ep);
    }

    checksum_out = std::accumulate(out.begin(), out.end(), uint64_t{0});
    return seconds_since(t0);
}

static void usage(const char* prog) {
    std::cerr
        << "Usage:\n  " << prog
        << " <pool: classic|elastic|ws|advws|coro> <fib_n> <threads> <warmup> <reps> [tasks] [split_threshold]\n\n"
        << "Examples:\n"
        << "  " << prog << " classic 44 8 1 3\n"
        << "  " << prog << " ws      44 8 1 3\n"
        << "  " << prog << " elastic 44 8 1 3 8 32\n"
        << "  " << prog << " advws   44 8 1 3 8 32\n"
        << "  " << prog << " coro    44 8 1 3 8 32\n\n"
        << "Defaults:\n"
        << "  tasks = threads\n"
        << "  split_threshold = 32 (switch to iterative fib)\n";
}

int main(int argc, char** argv) {
    if (argc < 6) {
        usage(argv[0]);
        return 1;
    }

    try {
        const std::string pool_kind = argv[1];
        const unsigned fib_n = static_cast<unsigned>(std::stoul(argv[2]));
        const size_t threads = std::stoul(argv[3]);
        const int warmup = std::stoi(argv[4]);
        const int reps = std::stoi(argv[5]);
        const size_t tasks = (argc >= 7) ? std::stoul(argv[6]) : threads;
        const unsigned split_threshold = (argc >= 8) ? static_cast<unsigned>(std::stoul(argv[7])) : 32U;
        const uint64_t fib_value = fib_task(fib_n, split_threshold);

        if (threads == 0 || tasks == 0 || reps <= 0 || warmup < 0) {
            std::cerr << "Invalid args: threads/tasks must be > 0, reps > 0, warmup >= 0\n";
            return 1;
        }

        std::cout << "Fibonacci benchmark (batched CPU-bound tasks)\n"
                  << "pool=" << pool_kind
                  << " fib_n=" << fib_n
                  << " fib_value=" << fib_value
                  << " threads=" << threads
                  << " warmup=" << warmup
                  << " reps=" << reps
                  << " tasks=" << tasks
                  << " split_threshold=" << split_threshold
                  << "\n";

        double best = 1e100;
        double sum = 0.0;
        uint64_t last_checksum = 0;

        auto run_pool = [&](auto& pool) {
            for (int i = 0; i < warmup; ++i) {
                uint64_t discard = 0;
                (void)fib_parallel_batch(pool, fib_n, split_threshold, tasks, discard);
            }

            for (int r = 0; r < reps; ++r) {
                uint64_t checksum = 0;
                const double t = fib_parallel_batch(pool, fib_n, split_threshold, tasks, checksum);
                best = std::min(best, t);
                sum += t;
                last_checksum = checksum;
                std::cout << "Run " << r << ": " << t << " s\n";
            }
        };

        if (pool_kind == "classic") {
            ThreadPool pool(threads);
            run_pool(pool);
        } else if (pool_kind == "ws") {
            ThreadPool pool(threads, ThreadPool::PoolKind::WorkStealing);
            run_pool(pool);
        } else if (pool_kind == "elastic") {
            ThreadPool pool(threads, std::max<size_t>(threads * 2, size_t{1}));
            run_pool(pool);
        } else if (pool_kind == "advws") {
            ThreadPool pool(
                threads,
                std::max<size_t>(threads * 2, size_t{1}),
                ThreadPool::PoolKind::AdvancedElasticStealing,
                std::chrono::milliseconds(200));
            run_pool(pool);
        } else if (pool_kind == "coro") {
            ThreadPool pool(threads);
            for (int i = 0; i < warmup; ++i) {
                uint64_t discard = 0;
                (void)fib_coroutine_batch(pool, fib_n, split_threshold, tasks, discard);
            }
            for (int r = 0; r < reps; ++r) {
                uint64_t checksum = 0;
                const double t = fib_coroutine_batch(pool, fib_n, split_threshold, tasks, checksum);
                best = std::min(best, t);
                sum += t;
                last_checksum = checksum;
                std::cout << "Run " << r << ": " << t << " s\n";
            }
        } else {
            std::cerr << "Unknown pool kind: " << pool_kind << "\n";
            usage(argv[0]);
            return 1;
        }

        std::cout << "Best: " << best << " s\n";
        std::cout << "Avg : " << (sum / reps) << " s\n";
        std::cout << "Fib(" << fib_n << "): " << fib_value << "\n";
        std::cout << "Checksum: " << last_checksum << "\n";
        std::cout << "Expected checksum: " << (fib_value * tasks) << "\n";

    } catch (const std::exception& ex) {
        std::cerr << "Argument parse error: " << ex.what() << "\n";
        usage(argv[0]);
        return 1;
    }

    return 0;
}
