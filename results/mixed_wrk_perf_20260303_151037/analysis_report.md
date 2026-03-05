# Mixed HTTP Workload Analysis

Source files:
- `results/mixed_wrk_perf_20260303_151037/summary.csv`
- `results/mixed_wrk_perf_20260303_151037/runs.csv`

Dataset:
- Summary rows: 150 (`5 threads x 6 presets x 5 modes`)
- Run rows: 450 (`3 reps` per summary row)
- Modes: `classic`, `ws`, `elastic`, `advws`, `coro`
- Threads tested: `1, 2, 4, 8, 16`


## Data Quality

- All runs succeeded (`exit_code=0` for all 450 runs).
- No socket errors were recorded.
- `successful_runs=3` for every summary row.


## Scenario Winners (Throughput)

Winner criterion: highest `avg_requests_sec` per `(threads, preset)` scenario.

| mode | wins |
|---|---:|
| coro | 29 |
| elastic | 1 |

The single non-`coro` win is `elastic` at `threads=16`, preset `p1_light_cpu_heavy_io` (very small margin over the other non-coro modes).


## Winner Counts by Thread

| thread | scenario_count | winner_breakdown |
|---|---:|---|
| t1 | 6 | coro:6 |
| t2 | 6 | coro:6 |
| t4 | 6 | coro:6 |
| t8 | 6 | coro:6 |
| t16 | 6 | coro:5, elastic:1 |


## Avg Normalized Throughput by Mode

Normalization: `avg_requests_sec / best_requests_sec_in_same_scenario` (higher is better, `1.0` is best).

| mode | avg_norm_throughput |
|---|---:|
| coro | 0.9997 |
| classic | 0.3384 |
| ws | 0.3383 |
| elastic | 0.3383 |
| advws | 0.3382 |


## Geometric Mean vs Classic

Across all `(threads, preset)` scenarios:

| mode | throughput_speedup_vs_classic | latency_ratio_vs_classic |
|---|---:|---:|
| coro | 5.157x | 0.245x |
| ws | 1.000x | 1.003x |
| elastic | 0.999x | 1.000x |
| advws | 0.999x | 1.003x |

Interpretation:
- `coro` delivers about `5.16x` throughput and about `75.5%` lower average latency than `classic`.
- `ws`, `elastic`, and `advws` are effectively tied with `classic` for this benchmark setup.


## Coro vs Classic by Preset

Geometric mean across threads (`1,2,4,8,16`):

| preset | coro_throughput_speedup | coro_latency_ratio |
|---|---:|---:|
| p1_light_cpu_heavy_io | 3.893x | 0.257x |
| p2_balanced | 5.970x | 0.167x |
| p3_cpu_heavy | 1.088x | 0.921x |
| p4_io_heavy_high_conc | 14.080x | 0.071x |
| p5_stress_mix | 3.356x | 0.298x |
| p6_ultra_io_bound | 15.748x | 0.257x |

Key pattern: `coro` advantage is strongest on I/O-heavy presets (`p4`, `p6`) and smallest on CPU-heavy preset (`p3`).


## Scaling Behavior (t1 to t16)

Throughput ratio `RPS(t16)/RPS(t1)`:

- `classic`: ~`16x` on every preset (near-linear scaling).
- `coro`: mixed scaling
  - `~1x` on `p1` and `p6` (already saturated at low thread counts)
  - `~1.6x` on `p4`
  - `~2.6x` on `p2`
  - `~15x` on `p3` and `p5`

This suggests `coro` reaches a non-server bottleneck quickly on some I/O-heavy presets (likely load-generator or connection-level saturation), while still scaling strongly when CPU work is substantial.


## Run-to-Run Stability

Metric: coefficient of variation (CV) of `requests_sec` across the 3 repetitions per scenario.

| mode | avg_cv_requests_sec |
|---|---:|
| classic | 0.000754 |
| ws | 0.000758 |
| coro | 0.000968 |
| advws | 0.001018 |
| elastic | 0.001042 |

All modes are very stable (`~0.08% to 0.10%` CV on average).


## Additional Perf Metrics from `runs.csv`

The `runs.csv` file includes Linux perf counters (`perf_*` columns).  
Raw counters rise with throughput, so the most useful comparison is normalized work: counters per 1,000 requests.


## Mode-Level Perf Summary

Average across all runs:

| mode | avg_rps | ctx_switches_per_1k_req | cpu_migrations_per_1k_req | cycles_per_1k_req | instructions_per_1k_req | cache_misses_per_1k_req | IPC | branch_miss_rate | cache_miss_rate |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| classic | 1080.94 | 1956.65 | 165.84 | 1.578e9 | 2.295e9 | 42,822.92 | 1.3455 | 0.2508% | 1.0531% |
| ws | 1080.64 | 1969.17 | 169.30 | 1.567e9 | 2.276e9 | 54,516.31 | 1.3399 | 0.2531% | 1.3236% |
| elastic | 1080.63 | 1984.39 | 141.44 | 1.559e9 | 2.268e9 | 40,467.11 | 1.3433 | 0.2573% | 0.9910% |
| advws | 1080.02 | 1977.27 | 130.15 | 1.522e9 | 2.212e9 | 49,854.03 | 1.3413 | 0.2605% | 1.2069% |
| coro | 3714.50 | 2956.79 | 248.13 | 2.141e9 | 2.913e9 | 298,476.42 | 1.2149 | 0.3900% | 2.6537% |

Notes:
- Among non-coro pools, these normalized efficiency metrics are close.
- `elastic` has the lowest cache misses per request on average among non-coro modes.
- `advws` has the lowest CPU migrations per request among non-coro modes.


## Coro vs Classic (Perf Interpretation)

Overall (`coro / classic`):

- Throughput: `3.436x`
- Context switches (raw): `5.665x`
- CPU migrations (raw): `4.534x`
- Cycles (raw): `2.666x`
- Instructions (raw): `2.362x`
- Cache misses (raw): `20.456x`
- Context switches per 1k requests: `1.511x`
- CPU migrations per 1k requests: `1.496x`
- Cycles per 1k requests: `1.357x`
- Instructions per 1k requests: `1.269x`
- Cache misses per 1k requests: `6.970x`
- IPC: `0.903x`
- Branch miss rate: `1.555x`
- Cache miss rate: `2.520x`

Interpretation:
- `coro` gains major throughput/latency, but does more scheduler and memory-system work per request.
- Efficiency per request (especially cache behavior) is weaker in `coro`, but wall-clock service performance is still much better in this benchmark.


## Interesting Scenario Patterns

- Highest context-switch intensity (`ctx_switches_per_1k_req`) appears in `coro` at high-thread, I/O-heavy settings:
  - `coro t16 p4_io_heavy_high_conc`: `5208`
  - `coro t16 p6_ultra_io_bound`: `5152`
  - `coro t16 p2_balanced`: `5092`
- Lowest context-switch intensity is around `~1646-1727` and appears in non-coro modes on several `t8` scenarios.
- Lowest migration intensity appears in CPU-heavy `t2` scenarios (`p3_cpu_heavy`) for non-coro modes (near `0.5` migrations per 1k req).
- Highest migration intensity includes:
  - `coro t4 p3_cpu_heavy`: `528.8` per 1k req
  - `coro t16 p6_ultra_io_bound`: `515.2` per 1k req
  - `coro t16 p4_io_heavy_high_conc`: `494.9` per 1k req


## Coro vs Classic by Preset (Perf View)

Geometric mean across threads:

| preset | rps_speedup | ctx_per_1k_req_ratio | migrations_per_1k_req_ratio | ipc_ratio | cache_miss_rate_ratio |
|---|---:|---:|---:|---:|---:|
| p1_light_cpu_heavy_io | 3.893x | 1.553x | 1.817x | 0.857x | 2.912x |
| p2_balanced | 5.970x | 1.609x | 6.454x | 0.896x | 4.117x |
| p3_cpu_heavy | 1.088x | 0.989x | 3.845x | 0.986x | 2.589x |
| p4_io_heavy_high_conc | 14.080x | 1.739x | 3.649x | 0.850x | 2.851x |
| p5_stress_mix | 3.356x | 1.047x | 0.596x | 0.957x | 1.945x |
| p6_ultra_io_bound | 15.748x | 1.825x | 8.385x | 0.823x | 3.631x |

Pattern:
- Largest throughput wins for `coro` (`p4`, `p6`) coincide with higher scheduling activity and higher cache miss pressure per request.


## Non-Coro Comparison (4 Thread-Pool Implementations Only)

To isolate the four pool implementations (`classic`, `ws`, `elastic`, `advws`), excluding `coro`:

- Winner counts: `classic 13`, `elastic 8`, `ws 5`, `advws 4` (out of 30 scenarios).
- Avg normalized throughput:
  - `classic 0.9996`
  - `ws 0.9992`
  - `elastic 0.9989`
  - `advws 0.9987`
- Geometric mean throughput vs classic:
  - `ws 0.9996x`
  - `elastic 0.9993x`
  - `advws 0.9991x`

Conclusion for the four pool variants in this HTTP mixed benchmark: performance differences are negligible (roughly within ~0.1%).


## Bottom Line

1. `coro` is the dominant mode for this mixed HTTP workload dataset, with large gains in both throughput and latency, especially on I/O-heavy presets.
2. Among the four thread-pool implementations (excluding `coro`), results are essentially tied for both performance and stability; perf-counter differences are small and mostly second-order.
3. `coro` trades per-request CPU/memory efficiency (more switches/migrations/cache misses) for much better service-level throughput/latency in this benchmark.
4. If your objective is maximizing HTTP mixed-workload performance in this setup, `coro` is the clear first choice.
