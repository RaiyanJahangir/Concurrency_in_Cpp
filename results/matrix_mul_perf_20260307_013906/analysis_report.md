# Matrix Multiplication Summary Analysis

Source: `/home/raiyan/projects/Concurrency_in_Cpp/results/matrix_mul_perf_20260307_013906/summary.csv`

Rows: 75; valid rows (exit_code=0 and best_s>0): 75; scenarios: 15

## Scenario Winners (best runtime count)

| mode | wins |
|---|---|
| advws | 12 |
| elastic | 3 |

## Avg Normalized Runtime by Mode (1.0 = scenario best)

| mode | avg_norm_runtime |
|---|---|
| advws | 1.004 |
| elastic | 1.091 |
| ws | 1.829 |
| coro | 1.900 |
| classic | 1.934 |

## Geometric Mean Speedup vs Classic (>1 faster)

| mode | speedup_vs_classic |
|---|---|
| advws | 1.896x |
| coro | 1.018x |
| elastic | 1.749x |
| ws | 1.065x |

## Coroutine Speedup vs Classic by Preset Level

| level | coro_speedup_vs_classic | n_scenarios |
|---|---|---|
| heavy | 1.001x | 5 |
| light | 1.024x | 5 |
| mid | 1.028x | 5 |

## Best Mode by Matrix Size and Thread

| n | t1 | t2 | t4 | t8 | t16 |
|---|---|---|---|---|---|
| 1024 | elastic | advws | advws | advws | advws |
| 2048 | elastic | advws | advws | advws | advws |
| 4096 | elastic | advws | advws | advws | advws |

## Fastest Overall Row by Matrix Size

| n | mode | threads | best_s |
|---|---|---|---|
| 1024 | advws | t16 | 0.021838 |
| 2048 | advws | t16 | 0.202186 |
| 4096 | advws | t16 | 1.578500 |

## Approx GFLOP/s from Best Runtime

| mode | avg_gflops | best_gflops | n_rows |
|---|---|---|---|
| advws | 48.494 | 98.336 | 15 |
| classic | 26.929 | 64.653 | 15 |
| coro | 27.488 | 67.345 | 15 |
| elastic | 44.082 | 94.518 | 15 |
| ws | 30.721 | 81.422 | 15 |

## Stability Ratio (avg/best; closer to 1.0 is more stable)

| mode | avg_stability_ratio |
|---|---|
| advws | 1.018 |
| classic | 1.005 |
| coro | 1.007 |
| elastic | 1.006 |
| ws | 1.009 |

## Winner Counts by Thread

| thread | scenario_count | winner_breakdown |
|---|---|---|
| t1 | 3 | elastic:3 |
| t2 | 3 | advws:3 |
| t4 | 3 | advws:3 |
| t8 | 3 | advws:3 |
| t16 | 3 | advws:3 |
