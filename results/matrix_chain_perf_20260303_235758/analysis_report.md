# Matrix Chain Summary Analysis


Source: `/home/raiyan/projects/Concurrency_in_Cpp/results/matrix_chain_perf_20260303_235758/summary.csv`

Rows: 15; valid rows (bench_best_s>0): 15; scenarios: 3


## Scenario Winners (best runtime count)

| mode | wins |
|---|---|
| advws | 2 |
| elastic | 1 |


## Avg Normalized Runtime by Mode (1.0 = scenario best)

| mode | avg_norm_runtime |
|---|---|
| advws | 1.018 |
| elastic | 1.356 |
| coro | 1.454 |
| classic | 1.458 |
| ws | 1.478 |


## Geometric Mean Speedup vs Classic (>1 faster)

| mode | speedup_vs_classic |
|---|---|
| advws | 1.431x |
| coro | 1.002x |
| elastic | 1.095x |
| ws | 0.987x |


## Coroutine Speedup vs Classic by Preset Level

| level | coro_speedup_vs_classic | n_scenarios |
|---|---|---|
| light | 0.969x | 1 |
| mid | 1.045x | 1 |
| heavy | 0.994x | 1 |


## GFLOP/s (best-time derived)

| mode | avg_gflops | best_gflops | n_rows |
|---|---|---|---|
| advws | 20.442 | 22.293 | 3 |
| classic | 14.243 | 14.421 | 3 |
| coro | 14.287 | 14.999 | 3 |
| elastic | 15.795 | 19.415 | 3 |
| ws | 14.060 | 14.280 | 3 |


## Winner Counts by Thread

| thread | scenario_count | winner_breakdown |
|---|---|---|
| t1 | 3 | advws:2, elastic:1 |
