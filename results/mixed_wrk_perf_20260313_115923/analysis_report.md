# Mixed HTTP Workload Analysis

Source files:
- `results/mixed_wrk_perf_20260313_115923/summary.csv`
- `results/mixed_wrk_perf_20260313_115923/runs.csv`

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
| elastic | 17 |
| advws | 12 |
| ws | 1 |

Non-`coro` winners occurred in `30` scenarios: elastic:17, advws:12, ws:1.


## Winner Counts by Thread

| thread | scenario_count | winner_breakdown |
|---|---|---|
| t1 | 6 | elastic:4, advws:2 |
| t2 | 6 | elastic:5, advws:1 |
| t4 | 6 | elastic:3, advws:3 |
| t8 | 6 | advws:3, elastic:3 |
| t16 | 6 | advws:3, elastic:2, ws:1 |


## Avg Normalized Throughput by Mode
Normalization: `avg_requests_sec / best_requests_sec_in_same_scenario` (higher is better, `1.0` is best).

| mode | avg_norm_throughput |
|---|---|
| elastic | 0.9982 |
| advws | 0.9964 |
| coro | 0.6540 |
| classic | 0.5227 |
| ws | 0.5219 |


## Geometric Mean vs Classic
Across all `(threads, preset)` scenarios:

| mode | throughput_speedup_vs_classic | latency_ratio_vs_classic |
|---|---|---|
| elastic | 1.928x | 1.676x |
| advws | 1.925x | 1.691x |
| coro | 1.228x | 1.296x |
| ws | 0.998x | 1.003x |

Interpretation:
- `coro` delivers about `1.23x` throughput and `29.6%` higher average latency than `classic`.
- Near parity with `classic`: `ws`.
- Faster than `classic`: `elastic` (1.93x), `advws` (1.92x).


## Coro vs Classic by Preset
Geometric mean across threads (`1,2,4,8,16`):

| preset | coro_throughput_speedup | coro_latency_ratio |
|---|---|---|
| p1_light_cpu_heavy_io | 1.257x | 0.799x |
| p2_balanced | 1.088x | 0.917x |
| p3_cpu_heavy | 0.972x | 1.022x |
| p4_io_heavy_high_conc | 1.404x | 2.876x |
| p5_stress_mix | 1.004x | 0.987x |
| p6_ultra_io_bound | 1.831x | 2.233x |

Key pattern: `coro` advantage is strongest on I/O-heavy presets and smallest on CPU-heavy preset (`p3`).


## Scaling Behavior (t1 to t16)
Throughput ratio `RPS(t16)/RPS(t1)`:
- `classic`: `~16.0x` average across presets
- `coro`: mixed scaling
  - `p1_light_cpu_heavy_io`: `~11.8x`
  - `p2_balanced`: `~17.2x`
  - `p3_cpu_heavy`: `~17.4x`
  - `p4_io_heavy_high_conc`: `~16.4x`
  - `p5_stress_mix`: `~18.5x`
  - `p6_ultra_io_bound`: `~16.0x`
- `ws`: `~16.0x` average across presets
- `elastic`: `~13.6x` average across presets
- `advws`: `~13.7x` average across presets


## Run-to-Run Stability
Metric: coefficient of variation (CV) of `requests_sec` across the 3 repetitions per scenario.

| mode | avg_cv_requests_sec |
|---|---|
| classic | 0.006126 |
| advws | 0.006269 |
| elastic | 0.006583 |
| ws | 0.006617 |
| coro | 0.008070 |

Observed mode-average CV ranges from `0.0061` to `0.0081` (lower is more stable).


## Additional Perf Metrics from `runs.csv`
The `runs.csv` file includes Linux perf counters (`perf_*` columns).
Raw counters rise with throughput, so the most useful comparison is normalized work: counters per 1,000 requests.


## Mode-Level Perf Summary

| mode | avg_rps | ctx_switches_per_1k_req | cpu_migrations_per_1k_req | cycles_per_1k_req | instructions_per_1k_req | cache_misses_per_1k_req | IPC | branch_miss_rate | cache_miss_rate |
|---|---|---|---|---|---|---|---|---|---|
| advws | 255.99 | 2312.84 | 851.42 | 1.376e+11 | 4.862e+11 | 505,184,760.81 | 3.5272 | 2.8638% | 3.4635% |
| classic | 140.35 | 2212.49 | 743.63 | 1.399e+11 | 4.998e+11 | 503,419,160.83 | 3.5682 | 2.8634% | 3.3581% |
| coro | 180.11 | 2257.35 | 748.05 | 1.418e+11 | 5.038e+11 | 503,051,636.46 | 3.5483 | 2.8667% | 3.4090% |
| elastic | 256.16 | 2277.59 | 837.83 | 1.373e+11 | 4.861e+11 | 505,815,265.07 | 3.5343 | 2.8636% | 3.4481% |
| ws | 140.17 | 2204.19 | 741.44 | 1.404e+11 | 5.000e+11 | 509,242,190.72 | 3.5611 | 2.8633% | 3.3958% |


## Coro vs Classic (Perf Interpretation)
Overall (`coro / classic`):

- Throughput: `1.283x`
- Context switches per 1k requests: `1.020x`
- CPU migrations per 1k requests: `1.006x`
- Cycles per 1k requests: `1.014x`
- Instructions per 1k requests: `1.008x`
- Cache misses per 1k requests: `0.999x`
- IPC: `0.994x`
- Branch miss rate: `1.001x`
- Cache miss rate: `1.015x`

Interpretation:
- Relative to `classic`, `coro` shows `1.283x` throughput, `1.020x` context switches per 1k requests, and `1.015x` cache miss rate.
- Per-request CPU and memory efficiency is roughly on par with `classic` in this dataset.


## Interesting Scenario Patterns
- Highest context-switch intensity (`ctx_switches_per_1k_req`):
  - `coro t16 p1_light_cpu_heavy_io`: `4223.1`
  - `classic t16 p1_light_cpu_heavy_io`: `3369.1`
  - `ws t16 p1_light_cpu_heavy_io`: `3359.0`
- Lowest context-switch intensity:
  - `coro t8 p1_light_cpu_heavy_io`: `2032.2`
  - `elastic t2 p6_ultra_io_bound`: `2052.1`
  - `elastic t16 p4_io_heavy_high_conc`: `2054.8`
- Lowest migration intensity:
  - `coro t1 p4_io_heavy_high_conc`: `25.4`
  - `coro t1 p1_light_cpu_heavy_io`: `30.3`
  - `coro t1 p6_ultra_io_bound`: `30.6`
- Highest migration intensity:
  - `ws t2 p3_cpu_heavy`: `1044.2`
  - `elastic t1 p3_cpu_heavy`: `1023.7`
  - `advws t1 p3_cpu_heavy`: `1014.5`


## Coro vs Classic by Preset (Perf View)
Geometric mean across threads:

| preset | rps_speedup | ctx_per_1k_req_ratio | migrations_per_1k_req_ratio | ipc_ratio | cache_miss_rate_ratio |
|---|---|---|---|---|---|
| p1_light_cpu_heavy_io | 1.257x | 1.037x | 1.023x | 0.995x | 1.013x |
| p2_balanced | 1.088x | 1.009x | 0.931x | 0.999x | 0.983x |
| p3_cpu_heavy | 0.972x | 1.021x | 0.889x | 0.995x | 1.033x |
| p4_io_heavy_high_conc | 1.404x | 0.992x | 0.771x | 0.991x | 1.035x |
| p5_stress_mix | 1.004x | 1.039x | 0.993x | 0.990x | 1.025x |
| p6_ultra_io_bound | 1.831x | 0.990x | 0.830x | 0.996x | 1.006x |


## Non-Coro Comparison (4 Thread-Pool Implementations Only)
- Winner counts: `elastic 17`, `advws 12`, `ws 1` (out of 30 scenarios).
- Avg normalized throughput:
  - `elastic 0.9982`
  - `advws 0.9964`
  - `classic 0.5227`
  - `ws 0.5219`
- Geometric mean throughput vs classic:
  - `advws 1.9250x`
  - `elastic 1.9284x`
  - `ws 0.9984x`

Conclusion for the four pool variants: there are material throughput gaps in this dataset.


## Bottom Line
1. `elastic` is the strongest overall mode in this dataset by average normalized throughput (`0.9982`).
2. `coro` wins `0` of `30` scenarios, with `1.228x` geometric-mean throughput vs `classic` and `29.6%` higher average latency.
3. Among the four thread-pool implementations (excluding `coro`), `advws`/`elastic` materially outperform `classic`/`ws` in this run.
4. `coro` achieves its gains with per-request perf characteristics that stay close to `classic`.
