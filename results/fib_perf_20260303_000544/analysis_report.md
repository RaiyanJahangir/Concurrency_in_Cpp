# Fib Summary Analysis


Source: `results\fib_perf_20260303_000544\summary_no32.csv`

Rows: 225; valid rows (bench_best_s>0): 225; scenarios: 45


## Scenario Winners (best runtime count)

| mode | wins |
|---|---|
| coro | 11 |
| advws | 11 |
| classic | 10 |
| ws | 9 |
| elastic | 4 |


## Avg Normalized Runtime by Mode (1.0 = scenario best)

| mode | avg_norm_runtime |
|---|---|
| ws | 1.512 |
| advws | 1.527 |
| coro | 1.541 |
| classic | 1.666 |
| elastic | 1.914 |


## Geometric Mean Speedup vs Classic (>1 faster)

| mode | speedup_vs_classic |
|---|---|
| advws | 1.043x |
| coro | 1.042x |
| elastic | 0.862x |
| ws | 1.064x |


## Coroutine Speedup vs Classic by Benchmark Family

| family | coro_speedup_vs_classic | n_scenarios |
|---|---|---|
| fib_bench | 1.019x | 15 |
| fib_fast_bench | 1.152x | 15 |
| fib_single_bench | 0.964x | 15 |


## Coroutine Speedup vs Classic by Preset Level

| level | coro_speedup_vs_classic | n_scenarios |
|---|---|---|
| light | 1.040x | 15 |
| mid | 1.053x | 15 |
| heavy | 1.033x | 15 |


## Best Mode by Benchmark and Thread (avg over presets)

| bench | t1 | t2 | t4 | t8 | t16 |
|---|---|---|---|---|---|
| fib_bench | coro | advws | coro | ws | ws |
| fib_fast_bench | classic | coro | advws | advws | ws |
| fib_single_bench | coro | coro | advws | classic | classic |


## Winner Counts by Thread

| thread | scenario_count | winner_breakdown |
|---|---|---|
| t1 | 9 | coro:4, classic:4, advws:1 |
| t2 | 9 | advws:5, elastic:2, coro:2 |
| t4 | 9 | ws:3, classic:2, advws:2, coro:1, elastic:1 |
| t8 | 9 | coro:3, classic:2, ws:2, advws:2 |
| t16 | 9 | ws:4, classic:2, coro:1, elastic:1, advws:1 |
