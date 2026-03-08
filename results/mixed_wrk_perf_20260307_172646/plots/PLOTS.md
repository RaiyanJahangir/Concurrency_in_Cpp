# Mixed Workload Plots

- `01_throughput_vs_threads_by_preset.png`: Throughput scaling for each preset.
- `02_latency_vs_threads_by_preset.png`: Average latency scaling for each preset.
- `03_winner_heatmap.png`: Best-throughput mode by `(preset, threads)`.
- `04_speedup_vs_classic_by_thread.png`: Geometric mean throughput speedup vs `classic`.
- `05_perf_counters_per_kreq.png`: Normalized perf counters (scheduler + micro-arch).
- `06_latency_and_p99_vs_threads.png`: Average and p99 latency trends by thread count.
- `07_stability_cv_boxplot.png`: Run-to-run stability (CV of requests/sec).
- `08_throughput_vs_ctx_scatter.png`: Throughput vs scheduling overhead tradeoff.
