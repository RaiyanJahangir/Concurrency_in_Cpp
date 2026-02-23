# Concurrency\_in\_Cpp

Comparing the performance of threads and coroutines in C++

## C++20 Toolchain Install

Ubuntu (GCC/G++): https://documentation.ubuntu.com/ubuntu-for-developers/howto/gcc-setup/
```
sudo apt install gcc g++
```

macOS (LLVM/Clang via Homebrew): https://formulae.brew.sh/formula/llvm
```
brew install llvm
```

Windows (MSVC Build Tools): https://visualstudio.microsoft.com/downloads/
Download and install Build Tools for Visual Studio, then select the C++ workload during setup.

## Git Installation
Install [Git](https://git-scm.com/downloads).

- After installation, clone the repository
```
git clone https://github.com/RaiyanJahangir/Concurrency_in_Cpp.git
```
- Enter the cloned directory
```
cd Concurrency_in_Cpp
```

## Classic fixed pool (fixed threads + Global Queue)
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

## Elastic Pool (Dynamic Threads + Global Queue)

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

## Advanced Elastic Pool (Dynamic Threads + Per-thread Queues + Stealing)

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

## Unit-Style ThreadPool Tests (Class-Based)

Added `test_thread_pool_unit.cpp`, a class-based test executable with assertion-style checks for:
- constructor validation
- classic fixed pool task completion
- work-stealing nested submissions
- elastic global burst processing
- advanced elastic stealing nested workload completion

Build:
```
g++ -std=c++20 -O2 -pthread thread_pool.cpp test_thread_pool_unit.cpp -o thread_pool_unit_test
```
Run:
```
./thread_pool_unit_test
```
The executable prints `[PASS]/[FAIL]` per test and returns non-zero on failure.

## Run the Matrix Multiplication Benchmark "matrix_mul_bench.cpp"

```
g++ -O3 -std=c++20 -pthread matrix_mul_bench.cpp \
  thread_pool.cpp \
  -o matrix_mul_bench
```
Run the benchmark with one of the thread pool
```
./matrix_mul_bench classic 1024 64 8 1 3
./matrix_mul_bench ws      1024 64 8 1 3
./matrix_mul_bench elastic 1024 64 8 1 3
./matrix_mul_bench advws   1024 64 8 1 3
```
- 1st arg: number of the matrix dimension (N)
- 2nd arg: block size (BS)
- 3rd arg: number of threads
- 4th arg: number of warmup runs (not timed)
- 5th arg: number of timed runs (best and average reported)



## To start and run an experiment on CloudLab:

Go to "Start an Experment" at the top left drop-down list
1. Select a Profile (can just use the default profile)
2. Parameterize - Number of Nodes (1 is ok), Select OS image (Newest Ubuntu), Optional physical node type (check Resource Availability to see which server is available)
3. Finalize - Select the green cluster
4. Schedule - Choose the date and time to get the server, and how many hours we can have the server

Connect to the server through the shell using ssh commend given by the CloudLab once the experiment time starts

Clone the repo and run the test on the server

## Run the Fibonacci Benchmark "fib_bench.cpp"

```
g++ -O3 -std=c++20 -pthread fib_bench.cpp \
  thread_pool.cpp \
  -o fib_bench
```
Run the benchmark with one of the thread pool
```
./fib_bench classic 44 8 1 3
./fib_bench ws      44 8 1 3
./fib_bench elastic 44 8 1 3
./fib_bench advws   44 8 1 3
```
- 1st arg: Fibonacci index per task (`fib_n`)
- 2nd arg: number of threads
- 3rd arg: number of warmup runs (not timed)
- 4th arg: number of timed runs (best and average reported)
- 5th optional arg: number of tasks submitted per run (default = threads)
- 6th optional arg: recursion split threshold (default = 32)

## Run the Single-Fibonacci Parallel Benchmark "fib_single_bench.cpp"

This benchmark parallelizes one Fibonacci computation tree (instead of running many independent Fibonacci tasks).

```
g++ -O3 -std=c++20 -pthread fib_single_bench.cpp \
  thread_pool.cpp \
  -o fib_single_bench
```
Run the benchmark with one of the thread pool
```
./fib_single_bench classic 44 8 1 3
./fib_single_bench ws      44 8 1 3
./fib_single_bench elastic 44 8 1 3
./fib_single_bench advws   44 8 1 3
```
- 1st arg: Fibonacci index (`fib_n`)
- 2nd arg: number of threads
- 3rd arg: number of warmup runs (not timed)
- 4th arg: number of timed runs (best and average reported)
- 5th optional arg: recursion split threshold for task spawning (default = 30)

## Run the Fast-Doubling Fibonacci Benchmark "fib_fast_bench.cpp"

This benchmark uses the fast doubling Fibonacci algorithm (`O(log n)`) per task and compares the 4 pool variants on many independent tasks.

```
g++ -O3 -std=c++20 -pthread fib_fast_bench.cpp \
  thread_pool.cpp \
  -o fib_fast_bench
```
Run the benchmark with one of the thread pool
```
./fib_fast_bench classic 90 8 1 3
./fib_fast_bench ws      90 8 1 3
./fib_fast_bench elastic 90 8 1 3
./fib_fast_bench advws   90 8 1 3
```
- 1st arg: Fibonacci index (`fib_n`, max 93 for `uint64_t`)
- 2nd arg: number of threads
- 3rd arg: number of warmup runs (not timed)
- 4th arg: number of timed runs (best and average reported)
- 5th optional arg: number of tasks submitted per run (default = threads)

## Run All CPU-Bound Workloads and Save CSV

Use `run_cpu_workloads.cpp` to build a C++ runner that executes all CPU workloads
(`matrix`, `fib`, `fib_single`, `fib_fast`) across all pool variants
(`classic`, `ws`, `elastic`, `advws`) with multiple trials and exports one CSV.

When to run this C++ runner:
- After changing `thread_pool.cpp/.h` or any CPU benchmark file.
- Before collecting final report numbers/graphs.
- On an otherwise idle machine for more stable measurements.

Build the C++ runner from source:
```bash
cd Concurrency_in_Cpp
g++ -O2 -std=c++20 -pthread run_cpu_workloads.cpp -o run_cpu_workloads
```

Run the C++ executable with defaults:
```bash
./run_cpu_workloads
```
This writes a timestamped CSV in `results/`.

Run with explicit output path and trial count:
```bash
./run_cpu_workloads results/cpu_metrics.csv 5
```
- 1st positional arg: output CSV path
- 2nd positional arg: number of trials per `(workload, pool)` combination

Optional: enable `perf` metrics if your system blocks non-root perf access:
```bash
sudo sysctl -w kernel.perf_event_paranoid=-1
./run_cpu_workloads results/cpu_metrics_perf.csv 5
```

Run with custom benchmark parameters (proposal-style example):
```bash
  TRIALS=5 THREADS=8 WARMUP=1 REPS=3 \
  MATRIX_N=1024 MATRIX_BS=64 \
  FIB_N=44 FIB_TASKS=8 FIB_SPLIT_THRESHOLD=32 \
  FIB_SINGLE_N=44 FIB_SINGLE_SPLIT_THRESHOLD=30 \
  FIB_FAST_N=90 FIB_FAST_TASKS=8 \
  ./run_cpu_workloads results/cpu_metrics.csv
```

Parameter meanings:
- `TRIALS`: repeated runs of each workload/pool pair.
- `THREADS`: worker threads used by each pool.
- `WARMUP`: untimed warmup runs passed to each benchmark.
- `REPS`: timed runs passed to each benchmark (used for per-run timing stats).
- `MATRIX_N`, `MATRIX_BS`: matrix benchmark size and block size.
- `FIB_N`, `FIB_TASKS`, `FIB_SPLIT_THRESHOLD`: batch Fibonacci benchmark parameters.
- `FIB_SINGLE_N`, `FIB_SINGLE_SPLIT_THRESHOLD`: single-tree Fibonacci benchmark parameters.
- `FIB_FAST_N`, `FIB_FAST_TASKS`: fast-doubling Fibonacci benchmark parameters.

CSV includes:
- Application metrics: per-run times, p50/p95/p99 of run times, best/avg time, throughput, checksum fields.
- OS metrics from `/usr/bin/time`: elapsed/user/sys time, CPU%, max RSS, context switches.
- `perf` metrics (when available): task-clock, context-switches, cpu-migrations, cycles, instructions, cache-misses.


## Mixed Workload Benchmark

This benchmark evaluates different ThreadPool implementations under a mixed workload:
```
CPU work → blocking I/O wait → CPU work
```
### How to run the benchmark

Compile the HTTP server:
```
g++ -O2 -std=c++20 -pthread mini_http_server.cpp thread_pool.cpp -o mini_http_server
```

Compile the benchmark client:
```
g++ -O2 -std=c++20 -pthread mixed_bench.cpp -o mixed_bench
```

### Running the Server

In one terminal, run the server with one of the thread pools:
```
./mini_http_server classic 8080 8
./mini_http_server ws      8080 8
./mini_http_server elastic 8080 4 32
./mini_http_server advws   8080 4 32 50
```
### Running the Benchmark

In another terminal:
```
./mixed_bench 127.0.0.1 8080 200 5000 200 32 10
```
The corresponding arguments are: host port cpu1_us io_us cpu2_us concurrency duration_seconds

Output Summary:
- Concurrency: Number of simultaneous clients.
- Throughput (req/s): Requests completed per second (system capacity).
- Latency (avg, p50, p95, p99): Time per request.
- p50 = median
- p95/p99 = tail latency (stability under load)

How to Read It:
- Throughput rising + low latency → good scaling.
- Throughput plateaus + latency increases → pool is saturated.
- p95/p99 much higher than p50 → queueing and overload.
- All latencies high and similar → fully overloaded system.
