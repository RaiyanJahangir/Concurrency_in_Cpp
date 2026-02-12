#include "thread_pool.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

int main() {
    using namespace std::chrono_literals;

    ThreadPool pool(2, 8, 150ms);

    for (int i = 0; i < 24; ++i) {
        pool.submit([i]() {
            std::ostringstream oss;
            oss << "Task " << i
                << " running on OS thread "
                << std::this_thread::get_id()
                << "\n";
            std::cout << oss.str();
            std::this_thread::sleep_for(50ms);
        });
    }

    // Let the pool go idle so excess threads can retire.
    std::this_thread::sleep_for(400ms);

    for (int i = 24; i < 36; ++i) {
        pool.submit([i]() {
            std::ostringstream oss;
            oss << "Task " << i
                << " running on OS thread "
                << std::this_thread::get_id()
                << "\n";
            std::cout << oss.str();
        });
    }

    return 0;
}
