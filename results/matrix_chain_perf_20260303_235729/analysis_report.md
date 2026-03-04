# Matrix Chain Summary Analysis


Source: `/home/raiyan/projects/Concurrency_in_Cpp/results/matrix_chain_perf_20260303_235729/summary.csv`

Rows: 15; valid rows (bench_best_s>0): 0; scenarios: 3


## Invalid/zero timing rows

| bench | preset | mode | threads | bench_best_s |
|---|---|---|---|---|
| matrix_chain_bench | mc_light | classic | 1 |  |
| matrix_chain_bench | mc_mid | classic | 1 |  |
| matrix_chain_bench | mc_heavy | classic | 1 |  |
| matrix_chain_bench | mc_light | coro | 1 |  |
| matrix_chain_bench | mc_mid | coro | 1 |  |
| matrix_chain_bench | mc_heavy | coro | 1 |  |
| matrix_chain_bench | mc_light | ws | 1 |  |
| matrix_chain_bench | mc_mid | ws | 1 |  |
| matrix_chain_bench | mc_heavy | ws | 1 |  |
| matrix_chain_bench | mc_light | elastic | 1 |  |
| matrix_chain_bench | mc_mid | elastic | 1 |  |
| matrix_chain_bench | mc_heavy | elastic | 1 |  |
| matrix_chain_bench | mc_light | advws | 1 |  |
| matrix_chain_bench | mc_mid | advws | 1 |  |
| matrix_chain_bench | mc_heavy | advws | 1 |  |


## Scenario Winners (best runtime count)

| mode | wins |
|---|---|


## Avg Normalized Runtime by Mode (1.0 = scenario best)

| mode | avg_norm_runtime |
|---|---|


## Geometric Mean Speedup vs Classic (>1 faster)

| mode | speedup_vs_classic |
|---|---|
| advws | NA |
| coro | NA |
| elastic | NA |
| ws | NA |


## Coroutine Speedup vs Classic by Preset Level

| level | coro_speedup_vs_classic | n_scenarios |
|---|---|---|
| light | NA | 0 |
| mid | NA | 0 |
| heavy | NA | 0 |


## GFLOP/s (best-time derived)

| mode | avg_gflops | best_gflops | n_rows |
|---|---|---|---|


## Winner Counts by Thread

| thread | scenario_count | winner_breakdown |
|---|---|---|
| t1 | 0 |  |
