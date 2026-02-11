// To run this test:
// g++ -O2 -std=c++20 -pthread test_forkjoin_workstealing.cpp thread_pool_forkjoin.cpp -o ws_test
// ./ws_test

#include "thread_pool.h"

#include <atomic>
#include <iostream>
#include <sstream>
#include <thread>

int main() {
    constexpr size_t NUM_THREADS = 4;
    WorkStealingThreadPool pool(NUM_THREADS);

    std::atomic<int> counter{0};

    for (int i = 0; i < 8; ++i) {
        pool.submit([i, &pool, &counter] {
            for (int j = 0; j < 5; ++j) {
                pool.submit([i, j, &counter] {
                    counter.fetch_add(1, std::memory_order_relaxed);
                    std::ostringstream oss;
                    oss << "Task (" << i << "," << j << ") on thread "
                        << std::this_thread::get_id() << "\n";
                    std::cout << oss.str();
                });
            }
        });
    }

    // Destructor drains all queued tasks.
    return 0;
}

