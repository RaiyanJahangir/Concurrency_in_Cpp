# Matrix Chain Summary Analysis


Source: `/home/raiyan/projects/Concurrency_in_Cpp/results/matrix_chain_perf_20260304_015059/summary.csv`

Rows: 75; valid rows (bench_best_s>0): 75; scenarios: 15


## Scenario Winners (best runtime count)

| mode | wins |
|---|---|
| advws | 11 |
| elastic | 4 |


## Avg Normalized Runtime by Mode (1.0 = scenario best)

| mode | avg_norm_runtime |
|---|---|
| advws | 1.010 |
| elastic | 1.092 |
| ws | 1.678 |
| coro | 1.816 |
| classic | 1.828 |


## Geometric Mean Speedup vs Classic (>1 faster)

| mode | speedup_vs_classic |
|---|---|
| advws | 1.771x |
| coro | 1.009x |
| elastic | 1.643x |
| ws | 1.088x |


## Coroutine Speedup vs Classic by Preset Level

| level | coro_speedup_vs_classic | n_scenarios |
|---|---|---|
| light | 1.004x | 5 |
| mid | 1.032x | 5 |
| heavy | 0.993x | 5 |


## GFLOP/s (best-time derived)

| mode | avg_gflops | best_gflops | n_rows |
|---|---|---|---|
| advws | 49.896 | 105.435 | 15 |
| classic | 29.861 | 87.930 | 15 |
| coro | 30.494 | 86.537 | 15 |
| elastic | 45.939 | 95.594 | 15 |
| ws | 32.529 | 91.754 | 15 |


## Winner Counts by Thread

| thread | scenario_count | winner_breakdown |
|---|---|---|
| t1 | 3 | advws:2, elastic:1 |
| t2 | 3 | elastic:2, advws:1 |
| t4 | 3 | advws:2, elastic:1 |
| t8 | 3 | advws:3 |
| t16 | 3 | advws:3 |
