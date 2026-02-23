#include "thread_pool.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
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

template <typename Pred>
bool wait_until(Pred pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

void expect_throws(const std::function<void()>& fn, const std::string& msg) {
    bool threw = false;
    try {
        fn();
    } catch (...) {
        threw = true;
    }
    expect_true(threw, msg);
}

}  // namespace

class ThreadPoolTests {
public:
    static void register_all(TestSuite& suite) {
        suite.add("fixed constructor rejects zero threads", constructor_rejects_zero);
        suite.add("elastic constructor validates bounds", elastic_bounds_validation);
        suite.add("advanced constructor validates kind", advanced_kind_validation);
        suite.add("classic fixed executes all submitted tasks", classic_executes_all);
        suite.add("work stealing executes nested submissions", ws_nested_submissions);
        suite.add("elastic global executes burst workload", elastic_burst_executes_all);
        suite.add("advanced elastic stealing executes nested workload", advanced_nested_executes_all);
    }

private:
    static void constructor_rejects_zero() {
        expect_throws(
            [] { ThreadPool pool(0); },
            "expected fixed constructor to reject zero threads");
    }

    static void elastic_bounds_validation() {
        using ms = std::chrono::milliseconds;
        expect_throws(
            [] { ThreadPool pool(0, 2, ms(50)); },
            "expected elastic constructor to reject min=0");
        expect_throws(
            [] { ThreadPool pool(3, 2, ms(50)); },
            "expected elastic constructor to reject min>max");
    }

    static void advanced_kind_validation() {
        using ms = std::chrono::milliseconds;
        expect_throws(
            [] { ThreadPool pool(2, 4, ThreadPool::PoolKind::WorkStealing, ms(50)); },
            "expected advanced constructor to reject non-advanced kind");
    }

    static void classic_executes_all() {
        constexpr int kTasks = 300;
        std::atomic<int> done{0};
        {
            ThreadPool pool(4);
            for (int i = 0; i < kTasks; ++i) {
                pool.submit([&done] { done.fetch_add(1, std::memory_order_relaxed); });
            }
            expect_true(
                wait_until([&done] { return done.load(std::memory_order_relaxed) == kTasks; },
                           std::chrono::milliseconds(2500)),
                "classic pool did not finish all tasks in time");
        }
        expect_true(done.load(std::memory_order_relaxed) == kTasks, "classic task count mismatch");
    }

    static void ws_nested_submissions() {
        constexpr int kOuter = 24;
        constexpr int kInner = 12;
        const int expected = kOuter * kInner;

        std::atomic<int> done{0};
        {
            ThreadPool pool(4, ThreadPool::PoolKind::WorkStealing);
            for (int i = 0; i < kOuter; ++i) {
                pool.submit([&pool, &done] {
                    for (int j = 0; j < kInner; ++j) {
                        pool.submit([&done] { done.fetch_add(1, std::memory_order_relaxed); });
                    }
                });
            }

            expect_true(
                wait_until([&done, expected] { return done.load(std::memory_order_relaxed) == expected; },
                           std::chrono::milliseconds(3000)),
                "work stealing pool did not finish nested tasks in time");
        }
        expect_true(done.load(std::memory_order_relaxed) == expected, "work stealing task count mismatch");
    }

    static void elastic_burst_executes_all() {
        constexpr int kTasks = 260;
        std::atomic<int> done{0};

        {
            ThreadPool pool(2, 8, std::chrono::milliseconds(80));
            for (int i = 0; i < kTasks; ++i) {
                pool.submit([&done] {
                    done.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                });
            }
            expect_true(
                wait_until([&done] { return done.load(std::memory_order_relaxed) == kTasks; },
                           std::chrono::milliseconds(4000)),
                "elastic global pool did not finish burst tasks in time");
        }

        expect_true(done.load(std::memory_order_relaxed) == kTasks, "elastic global task count mismatch");
    }

    static void advanced_nested_executes_all() {
        constexpr int kOuter = 16;
        constexpr int kInner = 10;
        const int expected = kOuter * kInner;

        std::atomic<int> done{0};
        {
            ThreadPool pool(
                2, 8, ThreadPool::PoolKind::AdvancedElasticStealing, std::chrono::milliseconds(80));
            for (int i = 0; i < kOuter; ++i) {
                pool.submit([&pool, &done] {
                    for (int j = 0; j < kInner; ++j) {
                        pool.submit([&done] {
                            done.fetch_add(1, std::memory_order_relaxed);
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        });
                    }
                });
            }

            expect_true(
                wait_until([&done, expected] { return done.load(std::memory_order_relaxed) == expected; },
                           std::chrono::milliseconds(4500)),
                "advanced elastic stealing pool did not finish nested tasks in time");
        }
        expect_true(done.load(std::memory_order_relaxed) == expected, "advanced pool task count mismatch");
    }
};

int main() {
    TestSuite suite;
    ThreadPoolTests::register_all(suite);
    return suite.run();
}
