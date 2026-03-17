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

### Experiment Results

All experiment results, including analysis plots, a summary of the experiment runs with all measured metrics can be found in [Results](./results).


## More Documentation

For detailed explanations, see:

- [Usage Guide](docs/usage.md)
- [Benchmarks Guide](docs/benchmarks.md)
