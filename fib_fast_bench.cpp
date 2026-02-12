/*
Fast-doubling Fibonacci benchmark using existing ThreadPool variants.

Build:
g++ -O3 -std=c++20 -pthread fib_fast_bench.cpp thread_pool.cpp -o fib_fast_bench

Run:
./fib_fast_bench classic 90 8 1 3
./fib_fast_bench ws      90 8 1 3
./fib_fast_bench elastic 90 8 1 3
./fib_fast_bench advws   90 8 1 3

Args:
1st: pool kind (classic|elastic|ws|advws)
2nd: fib_n (0..93 for uint64_t exactness)
3rd: threads
4th: warmup runs
5th: timed runs
6th optional: tasks per run (default = threads)
*/

#include "thread_pool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;

static inline double seconds_since(const Clock::time_point& t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

static std::pair<uint64_t, uint64_t> fib_fast_pair(unsigned n) {
    if (n == 0U) {
        return {0U, 1U};
    }

    const auto [a, b] = fib_fast_pair(n >> 1U);  // a=F(k), b=F(k+1)

    const __uint128_t aa = static_cast<__uint128_t>(a) * a;
    const __uint128_t bb = static_cast<__uint128_t>(b) * b;
    const __uint128_t two_b_minus_a = static_cast<__uint128_t>(2U) * b - a;
    const __uint128_t c128 = static_cast<__uint128_t>(a) * two_b_minus_a;  // F(2k)
    const __uint128_t d128 = aa + bb;                                       // F(2k+1)

    const uint64_t c = static_cast<uint64_t>(c128);
    const uint64_t d = static_cast<uint64_t>(d128);

    if ((n & 1U) == 0U) {
        return {c, d};
    }
    return {d, c + d};
}

static uint64_t fib_fast(unsigned n) {
    return fib_fast_pair(n).first;
}

template <typename Pool>
static double fib_parallel_batch(Pool& pool,
                                 unsigned n,
                                 size_t tasks,
                                 uint64_t& checksum_out) {
    std::atomic<size_t> done{0};
    std::mutex m;
    std::condition_variable cv;
    std::vector<uint64_t> out(tasks, 0);

    const auto t0 = Clock::now();

    for (size_t i = 0; i < tasks; ++i) {
        pool.submit([&, i] {
            out[i] = fib_fast(n);
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

static void usage(const char* prog) {
    std::cerr
        << "Usage:\n  " << prog
        << " <pool: classic|elastic|ws|advws> <fib_n> <threads> <warmup> <reps> [tasks]\n\n"
        << "Examples:\n"
        << "  " << prog << " classic 90 8 1 3\n"
        << "  " << prog << " ws      90 8 1 3\n"
        << "  " << prog << " elastic 90 8 1 3\n"
        << "  " << prog << " advws   90 8 1 3\n";
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

        if (threads == 0 || tasks == 0 || reps <= 0 || warmup < 0) {
            std::cerr << "Invalid args: threads/tasks must be > 0, reps > 0, warmup >= 0\n";
            return 1;
        }
        if (fib_n > 93) {
            std::cerr << "fib_n must be <= 93 for uint64_t exactness\n";
            return 1;
        }

        const uint64_t fib_value = fib_fast(fib_n);

        std::cout << "Fibonacci benchmark (fast doubling kernel)\n"
                  << "pool=" << pool_kind
                  << " fib_n=" << fib_n
                  << " fib_value=" << fib_value
                  << " threads=" << threads
                  << " warmup=" << warmup
                  << " reps=" << reps
                  << " tasks=" << tasks << "\n";

        double best = 1e100;
        double sum = 0.0;
        uint64_t last_checksum = 0;

        auto run_pool = [&](auto& pool) {
            for (int i = 0; i < warmup; ++i) {
                uint64_t discard = 0;
                (void)fib_parallel_batch(pool, fib_n, tasks, discard);
            }
            for (int r = 0; r < reps; ++r) {
                uint64_t checksum = 0;
                const double t = fib_parallel_batch(pool, fib_n, tasks, checksum);
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
