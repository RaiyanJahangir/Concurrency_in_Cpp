# Fib Summary Analysis


Source: `results\fib_perf_20260303_000544\summary.csv`

Rows: 270; valid rows (bench_best_s>0): 269; scenarios: 54


## Invalid/zero timing rows

| bench | preset | mode | threads | bench_best_s |
|---|---|---|---|---|
| fib_bench | fb_heavy | ws | 32 |  |


## Scenario Winners (best runtime count)

| mode | wins |
|---|---|
| classic | 14 |
| coro | 13 |
| advws | 12 |
| ws | 11 |
| elastic | 4 |


## Avg Normalized Runtime by Mode (1.0 = scenario best)

| mode | avg_norm_runtime |
|---|---|
| ws | 1.494 |
| coro | 1.542 |
| advws | 1.557 |
| classic | 1.617 |
| elastic | 1.850 |


## Geometric Mean Speedup vs Classic (>1 faster)

| mode | speedup_vs_classic |
|---|---|
| advws | 1.002x |
| coro | 1.012x |
| elastic | 0.861x |
| ws | 1.048x |


## Coroutine Speedup vs Classic by Benchmark Family

| family | coro_speedup_vs_classic | n_scenarios |
|---|---|---|
| fib_bench | 1.041x | 18 |
| fib_fast_bench | 1.101x | 18 |
| fib_single_bench | 0.905x | 18 |


## Coroutine Speedup vs Classic by Preset Level

| level | coro_speedup_vs_classic | n_scenarios |
|---|---|---|
| light | 0.971x | 18 |
| mid | 1.076x | 18 |
| heavy | 0.993x | 18 |


## Best Mode by Benchmark and Thread (avg over presets)

| bench | t1 | t2 | t4 | t8 | t16 | t32 |
|---|---|---|---|---|---|---|
| fib_bench | coro | advws | coro | ws | ws | ws |
| fib_fast_bench | classic | coro | advws | advws | ws | classic |
| fib_single_bench | coro | coro | advws | classic | classic | classic |


## Winner Counts by Thread

| thread | scenario_count | winner_breakdown |
|---|---|---|
| t1 | 9 | coro:4, classic:4, advws:1 |
| t2 | 9 | advws:5, elastic:2, coro:2 |
| t4 | 9 | ws:3, classic:2, advws:2, coro:1, elastic:1 |
| t8 | 9 | coro:3, classic:2, ws:2, advws:2 |
| t16 | 9 | ws:4, classic:2, coro:1, elastic:1, advws:1 |
| t32 | 9 | classic:4, ws:2, coro:2, advws:1 |
