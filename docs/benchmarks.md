### To start and run an experiment on CloudLab:

Go to "Start an Experiment" at the top left drop-down list.
1. Select a Profile (can just use the default profile)
2. Parameterize - Number of Nodes (1 is ok), Select OS image (Newest Ubuntu), Optional physical node type (check Resource Availability to see which server is available)
3. Finalize - Select the green cluster
4. Schedule - Choose the date and time to get the server, and how many hours we can have the server

Connect to the server through the shell using the `ssh` command given by CloudLab once the experiment time starts.

Clone the repo and run the benchmarks on the server.

### Run the Matrix Multiplication Benchmark "matrix_mul_bench.cpp"

```
g++ -O3 -std=c++20 -pthread matrix_mul_bench.cpp \
  thread_pool.cpp \
  -o matrix_mul_bench
```
Run the benchmark with one of the execution modes:
```
./matrix_mul_bench classic 1024 64 8 1 3
./matrix_mul_bench ws      1024 64 8 1 3
./matrix_mul_bench elastic 1024 64 8 1 3
./matrix_mul_bench advws   1024 64 8 1 3
./matrix_mul_bench coro    1024 64 8 1 3
```
- 1st arg: execution mode (`classic`, `ws`, `elastic`, `advws`, or `coro`)
- 2nd arg: matrix dimension (`N`)
- 3rd arg: block size (`BS`)
- 4th arg: number of threads
- 5th arg: number of warmup runs (not timed)
- 6th arg: number of timed runs (best and average reported)

### Run the Fibonacci Benchmark "fib_bench.cpp"

```
g++ -O3 -std=c++20 -pthread fib_bench.cpp \
  thread_pool.cpp \
  -o fib_bench
```
Run the benchmark with one of the execution modes:
```
./fib_bench classic 44 8 1 3
./fib_bench ws      44 8 1 3
./fib_bench elastic 44 8 1 3
./fib_bench advws   44 8 1 3
./fib_bench coro    44 8 1 3
```
- 1st arg: execution mode (`classic`, `ws`, `elastic`, `advws`, or `coro`)
- 2nd arg: Fibonacci index per task (`fib_n`)
- 3rd arg: number of threads
- 4th arg: number of warmup runs (not timed)
- 5th arg: number of timed runs (best and average reported)
- 6th optional arg: number of tasks submitted per run (default = threads)
- 7th optional arg: recursion split threshold (default = 32)

### Run the Single-Fibonacci Parallel Benchmark "fib_single_bench.cpp"

This benchmark parallelizes one Fibonacci computation tree (instead of running many independent Fibonacci tasks).

```
g++ -O3 -std=c++20 -pthread fib_single_bench.cpp \
  thread_pool.cpp \
  -o fib_single_bench
```
Run the benchmark with one of the execution modes:
```
./fib_single_bench classic 44 8 1 3
./fib_single_bench ws      44 8 1 3
./fib_single_bench elastic 44 8 1 3
./fib_single_bench advws   44 8 1 3
./fib_single_bench coro    44 8 1 3
```
- 1st arg: execution mode (`classic`, `ws`, `elastic`, `advws`, or `coro`)
- 2nd arg: Fibonacci index (`fib_n`)
- 3rd arg: number of threads
- 4th arg: number of warmup runs (not timed)
- 5th arg: number of timed runs (best and average reported)
- 6th optional arg: recursion split threshold for task spawning (default = 30)

### Run the Fast-Doubling Fibonacci Benchmark "fib_fast_bench.cpp"

This benchmark uses the fast doubling Fibonacci algorithm (`O(log n)`) per task and compares all execution modes on many independent tasks.

```
g++ -O3 -std=c++20 -pthread fib_fast_bench.cpp \
  thread_pool.cpp \
  -o fib_fast_bench
```
Run the benchmark with one of the execution modes:
```
./fib_fast_bench classic 90 8 1 3
./fib_fast_bench ws      90 8 1 3
./fib_fast_bench elastic 90 8 1 3
./fib_fast_bench advws   90 8 1 3
./fib_fast_bench coro    90 8 1 3
```
- 1st arg: execution mode (`classic`, `ws`, `elastic`, `advws`, or `coro`)
- 2nd arg: Fibonacci index (`fib_n`, max 93 for `uint64_t`)
- 3rd arg: number of threads
- 4th arg: number of warmup runs (not timed)
- 5th arg: number of timed runs (best and average reported)
- 6th optional arg: number of tasks submitted per run (default = threads)

### Run All CPU-Bound Workloads and Save CSV

Use `run_cpu_workloads.cpp` to build a C++ runner that executes all CPU workloads
(`matrix`, `fib`, `fib_single`, `fib_fast`) across all pool variants
(`classic`, `ws`, `elastic`, `advws`) with multiple trials and exports one CSV.

When to run this C++ runner:
- After changing `thread_pool.cpp`, `thread_pool.h`, or any CPU benchmark file.
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


### Mixed Workload Benchmark

This benchmark evaluates the runtime modes under a mixed workload:
```
CPU work → blocking I/O wait → CPU work
```
#### How to run the benchmark

Compile the HTTP server:
```
g++ -O2 -std=c++20 -pthread mini_http_server.cpp thread_pool.cpp -o mini_http_server
```

Compile the benchmark client:
```
g++ -O2 -std=c++20 -pthread mixed_bench.cpp -o mixed_bench
```

#### Running the Server

In one terminal, run the server with one of the execution modes:
```
./mini_http_server classic 8080 8
./mini_http_server coro    8080 8
./mini_http_server ws      8080 8
./mini_http_server elastic 8080 4 32
./mini_http_server advws   8080 4 32 50
```
#### Running the Benchmark

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

#### Install perf and wrk in CloudLab server

##### perf
```
sudo apt install linux-tools-standard-WSL2 linux-cloud-tools-standard-WSL2
```
To temporarily have permission to use `perf`:
```
sudo sh -c 'echo -1 > /proc/sys/kernel/perf_event_paranoid'
```
To run `perf`:
```
perf stat ./your_program
```
like:
```
perf stat ./matrix_mul_bench advws 8000 64 8 1 3
```
##### wrk
To install wrk:
```
sudo apt update
sudo apt install wrk
```

#### Compare Pools With wrk

If you want to drive the HTTP benchmark with `wrk` instead of the custom
`mixed_bench` client, this repo now includes:
- `wrk_workload.lua`: generates `/work?cpu1=...&io=...&cpu2=...` requests
- `run_mixed_wrk_all_perf_stats.sh`: runs a `wrk` sweep across `classic`, `coro`, `ws`, `elastic`, and `advws` while collecting server-side `perf stat` counters

Single run example:
```
./mini_http_server coro 8080 8
wrk -t4 -c32 -d10s --latency \
  -s ./wrk_workload.lua http://127.0.0.1:8080 -- 200 5000 200
```

The positional values passed after `--` are:
- `cpu1_us`
- `io_us`
- `cpu2_us`

To collect `perf stat` counters for each server mode during the same sweep:
```
chmod +x run_mixed_wrk_all_perf_stats.sh
./run_mixed_wrk_all_perf_stats.sh
```

Optional environment overrides:
```
WRK_THREADS=4 WRK_TIMEOUT=15s ./run_mixed_wrk_all_perf_stats.sh 8080 127.0.0.1
MODES="classic coro ws elastic advws" ./run_mixed_wrk_all_perf_stats.sh
PERF_EVENTS="task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions" ./run_mixed_wrk_all_perf_stats.sh
```

Results are written to:
```
results/mixed_wrk_perf_<timestamp>/summary.csv
```

The CSV captures:
- latency summary from `wrk` (`Latency`, `Req/Sec`, `p50`, `p75`, `p90`, `p99`)
- total requests and throughput
- socket errors, if any
- raw log path for each `(mode, preset)` run

### Mixed Workload Benchmark (Matrix-Backed CPU Stages)

This variant keeps the same HTTP `/work` endpoint shape, but the CPU phases are
implemented as repeated blocked matrix multiplications instead of busy loops.
The request parameters still use:
```
cpu work → blocking I/O wait → cpu work
```
where `cpu1` and `cpu2` are iteration counts for the matrix multiplication kernel.

#### How to run the matrix-backed benchmark

Compile the HTTP server:
```
g++ -O2 -std=c++20 -pthread mini_http_server_matmul.cpp thread_pool.cpp -o mini_http_server_matmul
```

Compile the benchmark client:
```
g++ -O2 -std=c++20 -pthread mixed_bench_matmul.cpp -o mixed_bench_matmul
```

#### Running the Matrix-Backed Server

In one terminal, run the server with one of the execution modes:
```
MIXED_MATMUL_N=64 MIXED_MATMUL_BS=32 ./mini_http_server_matmul classic 8080 8
MIXED_MATMUL_N=64 MIXED_MATMUL_BS=32 ./mini_http_server_matmul coro    8080 8
MIXED_MATMUL_N=64 MIXED_MATMUL_BS=32 ./mini_http_server_matmul ws      8080 8
MIXED_MATMUL_N=64 MIXED_MATMUL_BS=32 ./mini_http_server_matmul elastic 8080 4 32
MIXED_MATMUL_N=64 MIXED_MATMUL_BS=32 ./mini_http_server_matmul advws   8080 4 32 50
```
- `MIXED_MATMUL_N`: matrix dimension used inside each CPU stage (default `64`)
- `MIXED_MATMUL_BS`: blocked matmul tile size (default `32`)

#### Running the Matrix-Backed Benchmark Client

In another terminal:
```
./mixed_bench_matmul 127.0.0.1 8080 2 5000 2 32 10
```
The corresponding arguments are: host port cpu1_iters io_us cpu2_iters concurrency duration_seconds

#### Compare Matrix-Backed Modes With wrk

To sweep all modes and collect `wrk` plus server-side `perf stat` counters:
```
chmod +x run_mixed_wrk_all_perf_stats_matmul.sh
./run_mixed_wrk_all_perf_stats_matmul.sh
```

Optional environment overrides:
```
WRK_THREADS=4 WRK_TIMEOUT=20s ./run_mixed_wrk_all_perf_stats_matmul.sh 8080 127.0.0.1
MODES="classic coro ws elastic advws" ./run_mixed_wrk_all_perf_stats_matmul.sh
MIXED_MATMUL_N=64 MIXED_MATMUL_BS=32 ./run_mixed_wrk_all_perf_stats_matmul.sh
PERF_EVENTS="task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions" ./run_mixed_wrk_all_perf_stats_matmul.sh
```

Results are written to:
```
results/mixed_wrk_matmul_perf_<timestamp>/summary.csv
```