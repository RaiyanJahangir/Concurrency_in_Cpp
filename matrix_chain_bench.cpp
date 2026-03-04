/*
To compile and run this benchmark:

g++ -O3 -std=c++20 -pthread matrix_chain_bench.cpp \
  thread_pool.cpp \
  -o matrix_chain_bench

./matrix_chain_bench classic 512 64 4 8 1 3
./matrix_chain_bench ws      512 64 4 8 1 3
./matrix_chain_bench elastic 512 64 4 8 1 3
./matrix_chain_bench advws   512 64 4 8 1 3
./matrix_chain_bench coro    512 64 4 8 1 3

1st arg: pool kind (classic|elastic|ws|advws|coro)
2nd arg: matrix dimension N (square matrices)
3rd arg: block size BS
4th arg: chain length L (number of matrices in the chain)
5th arg: threads
6th arg: warmup runs
7th arg: timed reps
*/

#include "thread_pool.h"
#include "coro_runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

static inline double seconds_since(const Clock::time_point& t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

static inline size_t ridx(size_t N, size_t r, size_t c) { return r * N + c; }

static void fill_random(std::vector<double>& M, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (auto& x : M) {
        x = dist(rng);
    }
}

static void matmul_tile(size_t N,
                        size_t BS,
                        const std::vector<double>& A,
                        const std::vector<double>& B,
                        std::vector<double>& C,
                        size_t i0,
                        size_t j0) {
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

    const auto t0 = Clock::now();

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

template <typename Pool>
static double matrix_chain_parallel(Pool& pool,
                                    size_t N,
                                    size_t BS,
                                    const std::vector<std::vector<double>>& mats,
                                    std::vector<double>& out_final) {
    std::vector<double> cur = mats.front();
    std::vector<double> next(N * N, 0.0);

    double elapsed = 0.0;
    for (size_t i = 1; i < mats.size(); ++i) {
        elapsed += matmul_parallel(pool, N, BS, cur, mats[i], next);
        cur.swap(next);
    }

    out_final.swap(cur);
    return elapsed;
}

static double matrix_chain_coroutine(ThreadPool& pool,
                                     size_t N,
                                     size_t BS,
                                     const std::vector<std::vector<double>>& mats,
                                     std::vector<double>& out_final) {
    std::vector<double> cur = mats.front();
    std::vector<double> next(N * N, 0.0);

    double elapsed = 0.0;
    for (size_t i = 1; i < mats.size(); ++i) {
        elapsed += matmul_coroutine_parallel(pool, N, BS, cur, mats[i], next);
        cur.swap(next);
    }

    out_final.swap(cur);
    return elapsed;
}

static double checksum_sparse(const std::vector<double>& M) {
    double s = 0.0;
    const size_t step = std::max<size_t>(1, M.size() / 64);
    for (size_t i = 0; i < M.size(); i += step) {
        s += M[i];
    }
    return s;
}

static double chain_gflops(size_t N, size_t chain_len, double seconds) {
    if (seconds <= 0.0 || chain_len < 2) {
        return 0.0;
    }
    const double n = static_cast<double>(N);
    const double ops = static_cast<double>(chain_len - 1) * 2.0 * n * n * n;
    return ops / seconds / 1e9;
}

static void usage(const char* prog) {
    std::cerr
        << "Usage:\n  " << prog
        << " <pool: classic|elastic|ws|advws|coro> <N> <BS> <chain_len> <threads> <warmup> <reps>\n\n"
        << "Examples:\n"
        << "  " << prog << " classic 512 64 4 8 1 3\n"
        << "  " << prog << " ws      512 64 4 8 1 3\n"
        << "  " << prog << " elastic 512 64 4 8 1 3\n"
        << "  " << prog << " advws   512 64 4 8 1 3\n"
        << "  " << prog << " coro    512 64 4 8 1 3\n";
}

int main(int argc, char** argv) {
    if (argc < 8) {
        usage(argv[0]);
        return 1;
    }

    try {
        const std::string pool_kind = argv[1];
        const size_t N = std::stoul(argv[2]);
        const size_t BS = std::stoul(argv[3]);
        const size_t chain_len = std::stoul(argv[4]);
        const size_t threads = std::stoul(argv[5]);
        const int warmup = std::stoi(argv[6]);
        const int reps = std::stoi(argv[7]);

        if (N == 0 || BS == 0 || threads == 0 || chain_len < 2 || reps <= 0 || warmup < 0) {
            std::cerr << "Invalid args: N/BS/threads > 0, chain_len >= 2, reps > 0, warmup >= 0\n";
            return 1;
        }

        std::vector<std::vector<double>> mats(chain_len, std::vector<double>(N * N));
        for (size_t i = 0; i < chain_len; ++i) {
            fill_random(mats[i], 1337ULL + static_cast<uint64_t>(i) * 7919ULL);
        }

        std::cout << "Matrix chain benchmark (blocked tiled multiply)\n"
                  << "pool=" << pool_kind
                  << " N=" << N
                  << " BS=" << BS
                  << " chain_len=" << chain_len
                  << " threads=" << threads
                  << " warmup=" << warmup
                  << " reps=" << reps
                  << "\n";

        double best = 1e100;
        double sum = 0.0;
        double last_checksum = 0.0;
        std::vector<double> final_out;

        auto run_pool = [&](auto& pool) {
            for (int i = 0; i < warmup; ++i) {
                std::vector<double> discard;
                (void)matrix_chain_parallel(pool, N, BS, mats, discard);
            }
            for (int r = 0; r < reps; ++r) {
                std::vector<double> out;
                const double t = matrix_chain_parallel(pool, N, BS, mats, out);
                best = std::min(best, t);
                sum += t;
                final_out.swap(out);
                last_checksum = checksum_sparse(final_out);
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
                std::vector<double> discard;
                (void)matrix_chain_coroutine(pool, N, BS, mats, discard);
            }
            for (int r = 0; r < reps; ++r) {
                std::vector<double> out;
                const double t = matrix_chain_coroutine(pool, N, BS, mats, out);
                best = std::min(best, t);
                sum += t;
                final_out.swap(out);
                last_checksum = checksum_sparse(final_out);
                std::cout << "Run " << r << ": " << t << " s\n";
            }
        } else {
            std::cerr << "Unknown pool kind: " << pool_kind << "\n";
            usage(argv[0]);
            return 1;
        }

        std::cout << "Best: " << best << " s\n";
        std::cout << "Avg : " << (sum / reps) << " s\n";
        std::cout << "Checksum: " << last_checksum << "\n";
        std::cout << "Effective GFLOP/s (best): " << chain_gflops(N, chain_len, best) << "\n";

    } catch (const std::exception& ex) {
        std::cerr << "Argument parse error: " << ex.what() << "\n";
        usage(argv[0]);
        return 1;
    }

    return 0;
}
