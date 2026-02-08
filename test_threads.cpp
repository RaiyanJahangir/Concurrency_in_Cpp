#include "thread_pool.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <sstream>

int main() {
    constexpr size_t NUM_THREADS = 4;
    constexpr size_t NUM_TASKS   = 40;

    ThreadPool pool(NUM_THREADS);

    std::atomic<int> task_id{0};

    for (int i = 0; i < NUM_TASKS; ++i) {
        pool.submit([i]() {
            std::ostringstream oss;
            oss << "Task " << i
                << " handled by OS thread "
                << std::this_thread::get_id()
                << "\n";
            std::cout << oss.str();
        });
    }

    // Pool destructor waits for all tasks
    return 0;
}
