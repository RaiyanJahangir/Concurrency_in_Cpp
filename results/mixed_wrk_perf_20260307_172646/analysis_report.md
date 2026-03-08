# Mixed HTTP Workload Analysis

Source files:
- `results/mixed_wrk_perf_20260307_172646/summary.csv`
- `results/mixed_wrk_perf_20260307_172646/runs.csv`

Dataset:
- Summary rows: 150 (`5 threads x 6 presets x 5 modes`)
- Run rows: 450 (`3 reps` per summary row)
- Modes: `advws`, `classic`, `coro`, `elastic`, `ws`
- Threads tested: `1`, `2`, `4`, `8`, `16`


## Data Quality
- All runs succeeded (`exit_code=0` for all 450 runs).
- No socket errors were recorded.
- `successful_runs=3` for every summary row.


## Scenario Winners (Throughput)
Winner criterion: highest `avg_requests_sec` per `(threads, preset)` scenario.

| mode | wins |
|---|---|
| coro | 22 |
| advws | 7 |
| elastic | 1 |

Non-`coro` winners occurred in `8` scenarios: advws:7, elastic:1.


## Winner Counts by Thread

| thread | scenario_count | winner_breakdown |
|---|---|---|
| t1 | 6 | coro:5, advws:1 |
| t2 | 6 | coro:5, advws:1 |
| t4 | 6 | coro:5, advws:1 |
| t8 | 6 | coro:4, advws:2 |
| t16 | 6 | coro:3, advws:2, elastic:1 |


## Avg Normalized Throughput by Mode
Normalization: `avg_requests_sec / best_requests_sec_in_same_scenario` (higher is better, `1.0` is best).

| mode | avg_norm_throughput |
|---|---|
| coro | 0.9249 |
| advws | 0.5005 |
| elastic | 0.4996 |
| classic | 0.2699 |
| ws | 0.2682 |


## Geometric Mean vs Classic
Across all `(threads, preset)` scenarios:

| mode | throughput_speedup_vs_classic | latency_ratio_vs_classic |
|---|---|---|
| coro | 5.156x | 0.245x |
| advws | 1.943x | 0.650x |
| elastic | 1.941x | 0.648x |
| ws | 0.993x | 1.056x |

Interpretation:
- `coro` delivers about `5.16x` throughput and about `75.5%` lower average latency than `classic`.
- Near parity with `classic`: `ws`.
- Faster than `classic`: `advws` (1.94x), `elastic` (1.94x).


## Coro vs Classic by Preset
Geometric mean across threads (`1,2,4,8,16`):

| preset | coro_throughput_speedup | coro_latency_ratio |
|---|---|---|
| p1_light_cpu_heavy_io | 3.898x | 0.257x |
| p2_balanced | 5.969x | 0.167x |
| p3_cpu_heavy | 1.087x | 0.921x |
| p4_io_heavy_high_conc | 14.079x | 0.071x |
| p5_stress_mix | 3.356x | 0.298x |
| p6_ultra_io_bound | 15.735x | 0.257x |

Key pattern: `coro` advantage is strongest on I/O-heavy presets and smallest on CPU-heavy preset (`p3`).


## Scaling Behavior (t1 to t16)
Throughput ratio `RPS(t16)/RPS(t1)`:
- `classic`: `~15.9x` average across presets
- `coro`: mixed scaling
  - `p1_light_cpu_heavy_io`: `~1.0x`
  - `p2_balanced`: `~2.6x`
  - `p3_cpu_heavy`: `~15.8x`
  - `p4_io_heavy_high_conc`: `~1.6x`
  - `p5_stress_mix`: `~15.1x`
  - `p6_ultra_io_bound`: `~1.0x`
- `ws`: `~15.9x` average across presets
- `elastic`: `~14.2x` average across presets
- `advws`: `~14.2x` average across presets


## Run-to-Run Stability
Metric: coefficient of variation (CV) of `requests_sec` across the 3 repetitions per scenario.

| mode | avg_cv_requests_sec |
|---|---|
| classic | 0.000956 |
| advws | 0.001326 |
| coro | 0.001988 |
| elastic | 0.002632 |
| ws | 0.009772 |

Observed mode-average CV ranges from `0.0010` to `0.0098` (lower is more stable).


## Additional Perf Metrics from `runs.csv`
The `runs.csv` file includes Linux perf counters (`perf_*` columns).
Raw counters rise with throughput, so the most useful comparison is normalized work: counters per 1,000 requests.


## Mode-Level Perf Summary

| mode | avg_rps | ctx_switches_per_1k_req | cpu_migrations_per_1k_req | cycles_per_1k_req | instructions_per_1k_req | cache_misses_per_1k_req | IPC | branch_miss_rate | cache_miss_rate |
|---|---|---|---|---|---|---|---|---|---|
| advws | 2053.31 | 2117.37 | 295.69 | 1.618e+09 | 2.297e+09 | 81,703.10 | 1.3258 | 0.2569% | 1.9362% |
| classic | 1081.25 | 1970.36 | 156.52 | 1.562e+09 | 2.273e+09 | 43,298.10 | 1.3462 | 0.2508% | 1.0828% |
| coro | 3713.21 | 2951.74 | 246.90 | 2.145e+09 | 2.916e+09 | 310,104.09 | 1.2123 | 0.3893% | 2.8092% |
| elastic | 2047.62 | 2098.96 | 292.92 | 1.629e+09 | 2.313e+09 | 66,918.94 | 1.3247 | 0.2571% | 1.5874% |
| ws | 1074.25 | 1980.67 | 182.32 | 1.577e+09 | 2.292e+09 | 57,804.94 | 1.3390 | 0.2515% | 1.4329% |


## Coro vs Classic (Perf Interpretation)
Overall (`coro / classic`):

- Throughput: `3.434x`
- Context switches per 1k requests: `1.498x`
- CPU migrations per 1k requests: `1.577x`
- Cycles per 1k requests: `1.373x`
- Instructions per 1k requests: `1.283x`
- Cache misses per 1k requests: `7.162x`
- IPC: `0.901x`
- Branch miss rate: `1.553x`
- Cache miss rate: `2.595x`

Interpretation:
- `coro` gains major throughput/latency, but does more scheduler and memory-system work per request.
- Efficiency per request (especially cache behavior) is weaker in `coro`, but service performance is much better in this benchmark.


## Interesting Scenario Patterns
- Highest context-switch intensity (`ctx_switches_per_1k_req`):
  - `coro t16 p4_io_heavy_high_conc`: `5213.0`
  - `coro t16 p6_ultra_io_bound`: `5125.6`
  - `coro t16 p2_balanced`: `5079.8`
- Lowest context-switch intensity:
  - `classic t8 p4_io_heavy_high_conc`: `1659.5`
  - `classic t16 p2_balanced`: `1661.9`
  - `ws t8 p4_io_heavy_high_conc`: `1688.9`
- Lowest migration intensity:
  - `classic t2 p3_cpu_heavy`: `0.5`
  - `ws t1 p3_cpu_heavy`: `0.5`
  - `ws t2 p3_cpu_heavy`: `0.6`
- Highest migration intensity:
  - `ws t2 p4_io_heavy_high_conc`: `722.6`
  - `advws t1 p1_light_cpu_heavy_io`: `655.3`
  - `advws t1 p2_balanced`: `654.9`


## Coro vs Classic by Preset (Perf View)
Geometric mean across threads:

| preset | rps_speedup | ctx_per_1k_req_ratio | migrations_per_1k_req_ratio | ipc_ratio | cache_miss_rate_ratio |
|---|---|---|---|---|---|
| p1_light_cpu_heavy_io | 3.898x | 1.540x | 4.225x | 0.860x | 4.357x |
| p2_balanced | 5.969x | 1.605x | 4.353x | 0.893x | 3.321x |
| p3_cpu_heavy | 1.087x | 1.010x | 3.085x | 0.985x | 3.450x |
| p4_io_heavy_high_conc | 14.079x | 1.670x | 4.457x | 0.844x | 2.755x |
| p5_stress_mix | 3.356x | 1.045x | 0.692x | 0.958x | 2.214x |
| p6_ultra_io_bound | 15.735x | 1.817x | 8.008x | 0.815x | 3.243x |


## Non-Coro Comparison (4 Thread-Pool Implementations Only)
- Winner counts: `advws 17`, `elastic 13` (out of 30 scenarios).
- Avg normalized throughput:
  - `advws 0.9997`
  - `elastic 0.9988`
  - `classic 0.5195`
  - `ws 0.5160`
- Geometric mean throughput vs classic:
  - `advws 1.9432x`
  - `elastic 1.9415x`
  - `ws 0.9929x`

Conclusion for the four pool variants: there are material throughput gaps in this dataset.


## Bottom Line
1. `coro` is the dominant mode for this mixed HTTP workload dataset, with large gains in throughput and latency, especially on I/O-heavy presets.
2. Among the four thread-pool implementations (excluding `coro`), `advws`/`elastic` materially outperform `classic`/`ws` in this run.
3. `coro` trades per-request CPU/memory efficiency for much better service-level throughput/latency.
