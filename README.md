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

## Repository Structure

This repository is organized around the runtime implementations, the workloads
used to evaluate them, and the scripts/results used for measurement.

- Core runtime:
  - `thread_pool.h`, `thread_pool.cpp`: shared `ThreadPool` implementation with
    the classic fixed pool, work-stealing pool, elastic global-queue pool, and
    advanced elastic work-stealing pool.
  - `coro_runtime.h`: the single coroutine runtime used by this project. It
    provides a pool-backed scheduler, coroutine tasks, detached tasks, and
    coroutine-friendly synchronization primitives.

- CPU-bound benchmarks:
  - `matrix_mul_bench.cpp`: blocked matrix multiplication benchmark.
  - `fib_bench.cpp`: batched recursive-threshold Fibonacci benchmark.
  - `fib_single_bench.cpp`: single-tree parallel Fibonacci benchmark.
  - `fib_fast_bench.cpp`: batched fast-doubling Fibonacci benchmark.

- Mixed workload benchmarks:
  - `mini_http_server.cpp`, `mixed_bench.cpp`: mixed HTTP benchmark with
    CPU-busy-work, blocking I/O delay, and CPU-busy-work stages.
  - `mini_http_server_matmul.cpp`, `mixed_bench_matmul.cpp`: mixed HTTP
    benchmark where the CPU stages are implemented as repeated blocked matrix
    multiplication.
  - `wrk_workload.lua`: shared `wrk` request generator for the `/work` endpoint.

- Experiment automation and analysis:
  - `run_cpu_workloads.cpp`: consolidated CPU benchmark runner that exports CSV.
  - `run_matrix_mul_all.sh`, `run_fib_all_perf_stats.sh`,
    `run_mixed_wrk_all_perf_stats.sh`,
    `run_mixed_wrk_all_perf_stats_matmul.sh`: experiment scripts for repeated
    measurement and result collection.
  - `analyze_fib_summary.py`, `analyze_mixed_wrk_summary.py`: summary analysis.
  - `plot_matrix_mul_summary.py`, `plot_fib_summary.py`,
    `plot_mixed_wrk_summary.py`: plotting scripts for the saved summaries.

- Tests:
  - `test_threads.cpp`, `test_forkjoin_workstealing.cpp`,
    `test_elastic_pool.cpp`, `test_advanced_elastic_pool.cpp`: focused runtime
    tests/demos for individual pool designs.
  - `test_thread_pool_unit.cpp`: unit-style tests for the main pool behaviors.
  - `test_matrix_fib_unit.cpp`: correctness tests for matrix and Fibonacci
    kernels.
  - `test_mini_http_server_unit.cpp`: unit-style tests for HTTP request parsing
    and response handling.

- Archived outputs:
  - `results/`: saved CSV summaries, analysis reports, and plots used in the
    project evaluation.

## Project Concepts Mapped to Code

The project plan was centered on comparing different concurrency models under
CPU-bound and mixed CPU/I/O workloads. The implemented code maps to those ideas
as follows:

- Baseline fixed-size thread pool with a single shared queue:
  implemented in `thread_pool.h` / `thread_pool.cpp` as the classic fixed mode
  (`ThreadPool::PoolKind::ClassicFixed`), exercised by the `classic` benchmark
  mode.

- Work-stealing thread pool:
  implemented in `thread_pool.h` / `thread_pool.cpp` as the work-stealing mode
  (`ThreadPool::PoolKind::WorkStealing`), exercised by the `ws` benchmark mode.

- Elastic thread pool with dynamic worker management:
  implemented in `thread_pool.h` / `thread_pool.cpp` as the elastic global-queue
  configuration, exercised by the `elastic` benchmark mode.

- Advanced elastic work-stealing pool:
  implemented in `thread_pool.h` / `thread_pool.cpp` as
  `ThreadPool::PoolKind::AdvancedElasticStealing`, exercised by the `advws`
  benchmark mode.

- Coroutine-based execution model:
  implemented as one coroutine runtime in `coro_runtime.h`, then integrated into
  the `coro` mode of the benchmark and server programs. This repository does not
  implement four coroutine variants; it implements one coroutine model that runs
  on top of the shared pool infrastructure.

- CPU-bound application study:
  implemented by `matrix_mul_bench.cpp`, `fib_bench.cpp`,
  `fib_single_bench.cpp`, and `fib_fast_bench.cpp`.

- Mixed workload study (CPU + I/O):
  implemented by the HTTP `/work` servers in `mini_http_server.cpp` and
  `mini_http_server_matmul.cpp`, with load generators in `mixed_bench.cpp`,
  `mixed_bench_matmul.cpp`, and `wrk_workload.lua`.

- Repeated measurement, CSV export, and evaluation:
  implemented by `run_cpu_workloads.cpp`, the `run_*.sh` scripts, the
  `analyze_*.py` scripts, the `plot_*.py` scripts, and the archived `results/`
  directory.

If you are reading the repository for the first time, the most important files
to start with are `thread_pool.h`, `thread_pool.cpp`, `coro_runtime.h`, the
four CPU benchmarks, the two HTTP server variants, and the scripts under the
experiment automation section above.

## Scripts and How to Use Them

The repository includes runner scripts for collecting results and Python scripts
for analyzing and plotting those results. The most common workflow is:

1. Run one of the experiment scripts to generate a new timestamped folder in `results/`.
2. Inspect the generated `summary.csv` (and `runs.csv` where applicable).
3. Run the corresponding analysis or plotting script on that saved output.

### Experiment runner scripts

`run_matrix_mul_all.sh`
- Purpose: builds `matrix_mul_bench`, sweeps matrix sizes `1024/2048/4096`,
  modes `classic/coro/ws/elastic/advws`, and thread counts `1/2/4/8/16`.
- Output: `results/matrix_mul_perf_<timestamp>/summary.csv` plus per-run logs.
- Run:
```bash
bash run_matrix_mul_all.sh
```
- Notes: this script currently uses the configuration values defined near the top
  of the file (`MODES`, `THREADS`, `NS`, `BS`, `WARMUP`, `REPS`).

`run_fib_all_perf_stats.sh`
- Purpose: builds the three Fibonacci benchmark binaries, sweeps their presets,
  records benchmark timing, `/usr/bin/time -v` statistics, and `perf stat`
  counters when `perf` is available.
- Output: `results/fib_perf_<timestamp>/summary.csv` plus benchmark/time/perf logs.
- Run:
```bash
bash run_fib_all_perf_stats.sh
```
- Common overrides:
```bash
THREADS_LIST="1 2 4 8 16" REPS=3 bash run_fib_all_perf_stats.sh
MODES="classic coro ws elastic advws" bash run_fib_all_perf_stats.sh
PERF_EVENTS="task-clock,context-switches,cpu-migrations,cycles,instructions" bash run_fib_all_perf_stats.sh
```

`run_mixed_wrk_all_perf_stats.sh`
- Purpose: runs the busy-wait HTTP server benchmark across all runtime modes
  using `wrk`, and collects throughput, latency, and server-side `perf` data.
- Output: `results/mixed_wrk_perf_<timestamp>/summary.csv` and `runs.csv`.
- Run:
```bash
bash run_mixed_wrk_all_perf_stats.sh
```
- Common overrides:
```bash
WRK_THREADS=4 WRK_TIMEOUT=15s bash run_mixed_wrk_all_perf_stats.sh 8080 127.0.0.1
MODES="classic coro ws elastic advws" bash run_mixed_wrk_all_perf_stats.sh
PERF_EVENTS="task-clock,context-switches,cpu-migrations,cycles,instructions" bash run_mixed_wrk_all_perf_stats.sh
```

`run_mixed_wrk_all_perf_stats_matmul.sh`
- Purpose: runs the matrix-backed HTTP workload across all runtime modes using
  `wrk`, and collects throughput, latency, and server-side `perf` data.
- Output: `results/mixed_wrk_matmul_perf_<timestamp>/summary.csv` and `runs.csv`.
- Run:
```bash
bash run_mixed_wrk_all_perf_stats_matmul.sh
```
- Common overrides:
```bash
WRK_THREADS=4 WRK_TIMEOUT=20s bash run_mixed_wrk_all_perf_stats_matmul.sh 8080 127.0.0.1
MIXED_MATMUL_N=64 MIXED_MATMUL_BS=32 bash run_mixed_wrk_all_perf_stats_matmul.sh
MODES="classic coro ws elastic advws" bash run_mixed_wrk_all_perf_stats_matmul.sh
```

`run_cpu_workloads.cpp`
- Purpose: a C++ runner that executes the CPU-only workloads and writes one CSV
  containing application metrics, `/usr/bin/time` metrics, and `perf` metrics
  when available.
- Output: a CSV in `results/`, such as `results/cpu_metrics.csv`.
- Build and run:
```bash
g++ -O2 -std=c++20 -pthread run_cpu_workloads.cpp -o run_cpu_workloads
./run_cpu_workloads
./run_cpu_workloads results/cpu_metrics.csv 5
```

### Analysis scripts

`analyze_fib_summary.py`
- Purpose: reads a Fibonacci `summary.csv` and produces a Markdown report with
  winners, normalized runtime, speedups vs `classic`, and per-thread summaries.
- Run:
```bash
python3 analyze_fib_summary.py results/fib_perf_<timestamp>/summary.csv
python3 analyze_fib_summary.py results/fib_perf_<timestamp>/summary.csv --out results/fib_perf_<timestamp>/analysis_report.md
```

`analyze_mixed_wrk_summary.py`
- Purpose: reads mixed-workload `summary.csv` and `runs.csv`, then produces a
  Markdown report with throughput winners, scaling, stability, and normalized
  perf metrics.
- Run:
```bash
python3 analyze_mixed_wrk_summary.py \
  results/mixed_wrk_perf_<timestamp>/summary.csv \
  --runs results/mixed_wrk_perf_<timestamp>/runs.csv

python3 analyze_mixed_wrk_summary.py \
  results/mixed_wrk_perf_<timestamp>/summary.csv \
  --runs results/mixed_wrk_perf_<timestamp>/runs.csv \
  --out results/mixed_wrk_perf_<timestamp>/analysis_report.md
```

### Plotting scripts

`plot_matrix_mul_summary.py`
- Purpose: generates the matrix-multiplication plots from `summary.csv`.
- Output: a `plots/` directory containing `svg`, `png`, or both, plus `PLOTS.md`.
- Run:
```bash
python3 plot_matrix_mul_summary.py results/matrix_mul_perf_<timestamp>/summary.csv
python3 plot_matrix_mul_summary.py results/matrix_mul_perf_<timestamp>/summary.csv --format png --png-scale 4
python3 plot_matrix_mul_summary.py results/matrix_mul_perf_<timestamp>/summary.csv --format both
```
- Notes: `--format auto` is the default. It prefers PNG when a rasterizer is
  available and otherwise falls back to SVG.

`plot_fib_summary.py`
- Purpose: generates Fibonacci plots such as runtime scaling, speedup vs
  `classic`, winner heatmaps, and resource/stability figures.
- Output: `results/fib_perf_<timestamp>/plots/`.
- Run:
```bash
python3 plot_fib_summary.py results/fib_perf_<timestamp>/summary.csv
python3 plot_fib_summary.py results/fib_perf_<timestamp>/summary.csv --outdir results/fib_perf_<timestamp>/plots
```

`plot_mixed_wrk_summary.py`
- Purpose: generates the mixed-workload throughput, latency, winner, perf, and
  non-coroutine comparison plots from `summary.csv` and `runs.csv`.
- Output: `results/mixed_wrk_perf_<timestamp>/plots/`.
- Run:
```bash
python3 plot_mixed_wrk_summary.py \
  results/mixed_wrk_perf_<timestamp>/summary.csv \
  --runs results/mixed_wrk_perf_<timestamp>/runs.csv

python3 plot_mixed_wrk_summary.py \
  results/mixed_wrk_perf_<timestamp>/summary.csv \
  --runs results/mixed_wrk_perf_<timestamp>/runs.csv \
  --plot noncoro
```
- Notes: `--plot` can be `all`, a single plot id such as `01` or `05`, `md`,
  or `noncoro` for the non-coroutine comparison subset.

### Supporting workload script

`wrk_workload.lua`
- Purpose: helper request generator used by the mixed-workload `wrk` runs.
- Use it with:
```bash
wrk -t4 -c32 -d10s --latency \
  -s ./wrk_workload.lua http://127.0.0.1:8080 -- 200 5000 200
```
- The positional values after `--` are `cpu1`, `io`, and `cpu2` for the
  `/work` endpoint parameters.

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

## Work-Stealing Fixed Pool (Fixed Threads + Per-Thread Queues)

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
g++ -std=c++20 -O1 -pthread thread_pool.cpp test_thread_pool_unit.cpp -o thread_pool_unit_test
```
Run:
```
./thread_pool_unit_test
```
The executable prints `[PASS]/[FAIL]` per test and returns non-zero on failure.

## Unit Tests for Matrix and Fibonacci Kernels

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

## Unit Tests for Mini HTTP Server

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

## Run the Matrix Multiplication Benchmark "matrix_mul_bench.cpp"

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

## To start and run an experiment on CloudLab:

Go to "Start an Experiment" at the top left drop-down list.
1. Select a Profile (can just use the default profile)
2. Parameterize - Number of Nodes (1 is ok), Select OS image (Newest Ubuntu), Optional physical node type (check Resource Availability to see which server is available)
3. Finalize - Select the green cluster
4. Schedule - Choose the date and time to get the server, and how many hours we can have the server

Connect to the server through the shell using the `ssh` command given by CloudLab once the experiment time starts.

Clone the repo and run the benchmarks on the server.

## Run the Fibonacci Benchmark "fib_bench.cpp"

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

## Run the Single-Fibonacci Parallel Benchmark "fib_single_bench.cpp"

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

## Run the Fast-Doubling Fibonacci Benchmark "fib_fast_bench.cpp"

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

## Run All CPU-Bound Workloads and Save CSV

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


## Mixed Workload Benchmark

This benchmark evaluates the runtime modes under a mixed workload:
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

In one terminal, run the server with one of the execution modes:
```
./mini_http_server classic 8080 8
./mini_http_server coro    8080 8
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

### Install perf and wrk in CloudLab server

#### perf
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
#### wrk
To install wrk:
```
sudo apt update
sudo apt install wrk
```

### Compare Pools With wrk

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

## Mixed Workload Benchmark (Matrix-Backed CPU Stages)

This variant keeps the same HTTP `/work` endpoint shape, but the CPU phases are
implemented as repeated blocked matrix multiplications instead of busy loops.
The request parameters still use:
```
cpu work → blocking I/O wait → cpu work
```
where `cpu1` and `cpu2` are iteration counts for the matrix multiplication kernel.

### How to run the matrix-backed benchmark

Compile the HTTP server:
```
g++ -O2 -std=c++20 -pthread mini_http_server_matmul.cpp thread_pool.cpp -o mini_http_server_matmul
```

Compile the benchmark client:
```
g++ -O2 -std=c++20 -pthread mixed_bench_matmul.cpp -o mixed_bench_matmul
```

### Running the Matrix-Backed Server

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

### Running the Matrix-Backed Benchmark Client

In another terminal:
```
./mixed_bench_matmul 127.0.0.1 8080 2 5000 2 32 10
```
The corresponding arguments are: host port cpu1_iters io_us cpu2_iters concurrency duration_seconds

### Compare Matrix-Backed Modes With wrk

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
