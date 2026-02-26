/*
To compile and run this benchmark:

g++ -O3 -std=c++20 -pthread matrix_mul_bench.cpp \
  thread_pool.cpp \
  -o matrix_mul_bench

./matrix_mul_bench classic 1024 64 8 1 3
./matrix_mul_bench ws      1024 64 8 1 3
./matrix_mul_bench elastic 1024 64 8 1 3

1st arg: number of the matrix dimension (N)
2nd arg: block size (BS)
3rd arg: number of threads
4th arg: number of warmup runs (not timed)
5th arg: number of timed runs (best and average reported)
*/


#include "thread_pool.h"
#include "coro_runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

static inline double seconds_since(const Clock::time_point& t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

static inline size_t ridx(size_t N, size_t r, size_t c) { return r * N + c; }

static void fill_random(std::vector<double>& M, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (auto& x : M) x = dist(rng);
}

// Compute one C tile: C[i0..i0+BS, j0..j0+BS] += A*B using k-blocking
static void matmul_tile(size_t N, size_t BS,
                        const std::vector<double>& A,
                        const std::vector<double>& B,
                        std::vector<double>& C,
                        size_t i0, size_t j0) {
    const size_t i_max = std::min(i0 + BS, N);
    const size_t j_max = std::min(j0 + BS, N);

    for (size_t k0 = 0; k0 < N; k0 += BS) {
        const size_t k_max = std::min(k0 + BS, N);

        for (size_t i = i0; i < i_max; ++i) {
            for (size_t k = k0; k < k_max; ++k) {
                const double aik = A[ridx(N, i, k)];
                const size_t b_row = ridx(N, k, 0);
                for (size_t j = j0; j < j_max; ++j) {
                    C[ridx(N, i, j)] += aik * B[b_row + j];
                }
            }
        }
    }
}

template <typename Pool>
static double matmul_parallel(Pool& pool,
                              size_t N, size_t BS,
                              const std::vector<double>& A,
                              const std::vector<double>& B,
                              std::vector<double>& C) {
    std::fill(C.begin(), C.end(), 0.0);

    const size_t tiles_i = (N + BS - 1) / BS;
    const size_t tiles_j = (N + BS - 1) / BS;
    const size_t total_tiles = tiles_i * tiles_j;

    std::atomic<size_t> done{0};
    std::mutex m;
    std::condition_variable cv;

    auto t0 = Clock::now();

    for (size_t ti = 0; ti < tiles_i; ++ti) {
        for (size_t tj = 0; tj < tiles_j; ++tj) {
            const size_t i0 = ti * BS;
            const size_t j0 = tj * BS;

            pool.submit([&, i0, j0] {
                matmul_tile(N, BS, A, B, C, i0, j0);

                const size_t finished = done.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (finished == total_tiles) {
                    std::lock_guard<std::mutex> lk(m);
                    cv.notify_one();
                }
            });
        }
    }

    // Join: wait until all tiles finish
    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return done.load(std::memory_order_acquire) == total_tiles; });
    }

    return seconds_since(t0);
}

static coro::DetachedTask matmul_tile_coro(size_t N,
                                           size_t BS,
                                           const std::vector<double>& A,
                                           const std::vector<double>& B,
                                           std::vector<double>& C,
                                           size_t i0,
                                           size_t j0,
                                           coro::PoolScheduler sched,
                                           std::atomic<size_t>& done,
                                           const size_t total_tiles,
                                           std::mutex& m,
                                           std::condition_variable& cv,
                                           std::exception_ptr& ep,
                                           std::mutex& ep_m) {
    co_await sched.schedule();
    try {
        matmul_tile(N, BS, A, B, C, i0, j0);
    } catch (...) {
        std::lock_guard<std::mutex> lk(ep_m);
        if (!ep) {
            ep = std::current_exception();
        }
    }

    const size_t finished = done.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (finished == total_tiles) {
        std::lock_guard<std::mutex> lk(m);
        cv.notify_one();
    }
}

static double matmul_coroutine_parallel(ThreadPool& pool,
                                        size_t N,
                                        size_t BS,
                                        const std::vector<double>& A,
                                        const std::vector<double>& B,
                                        std::vector<double>& C) {
    std::fill(C.begin(), C.end(), 0.0);

    const size_t tiles_i = (N + BS - 1) / BS;
    const size_t tiles_j = (N + BS - 1) / BS;
    const size_t total_tiles = tiles_i * tiles_j;

    std::atomic<size_t> done{0};
    std::mutex m;
    std::condition_variable cv;
    std::exception_ptr ep;
    std::mutex ep_m;
    coro::PoolScheduler sched(pool);

    const auto t0 = Clock::now();

    for (size_t ti = 0; ti < tiles_i; ++ti) {
        for (size_t tj = 0; tj < tiles_j; ++tj) {
            const size_t i0 = ti * BS;
            const size_t j0 = tj * BS;
            matmul_tile_coro(N, BS, A, B, C, i0, j0, sched, done, total_tiles, m, cv, ep, ep_m);
        }
    }

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return done.load(std::memory_order_acquire) == total_tiles; });
    }

    if (ep) {
        std::rethrow_exception(ep);
    }

    return seconds_since(t0);
}

static double checksum_sparse(const std::vector<double>& C) {
    // prevent “optimize away” & keep O(N) small
    double s = 0.0;
    const size_t step = std::max<size_t>(1, C.size() / 32);
    for (size_t i = 0; i < C.size(); i += step) s += C[i];
    return s;
}

static void usage(const char* prog) {
    std::cerr
        << "Usage:\n  " << prog
        << " <pool: classic|elastic|ws|advws> <N> <BS> <threads> <warmup> <reps>\n\n"
        << "Examples:\n"
        << "  " << prog << " classic 1024 64 8 1 3\n"
        << "  " << prog << " ws      1024 64 8 1 3\n"
        << "  " << prog << " elastic 1024 64 4 1 3   (elastic uses min=threads, max=2*threads)\n"
        << "  " << prog << " advws   1024 64 4 1 3   (advanced elastic stealing)\n"
        << "  " << prog << " coro    1024 64 8 1 3   (coroutine tiles on fixed pool)\n";
}

int main(int argc, char** argv) {
    if (argc < 7) {
        usage(argv[0]);
        return 1;
    }

    const std::string pool_kind = argv[1];
    const size_t N = std::stoul(argv[2]);
    const size_t BS = std::stoul(argv[3]);
    const size_t threads = std::stoul(argv[4]);
    const int warmup = std::stoi(argv[5]);
    const int reps = std::stoi(argv[6]);

    std::cout << "MatMul benchmark (blocked)\n"
              << "pool=" << pool_kind
              << " N=" << N << " BS=" << BS
              << " threads=" << threads
              << " warmup=" << warmup
              << " reps=" << reps << "\n";

    std::vector<double> A(N * N), B(N * N), C(N * N);
    fill_random(A, 12345);
    fill_random(B, 67890);

    double best = 1e100, sum = 0.0;

    auto run_pool = [&](auto& pool) {
        for (int i = 0; i < warmup; ++i) {
            (void)matmul_parallel(pool, N, BS, A, B, C);
        }
        for (int r = 0; r < reps; ++r) {
            const double t = matmul_parallel(pool, N, BS, A, B, C);
            best = std::min(best, t);
            sum += t;
            std::cout << "Run " << r << ": " << t << " s\n";
        }
        std::cout << "Best: " << best << " s\n";
        std::cout << "Avg : " << (sum / reps) << " s\n";
        std::cout << "Checksum: " << checksum_sparse(C) << "\n";
    };

    if (pool_kind == "classic") {
        ThreadPool pool(threads);
        run_pool(pool);
    } else if (pool_kind == "ws") {
        ThreadPool pool(threads, ThreadPool::PoolKind::WorkStealing);
        run_pool(pool);
    } else if (pool_kind == "elastic") {
        // Simple policy: min=threads, max=2*threads
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
            (void)matmul_coroutine_parallel(pool, N, BS, A, B, C);
        }
        for (int r = 0; r < reps; ++r) {
            const double t = matmul_coroutine_parallel(pool, N, BS, A, B, C);
            best = std::min(best, t);
            sum += t;
            std::cout << "Run " << r << ": " << t << " s\n";
        }
        std::cout << "Best: " << best << " s\n";
        std::cout << "Avg : " << (sum / reps) << " s\n";
        std::cout << "Checksum: " << checksum_sparse(C) << "\n";
    } else {
        std::cerr << "Unknown pool kind: " << pool_kind << "\n";
        usage(argv[0]);
        return 1;
    }

    return 0;
}
