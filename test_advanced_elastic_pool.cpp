#include "thread_pool.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

int main() {
    using namespace std::chrono_literals;

    ThreadPool pool(2, 8, ThreadPool::PoolKind::AdvancedElasticStealing, 150ms);

    for (int i = 0; i < 16; ++i) {
        pool.submit([i, &pool] {
            // Spawn subtasks from worker threads to exercise local deque behavior.
            for (int j = 0; j < 3; ++j) {
                pool.submit([i, j] {
                    std::ostringstream oss;
                    oss << "Task (" << i << "," << j << ") on OS thread "
                        << std::this_thread::get_id() << "\n";
                    std::cout << oss.str();
                    std::this_thread::sleep_for(20ms);
                });
            }
        });
    }

    // Give idle workers time to retire toward the minimum.
    std::this_thread::sleep_for(400ms);

    return 0;
}
