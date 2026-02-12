/*
Single-Fibonacci parallel benchmark using the existing ThreadPool variants.

Build:
g++ -O3 -std=c++20 -pthread fib_single_bench.cpp thread_pool.cpp -o fib_single_bench

Run:
./fib_single_bench classic 44 8 1 3
./fib_single_bench ws      44 8 1 3
./fib_single_bench elastic 44 8 1 3
./fib_single_bench advws   44 8 1 3

Args:
1st: pool kind (classic|elastic|ws|advws)
2nd: fib_n
3rd: threads
4th: warmup runs
5th: timed runs
6th optional: split_threshold (default 30)
*/

#include "thread_pool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

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

template <typename Pool>
static uint64_t fib_single_parallel(Pool& pool,
                                    unsigned n,
                                    unsigned split_threshold,
                                    uint64_t& spawned_internal_nodes) {
    struct Node {
        explicit Node(unsigned nn, std::shared_ptr<Node> p = nullptr, bool l = false)
            : n(nn), parent(std::move(p)), is_left_child(l) {}

        unsigned n;
        uint64_t left{0};
        uint64_t right{0};
        std::atomic<int> pending{0};
        std::shared_ptr<Node> parent;
        bool is_left_child;
    };

    std::mutex done_mutex;
    std::condition_variable done_cv;
    uint64_t result = 0;
    bool done = false;
    std::atomic<uint64_t> spawned{0};

    std::function<void(std::shared_ptr<Node>, uint64_t)> complete;
    std::function<void(std::shared_ptr<Node>)> run;

    complete = [&](std::shared_ptr<Node> cur, uint64_t value) {
        while (true) {
            const std::shared_ptr<Node> parent = cur->parent;
            if (!parent) {
                {
                    std::lock_guard<std::mutex> lk(done_mutex);
                    result = value;
                    done = true;
                }
                done_cv.notify_one();
                return;
            }

            if (cur->is_left_child) {
                parent->left = value;
            } else {
                parent->right = value;
            }

            const int prev = parent->pending.fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1) {
                value = parent->left + parent->right;
                cur = parent;
                continue;
            }
            return;
        }
    };

    run = [&](std::shared_ptr<Node> node) {
        if (node->n <= split_threshold) {
            complete(node, fib_seq(node->n));
            return;
        }

        node->pending.store(2, std::memory_order_relaxed);
        spawned.fetch_add(1, std::memory_order_relaxed);

        auto left = std::make_shared<Node>(node->n - 1, node, true);
        auto right = std::make_shared<Node>(node->n - 2, node, false);

        pool.submit([&, left] { run(left); });
        pool.submit([&, right] { run(right); });
    };

    auto root = std::make_shared<Node>(n);
    pool.submit([&, root] { run(root); });

    {
        std::unique_lock<std::mutex> lk(done_mutex);
        done_cv.wait(lk, [&] { return done; });
    }

    spawned_internal_nodes = spawned.load(std::memory_order_relaxed);
    return result;
}

template <typename Pool>
static double run_once(Pool& pool,
                       unsigned fib_n,
                       unsigned split_threshold,
                       uint64_t& fib_value,
                       uint64_t& spawned_internal_nodes) {
    const auto t0 = Clock::now();
    fib_value = fib_single_parallel(pool, fib_n, split_threshold, spawned_internal_nodes);
    return seconds_since(t0);
}

static void usage(const char* prog) {
    std::cerr
        << "Usage:\n  " << prog
        << " <pool: classic|elastic|ws|advws> <fib_n> <threads> <warmup> <reps> [split_threshold]\n\n"
        << "Examples:\n"
        << "  " << prog << " classic 44 8 1 3\n"
        << "  " << prog << " ws      44 8 1 3\n"
        << "  " << prog << " elastic 44 8 1 3\n"
        << "  " << prog << " advws   44 8 1 3\n"
        << "  " << prog << " ws      50 8 1 3 34\n";
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
        const unsigned split_threshold = (argc >= 7) ? static_cast<unsigned>(std::stoul(argv[6])) : 30U;

        if (threads == 0 || reps <= 0 || warmup < 0) {
            std::cerr << "Invalid args: threads must be > 0, reps > 0, warmup >= 0\n";
            return 1;
        }
        if (fib_n > 93) {
            std::cerr << "fib_n must be <= 93 for uint64_t exactness\n";
            return 1;
        }
        if (split_threshold < 2U) {
            std::cerr << "split_threshold should be >= 2\n";
            return 1;
        }

        std::cout << "Single Fibonacci benchmark (parallelized one fib tree)\n"
                  << "pool=" << pool_kind
                  << " fib_n=" << fib_n
                  << " threads=" << threads
                  << " warmup=" << warmup
                  << " reps=" << reps
                  << " split_threshold=" << split_threshold << "\n";

        double best = 1e100;
        double sum = 0.0;
        uint64_t last_value = 0;
        uint64_t last_spawned = 0;

        auto run_pool = [&](auto& pool) {
            for (int i = 0; i < warmup; ++i) {
                uint64_t warm_value = 0;
                uint64_t warm_spawned = 0;
                (void)run_once(pool, fib_n, split_threshold, warm_value, warm_spawned);
            }

            for (int r = 0; r < reps; ++r) {
                uint64_t value = 0;
                uint64_t spawned = 0;
                const double t = run_once(pool, fib_n, split_threshold, value, spawned);
                best = std::min(best, t);
                sum += t;
                last_value = value;
                last_spawned = spawned;
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
        std::cout << "Fib(" << fib_n << "): " << last_value << "\n";
        std::cout << "Spawned internal nodes: " << last_spawned << "\n";

    } catch (const std::exception& ex) {
        std::cerr << "Argument parse error: " << ex.what() << "\n";
        usage(argv[0]);
        return 1;
    }

    return 0;
}
