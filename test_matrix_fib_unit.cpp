#include "thread_pool.h"

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
#include <utility>
#include <vector>

class TestSuite {
public:
    using TestFn = std::function<void()>;

    void add(std::string name, TestFn fn) {
        tests_.push_back({std::move(name), std::move(fn)});
    }

    int run() const {
        int failures = 0;
        for (const auto& t : tests_) {
            try {
                t.second();
                std::cout << "[PASS] " << t.first << "\n";
            } catch (const std::exception& ex) {
                ++failures;
                std::cout << "[FAIL] " << t.first << ": " << ex.what() << "\n";
            } catch (...) {
                ++failures;
                std::cout << "[FAIL] " << t.first << ": unknown exception\n";
            }
        }
        std::cout << "Summary: " << (tests_.size() - static_cast<size_t>(failures))
                  << "/" << tests_.size() << " passed\n";
        return failures == 0 ? 0 : 1;
    }

private:
    std::vector<std::pair<std::string, TestFn>> tests_;
};

namespace {

void expect_true(bool cond, const std::string& msg) {
    if (!cond) {
        throw std::runtime_error(msg);
    }
}

void expect_eq_u64(uint64_t got, uint64_t expected, const std::string& msg) {
    if (got != expected) {
        throw std::runtime_error(msg + " (got=" + std::to_string(got) +
                                 ", expected=" + std::to_string(expected) + ")");
    }
}

void expect_eq_i64(int64_t got, int64_t expected, const std::string& msg) {
    if (got != expected) {
        throw std::runtime_error(msg + " (got=" + std::to_string(got) +
                                 ", expected=" + std::to_string(expected) + ")");
    }
}

size_t ridx(size_t n, size_t r, size_t c) {
    return r * n + c;
}

uint64_t fib_seq(unsigned n) {
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

uint64_t fib_task(unsigned n, unsigned split_threshold) {
    if (n <= split_threshold) {
        return fib_seq(n);
    }
    return fib_task(n - 1, split_threshold) + fib_task(n - 2, split_threshold);
}

std::pair<uint64_t, uint64_t> fib_fast_pair(unsigned n) {
    if (n == 0U) {
        return {0U, 1U};
    }
    const auto [a, b] = fib_fast_pair(n >> 1U);
    const __uint128_t aa = static_cast<__uint128_t>(a) * a;
    const __uint128_t bb = static_cast<__uint128_t>(b) * b;
    const __uint128_t two_b_minus_a = static_cast<__uint128_t>(2U) * b - a;
    const __uint128_t c128 = static_cast<__uint128_t>(a) * two_b_minus_a;
    const __uint128_t d128 = aa + bb;
    const uint64_t c = static_cast<uint64_t>(c128);
    const uint64_t d = static_cast<uint64_t>(d128);
    if ((n & 1U) == 0U) {
        return {c, d};
    }
    return {d, c + d};
}

uint64_t fib_fast(unsigned n) {
    return fib_fast_pair(n).first;
}

void matmul_tile(size_t n,
                 size_t bs,
                 const std::vector<int64_t>& a,
                 const std::vector<int64_t>& b,
                 std::vector<int64_t>& c,
                 size_t i0,
                 size_t j0) {
    const size_t i_max = std::min(i0 + bs, n);
    const size_t j_max = std::min(j0 + bs, n);

    for (size_t k0 = 0; k0 < n; k0 += bs) {
        const size_t k_max = std::min(k0 + bs, n);
        for (size_t i = i0; i < i_max; ++i) {
            for (size_t k = k0; k < k_max; ++k) {
                const int64_t aik = a[ridx(n, i, k)];
                for (size_t j = j0; j < j_max; ++j) {
                    c[ridx(n, i, j)] += aik * b[ridx(n, k, j)];
                }
            }
        }
    }
}

std::vector<int64_t> matmul_seq(size_t n,
                                const std::vector<int64_t>& a,
                                const std::vector<int64_t>& b) {
    std::vector<int64_t> c(n * n, 0);
    for (size_t i = 0; i < n; ++i) {
        for (size_t k = 0; k < n; ++k) {
            const int64_t aik = a[ridx(n, i, k)];
            for (size_t j = 0; j < n; ++j) {
                c[ridx(n, i, j)] += aik * b[ridx(n, k, j)];
            }
        }
    }
    return c;
}

template <typename Pool>
std::vector<int64_t> matmul_parallel(Pool& pool,
                                     size_t n,
                                     size_t bs,
                                     const std::vector<int64_t>& a,
                                     const std::vector<int64_t>& b) {
    std::vector<int64_t> c(n * n, 0);
    const size_t tiles_i = (n + bs - 1) / bs;
    const size_t tiles_j = (n + bs - 1) / bs;
    const size_t total_tiles = tiles_i * tiles_j;

    std::atomic<size_t> done{0};
    std::mutex m;
    std::condition_variable cv;

    for (size_t ti = 0; ti < tiles_i; ++ti) {
        for (size_t tj = 0; tj < tiles_j; ++tj) {
            const size_t i0 = ti * bs;
            const size_t j0 = tj * bs;
            pool.submit([&, i0, j0] {
                matmul_tile(n, bs, a, b, c, i0, j0);
                const size_t finished = done.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (finished == total_tiles) {
                    std::lock_guard<std::mutex> lk(m);
                    cv.notify_one();
                }
            });
        }
    }

    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [&] { return done.load(std::memory_order_acquire) == total_tiles; });
    return c;
}

void expect_matrix_eq(const std::vector<int64_t>& got,
                      const std::vector<int64_t>& expected,
                      const std::string& msg) {
    expect_true(got.size() == expected.size(), msg + " (shape mismatch)");
    for (size_t i = 0; i < got.size(); ++i) {
        if (got[i] != expected[i]) {
            throw std::runtime_error(msg + " at index " + std::to_string(i) +
                                     " (got=" + std::to_string(got[i]) +
                                     ", expected=" + std::to_string(expected[i]) + ")");
        }
    }
}

class MatrixFibTests {
public:
    static void register_all(TestSuite& suite) {
        suite.add("matrix seq 2x2 known result", matrix_seq_2x2_known_result);
        suite.add("matrix parallel classic matches seq", matrix_parallel_classic_matches_seq);
        suite.add("matrix parallel work stealing matches seq", matrix_parallel_ws_matches_seq);
        suite.add("fibonacci iterative known values", fibonacci_iterative_known_values);
        suite.add("fibonacci recursive-threshold matches iterative", fibonacci_threshold_matches_iterative);
        suite.add("fibonacci fast doubling matches iterative", fibonacci_fast_matches_iterative);
        suite.add("fibonacci batch checksum in thread pool", fibonacci_pool_batch_checksum);
    }

private:
    static void matrix_seq_2x2_known_result() {
        const size_t n = 2;
        const std::vector<int64_t> a = {
            1, 2,
            3, 4
        };
        const std::vector<int64_t> b = {
            5, 6,
            7, 8
        };
        const std::vector<int64_t> expected = {
            19, 22,
            43, 50
        };
        const auto got = matmul_seq(n, a, b);
        expect_matrix_eq(got, expected, "2x2 sequential matrix product mismatch");
    }

    static void matrix_parallel_classic_matches_seq() {
        const size_t n = 5;
        const size_t bs = 2;
        std::vector<int64_t> a(n * n);
        std::vector<int64_t> b(n * n);
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                a[ridx(n, i, j)] = static_cast<int64_t>((i + 1) * (j + 2));
                b[ridx(n, i, j)] = static_cast<int64_t>((i == j) ? 2 : ((i + j) % 3));
            }
        }

        const auto expected = matmul_seq(n, a, b);
        ThreadPool pool(4);
        const auto got = matmul_parallel(pool, n, bs, a, b);
        expect_matrix_eq(got, expected, "classic pool matrix product mismatch");
    }

    static void matrix_parallel_ws_matches_seq() {
        const size_t n = 7;
        const size_t bs = 3;
        std::vector<int64_t> a(n * n);
        std::vector<int64_t> b(n * n);
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                a[ridx(n, i, j)] = static_cast<int64_t>((i + j + 1) % 5 - 2);
                b[ridx(n, i, j)] = static_cast<int64_t>((2 * i + j + 3) % 7 - 3);
            }
        }

        const auto expected = matmul_seq(n, a, b);
        ThreadPool pool(4, ThreadPool::PoolKind::WorkStealing);
        const auto got = matmul_parallel(pool, n, bs, a, b);
        expect_matrix_eq(got, expected, "work stealing pool matrix product mismatch");
    }

    static void fibonacci_iterative_known_values() {
        const std::vector<std::pair<unsigned, uint64_t>> cases = {
            {0U, 0U}, {1U, 1U}, {2U, 1U}, {3U, 2U}, {10U, 55U}, {20U, 6765U}, {40U, 102334155U}
        };
        for (const auto& [n, expected] : cases) {
            expect_eq_u64(fib_seq(n), expected, "fib_seq known value mismatch for n=" + std::to_string(n));
        }
    }

    static void fibonacci_threshold_matches_iterative() {
        const std::vector<unsigned> inputs = {5U, 10U, 20U, 35U, 44U};
        for (unsigned n : inputs) {
            const uint64_t expected = fib_seq(n);
            const uint64_t got = fib_task(n, 16U);
            expect_eq_u64(got, expected, "fib_task mismatch for n=" + std::to_string(n));
        }
    }

    static void fibonacci_fast_matches_iterative() {
        for (unsigned n : {0U, 1U, 2U, 10U, 44U, 90U}) {
            expect_eq_u64(
                fib_fast(n),
                fib_seq(n),
                "fib_fast mismatch for n=" + std::to_string(n));
        }
    }

    static void fibonacci_pool_batch_checksum() {
        constexpr unsigned n = 30U;
        constexpr size_t tasks = 20U;
        const uint64_t each = fib_seq(n);
        const uint64_t expected = each * tasks;

        std::atomic<size_t> done{0};
        std::mutex m;
        std::condition_variable cv;
        std::vector<uint64_t> out(tasks, 0);

        ThreadPool pool(4, ThreadPool::PoolKind::WorkStealing);
        for (size_t i = 0; i < tasks; ++i) {
            pool.submit([&, i] {
                out[i] = fib_task(n, 18U);
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

        const uint64_t checksum = std::accumulate(out.begin(), out.end(), uint64_t{0});
        expect_eq_u64(checksum, expected, "parallel fibonacci checksum mismatch");
        expect_eq_i64(static_cast<int64_t>(done.load(std::memory_order_acquire)),
                      static_cast<int64_t>(tasks),
                      "parallel fibonacci completion count mismatch");
    }
};

}  // namespace

int main() {
    TestSuite suite;
    MatrixFibTests::register_all(suite);
    return suite.run();
}
