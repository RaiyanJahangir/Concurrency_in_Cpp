### Classic fixed pool (fixed threads + Global Queue)
Initial implementation of a classic fixed pool with a fixed number of threads and a global queue. We did a simple test by using 4 threads to handle 40 tasks (simple print statement) 
to make the initial implementation work.

Build:
```
g++ -O2 -std=c++20 -pthread test_threads.cpp thread_pool.cpp -o pool_test
```
Run:
```
./pool_test
```

### Work-Stealing Fixed Pool (Fixed Threads + Per-Thread Queues)

Added a fixed-size work-stealing pool that keeps per-thread deques and allows
threads to steal tasks when they run out of local work.

Build:
```
g++ -O2 -std=c++20 -pthread test_forkjoin_workstealing.cpp thread_pool.cpp -o ws_test
```
Run:
```
./ws_test
```

### Elastic Pool (Dynamic Threads + Global Queue)

Added an elastic thread pool that grows/shrinks between a minimum and a maximum
thread count with an idle timeout.

Files:
- thread_pool.h
- thread_pool.cpp
- test_elastic_pool.cpp

Example build:
```
g++ -std=c++20 -O2 -pthread thread_pool.cpp test_elastic_pool.cpp -o elastic_test
```
Run:
```
./elastic_test
```

### Advanced Elastic Pool (Dynamic Threads + Per-thread Queues + Stealing)

Added an advanced elastic pool that combines:
- dynamic threads (`min_threads` to `max_threads`)
- per-thread local deques
- work stealing between threads

Build:
```
g++ -std=c++20 -O2 -pthread thread_pool.cpp test_advanced_elastic_pool.cpp -o adv_elastic_test
```
Run:
```
./adv_elastic_test
```

### Unit-Style ThreadPool Tests (Class-Based)

Added `test_thread_pool_unit.cpp`, a class-based test executable with assertion-style checks for:
- constructor validation
- classic fixed pool task completion
- work-stealing nested submissions
- elastic global burst processing
- advanced elastic stealing nested workload completion

Build:
```
g++ -std=c++20 -O1 -pthread thread_pool.cpp test_thread_pool_unit.cpp -o thread_pool_unit_test
```
Run:
```
./thread_pool_unit_test
```
The executable prints `[PASS]/[FAIL]` per test and returns non-zero on failure.

### Unit Tests for Matrix and Fibonacci Kernels

Added `test_matrix_fib_unit.cpp`, a unit-style test executable that validates:
- matrix multiplication correctness (known 2x2 case)
- blocked parallel matrix multiplication against sequential reference
- Fibonacci correctness for iterative, recursive-threshold, and fast-doubling variants
- parallel Fibonacci batch checksum with thread-pool execution

Build:
```
g++ -std=c++20 -O1 -pthread thread_pool.cpp test_matrix_fib_unit.cpp -o matrix_fib_unit_test
```
Run:
```
./matrix_fib_unit_test
```

### Unit Tests for Mini HTTP Server

Added `test_mini_http_server_unit.cpp`, a unit-style test executable that validates:
- request parsing helpers (`parse_int`, query parsing, request-target parsing)
- HTTP response formatting (status/content-type/content-length/body)
- route handling for invalid method (`400`), unknown path (`404`), and `/work` (`200` JSON)

Build:
```
g++ -std=c++20 -O2 -pthread test_mini_http_server_unit.cpp thread_pool.cpp -o test_mini_http_server_unit
```
Run:
```
./test_mini_http_server_unit
```
The executable prints `[PASS]/[FAIL]` per test and returns non-zero on failure.

Note: if your GCC version hits an internal compiler error with `-O2`, use `-O1` or `-O2 -fno-cprop-registers`.
