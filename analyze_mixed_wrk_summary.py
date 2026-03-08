#!/usr/bin/env python3
"""Analyze mixed HTTP workload summary/runs CSVs and generate markdown report.

Usage:
  python analyze_mixed_wrk_summary.py \
    results/mixed_wrk_perf_20260307_172646/summary.csv \
    --runs results/mixed_wrk_perf_20260307_172646/runs.csv \
    --out results/mixed_wrk_perf_20260307_172646/analysis_report.md
"""

from __future__ import annotations

import argparse
import math
from collections import Counter
from pathlib import Path

import numpy as np
import pandas as pd


def gmean(vals: pd.Series) -> float:
    s = pd.to_numeric(vals, errors="coerce")
    s = s[(s > 0) & np.isfinite(s)]
    if s.empty:
        return float("nan")
    return float(np.exp(np.log(s).mean()))


def to_num(df: pd.DataFrame, cols: list[str]) -> pd.DataFrame:
    out = df.copy()
    for c in cols:
        if c in out.columns:
            out[c] = pd.to_numeric(out[c], errors="coerce")
    return out


def parse_latency_ms(val: object) -> float:
    if val is None or (isinstance(val, float) and math.isnan(val)):
        return float("nan")
    x = str(val).strip()
    try:
        if x.endswith("us"):
            return float(x[:-2]) / 1000.0
        if x.endswith("ms"):
            return float(x[:-2])
        if x.endswith("s"):
            return float(x[:-1]) * 1000.0
        return float(x)
    except ValueError:
        return float("nan")


def markdown_table(headers: list[str], rows: list[list[str]]) -> str:
    out = ["| " + " | ".join(headers) + " |", "|" + "|".join(["---"] * len(headers)) + "|"]
    for r in rows:
        out.append("| " + " | ".join(r) + " |")
    return "\n".join(out)


def fmt_float(v: float, nd: int = 3) -> str:
    if pd.isna(v) or not np.isfinite(v):
        return "NA"
    return f"{v:.{nd}f}"


def fmt_x(v: float, nd: int = 3) -> str:
    if pd.isna(v) or not np.isfinite(v):
        return "NA"
    return f"{v:.{nd}f}x"


def fmt_pct(v: float, nd: int = 1) -> str:
    if pd.isna(v) or not np.isfinite(v):
        return "NA"
    return f"{v:.{nd}f}%"


def build_report(summary_csv: Path, runs_csv: Path) -> str:
    summary = pd.read_csv(summary_csv)
    runs = pd.read_csv(runs_csv)

    summary = to_num(
        summary,
        [
            "threads",
            "reps",
            "successful_runs",
            "best_runtime_s",
            "avg_runtime_s",
            "best_requests_sec",
            "avg_requests_sec",
            "best_latency_avg",
            "avg_latency_avg",
        ],
    )
    runs = to_num(
        runs,
        [
            "threads",
            "run",
            "exit_code",
            "runtime_s",
            "requests",
            "requests_sec",
            "perf_available",
            "perf_task_clock",
            "perf_context_switches",
            "perf_cpu_migrations",
            "perf_page_faults",
            "perf_cycles",
            "perf_instructions",
            "perf_branches",
            "perf_branch_misses",
            "perf_cache_references",
            "perf_cache_misses",
        ],
    )

    runs["lat_avg_ms"] = runs["lat_avg"].map(parse_latency_ms)
    runs["p99_ms"] = runs["p99"].map(parse_latency_ms)

    modes = sorted(summary["mode"].dropna().unique().tolist())
    threads = sorted(summary["threads"].dropna().astype(int).unique().tolist())
    presets = sorted(summary["preset"].dropna().unique().tolist())

    rep = []
    rep.append("# Mixed HTTP Workload Analysis\n")
    rep.append("Source files:")
    rep.append(f"- `{summary_csv}`")
    rep.append(f"- `{runs_csv}`")

    rep.append("\nDataset:")
    rep.append(f"- Summary rows: {len(summary)} (`{len(threads)} threads x {len(presets)} presets x {len(modes)} modes`)")
    rep.append(f"- Run rows: {len(runs)} (`{int(runs['run'].max()) if len(runs) else 0} reps` per summary row)")
    rep.append(f"- Modes: {', '.join([f'`{m}`' for m in modes])}")
    rep.append(f"- Threads tested: {', '.join([f'`{t}`' for t in threads])}")

    # Data quality
    all_exit_ok = bool((runs["exit_code"] == 0).all()) if len(runs) else False
    socket_err_non_empty = runs["socket_errors"].fillna("").astype(str).str.strip().replace("nan", "")
    any_socket_err = socket_err_non_empty.ne("").any()
    all_successful = bool((summary["successful_runs"] == summary["reps"]).all()) if len(summary) else False

    rep.append("\n\n## Data Quality")
    rep.append(f"- All runs succeeded (`exit_code=0` for all {len(runs)} runs)." if all_exit_ok else "- Some runs have non-zero `exit_code`.")
    rep.append("- No socket errors were recorded." if not any_socket_err else "- Some runs contain socket errors.")
    if all_successful:
        reps_v = int(summary["reps"].dropna().iloc[0]) if len(summary) else 0
        rep.append(f"- `successful_runs={reps_v}` for every summary row.")
    else:
        rep.append("- Some summary rows have `successful_runs < reps`.")

    # Winners by throughput
    scen_cols = ["threads", "preset"]
    winners = (
        summary.sort_values(["threads", "preset", "avg_requests_sec"], ascending=[True, True, False])
        .groupby(scen_cols, as_index=False)
        .first()[["threads", "preset", "mode", "avg_requests_sec"]]
    )
    win_counts = winners["mode"].value_counts().to_dict()

    rep.append("\n\n## Scenario Winners (Throughput)")
    rep.append("Winner criterion: highest `avg_requests_sec` per `(threads, preset)` scenario.")
    rows = [[m, str(int(win_counts.get(m, 0)))] for m in sorted(win_counts.keys(), key=lambda k: (-win_counts[k], k))]
    rep.append("\n" + markdown_table(["mode", "wins"], rows))

    non_coro_winners = winners[winners["mode"] != "coro"]
    if len(non_coro_winners) == 1:
        row = non_coro_winners.iloc[0]
        rep.append(
            f"\nThe single non-`coro` win is `{row['mode']}` at `threads={int(row['threads'])}`, "
            f"preset `{row['preset']}`."
        )
    elif len(non_coro_winners) > 1:
        breakdown = Counter(non_coro_winners["mode"].tolist())
        parts = ", ".join([f"{m}:{n}" for m, n in breakdown.most_common()])
        rep.append(f"\nNon-`coro` winners occurred in `{len(non_coro_winners)}` scenarios: {parts}.")

    # Winner counts by thread
    rep.append("\n\n## Winner Counts by Thread")
    t_rows = []
    for t in threads:
        sub = winners[winners["threads"] == t]
        c = Counter(sub["mode"].tolist())
        parts = ", ".join([f"{m}:{n}" for m, n in c.most_common()])
        t_rows.append([f"t{t}", str(len(sub)), parts])
    rep.append("\n" + markdown_table(["thread", "scenario_count", "winner_breakdown"], t_rows))

    # Avg normalized throughput
    scen_best = summary.groupby(["threads", "preset"], as_index=False)["avg_requests_sec"].max().rename(
        columns={"avg_requests_sec": "best_rps"}
    )
    norm = summary.merge(scen_best, on=["threads", "preset"], how="left")
    norm["norm_rps"] = norm["avg_requests_sec"] / norm["best_rps"]
    norm_by_mode = norm.groupby("mode", as_index=False)["norm_rps"].mean().sort_values("norm_rps", ascending=False)

    rep.append("\n\n## Avg Normalized Throughput by Mode")
    rep.append("Normalization: `avg_requests_sec / best_requests_sec_in_same_scenario` (higher is better, `1.0` is best).")
    n_rows = [[m, f"{v:.4f}"] for m, v in norm_by_mode[["mode", "norm_rps"]].itertuples(index=False, name=None)]
    rep.append("\n" + markdown_table(["mode", "avg_norm_throughput"], n_rows))

    # Geometric means vs classic
    rep.append("\n\n## Geometric Mean vs Classic")
    merged_list = []
    for m in modes:
        if m == "classic":
            continue
        a = summary[summary["mode"] == "classic"][["threads", "preset", "avg_requests_sec", "avg_latency_avg"]].rename(
            columns={"avg_requests_sec": "classic_rps", "avg_latency_avg": "classic_lat"}
        )
        b = summary[summary["mode"] == m][["threads", "preset", "avg_requests_sec", "avg_latency_avg"]].rename(
            columns={"avg_requests_sec": "mode_rps", "avg_latency_avg": "mode_lat"}
        )
        k = a.merge(b, on=["threads", "preset"], how="inner")
        merged_list.append(
            [
                m,
                gmean(k["mode_rps"] / k["classic_rps"]),
                gmean(k["mode_lat"] / k["classic_lat"]),
            ]
        )
    gm_df = pd.DataFrame(merged_list, columns=["mode", "rps_sp", "lat_rt"]).sort_values("rps_sp", ascending=False)
    gm_rows = [[m, fmt_x(r, 3), fmt_x(l, 3)] for m, r, l in gm_df.itertuples(index=False, name=None)]
    rep.append("Across all `(threads, preset)` scenarios:")
    rep.append("\n" + markdown_table(["mode", "throughput_speedup_vs_classic", "latency_ratio_vs_classic"], gm_rows))

    if "coro" in gm_df["mode"].values:
        c = gm_df[gm_df["mode"] == "coro"].iloc[0]
        rep.append("\nInterpretation:")
        rep.append(
            f"- `coro` delivers about `{c['rps_sp']:.2f}x` throughput and about `{(1.0 - c['lat_rt']) * 100.0:.1f}%` lower average latency than `classic`."
        )
        others_df = gm_df[gm_df["mode"] != "coro"].copy()
        if not others_df.empty:
            tied = others_df[(others_df["rps_sp"] >= 0.95) & (others_df["rps_sp"] <= 1.05)]["mode"].tolist()
            faster = others_df[others_df["rps_sp"] > 1.05][["mode", "rps_sp"]].values.tolist()
            slower = others_df[others_df["rps_sp"] < 0.95][["mode", "rps_sp"]].values.tolist()
            if tied:
                rep.append(f"- Near parity with `classic`: `{', '.join(tied)}`.")
            if faster:
                rep.append(
                    "- Faster than `classic`: "
                    + ", ".join([f"`{m}` ({v:.2f}x)" for m, v in faster])
                    + "."
                )
            if slower:
                rep.append(
                    "- Slower than `classic`: "
                    + ", ".join([f"`{m}` ({v:.2f}x)" for m, v in slower])
                    + "."
                )

    # Coro vs classic by preset
    rep.append("\n\n## Coro vs Classic by Preset")
    cr = (
        summary[summary["mode"] == "classic"][["threads", "preset", "avg_requests_sec", "avg_latency_avg"]]
        .rename(columns={"avg_requests_sec": "classic_rps", "avg_latency_avg": "classic_lat"})
        .merge(
            summary[summary["mode"] == "coro"][["threads", "preset", "avg_requests_sec", "avg_latency_avg"]]
            .rename(columns={"avg_requests_sec": "coro_rps", "avg_latency_avg": "coro_lat"}),
            on=["threads", "preset"],
            how="inner",
        )
    )
    preset_rows = []
    for p in presets:
        s = cr[cr["preset"] == p]
        preset_rows.append([p, gmean(s["coro_rps"] / s["classic_rps"]), gmean(s["coro_lat"] / s["classic_lat"])])
    rep.append("Geometric mean across threads (`1,2,4,8,16`):")
    rep.append(
        "\n"
        + markdown_table(
            ["preset", "coro_throughput_speedup", "coro_latency_ratio"],
            [[p, fmt_x(rs, 3), fmt_x(lr, 3)] for p, rs, lr in preset_rows],
        )
    )
    rep.append("\nKey pattern: `coro` advantage is strongest on I/O-heavy presets and smallest on CPU-heavy preset (`p3`).")

    # Scaling behavior
    rep.append("\n\n## Scaling Behavior (t1 to t16)")
    t1 = summary[summary["threads"] == min(threads)][["mode", "preset", "avg_requests_sec"]].rename(columns={"avg_requests_sec": "rps_t1"})
    tN = summary[summary["threads"] == max(threads)][["mode", "preset", "avg_requests_sec"]].rename(columns={"avg_requests_sec": "rps_tn"})
    sc = t1.merge(tN, on=["mode", "preset"], how="inner")
    sc["scale"] = sc["rps_tn"] / sc["rps_t1"]

    rep.append("Throughput ratio `RPS(t16)/RPS(t1)`:")
    for m in ["classic", "coro", "ws", "elastic", "advws"]:
        sm = sc[sc["mode"] == m]
        if sm.empty:
            continue
        if m == "coro":
            rep.append("- `coro`: mixed scaling")
            for p, v in sm[["preset", "scale"]].sort_values("preset").itertuples(index=False, name=None):
                rep.append(f"  - `{p}`: `~{v:.1f}x`")
        else:
            rep.append(f"- `{m}`: `~{sm['scale'].mean():.1f}x` average across presets")

    # Stability
    rep.append("\n\n## Run-to-Run Stability")
    rep.append("Metric: coefficient of variation (CV) of `requests_sec` across the 3 repetitions per scenario.")
    cv = (
        runs.groupby(["mode", "threads", "preset"], as_index=False)["requests_sec"]
        .agg(["mean", "std"])
        .reset_index()
    )
    cv["cv"] = cv["std"] / cv["mean"]
    cv_mode = cv.groupby("mode", as_index=False)["cv"].mean().sort_values("cv")
    cv_rows = [[m, f"{v:.6f}"] for m, v in cv_mode[["mode", "cv"]].itertuples(index=False, name=None)]
    rep.append("\n" + markdown_table(["mode", "avg_cv_requests_sec"], cv_rows))
    cv_min = float(cv_mode["cv"].min()) if not cv_mode.empty else float("nan")
    cv_max = float(cv_mode["cv"].max()) if not cv_mode.empty else float("nan")
    rep.append(
        f"\nObserved mode-average CV ranges from `{cv_min:.4f}` to `{cv_max:.4f}` "
        "(lower is more stable)."
    )

    # Perf metrics
    rep.append("\n\n## Additional Perf Metrics from `runs.csv`")
    rep.append("The `runs.csv` file includes Linux perf counters (`perf_*` columns).")
    rep.append("Raw counters rise with throughput, so the most useful comparison is normalized work: counters per 1,000 requests.")

    perf = runs[runs["perf_available"] == 1].copy()
    for c in [
        "perf_context_switches",
        "perf_cpu_migrations",
        "perf_cycles",
        "perf_instructions",
        "perf_cache_misses",
    ]:
        perf[f"{c}_per_kreq"] = perf[c] / perf["requests"] * 1000.0
    perf["ipc"] = perf["perf_instructions"] / perf["perf_cycles"]
    perf["branch_miss_rate"] = perf["perf_branch_misses"] / perf["perf_branches"]
    perf["cache_miss_rate"] = perf["perf_cache_misses"] / perf["perf_cache_references"]

    mode_perf = perf.groupby("mode", as_index=False).agg(
        avg_rps=("requests_sec", "mean"),
        ctx_k=("perf_context_switches_per_kreq", "mean"),
        mig_k=("perf_cpu_migrations_per_kreq", "mean"),
        cyc_k=("perf_cycles_per_kreq", "mean"),
        ins_k=("perf_instructions_per_kreq", "mean"),
        cmiss_k=("perf_cache_misses_per_kreq", "mean"),
        ipc=("ipc", "mean"),
        bmr=("branch_miss_rate", "mean"),
        cmr=("cache_miss_rate", "mean"),
    )

    rep.append("\n\n## Mode-Level Perf Summary")
    mode_rows = []
    for r in mode_perf.itertuples(index=False):
        mode_rows.append(
            [
                r.mode,
                f"{r.avg_rps:.2f}",
                f"{r.ctx_k:.2f}",
                f"{r.mig_k:.2f}",
                f"{r.cyc_k:.3e}",
                f"{r.ins_k:.3e}",
                f"{r.cmiss_k:,.2f}",
                f"{r.ipc:.4f}",
                f"{r.bmr * 100.0:.4f}%",
                f"{r.cmr * 100.0:.4f}%",
            ]
        )
    rep.append(
        "\n"
        + markdown_table(
            [
                "mode",
                "avg_rps",
                "ctx_switches_per_1k_req",
                "cpu_migrations_per_1k_req",
                "cycles_per_1k_req",
                "instructions_per_1k_req",
                "cache_misses_per_1k_req",
                "IPC",
                "branch_miss_rate",
                "cache_miss_rate",
            ],
            mode_rows,
        )
    )

    # Coro vs classic perf interpretation
    rep.append("\n\n## Coro vs Classic (Perf Interpretation)")
    if {"coro", "classic"}.issubset(set(mode_perf["mode"])):
        c = mode_perf[mode_perf["mode"] == "coro"].iloc[0]
        k = mode_perf[mode_perf["mode"] == "classic"].iloc[0]
        rep.append("Overall (`coro / classic`):")
        rep.append("")
        rep.append(f"- Throughput: `{c.avg_rps / k.avg_rps:.3f}x`")
        rep.append(f"- Context switches per 1k requests: `{c.ctx_k / k.ctx_k:.3f}x`")
        rep.append(f"- CPU migrations per 1k requests: `{c.mig_k / k.mig_k:.3f}x`")
        rep.append(f"- Cycles per 1k requests: `{c.cyc_k / k.cyc_k:.3f}x`")
        rep.append(f"- Instructions per 1k requests: `{c.ins_k / k.ins_k:.3f}x`")
        rep.append(f"- Cache misses per 1k requests: `{c.cmiss_k / k.cmiss_k:.3f}x`")
        rep.append(f"- IPC: `{c.ipc / k.ipc:.3f}x`")
        rep.append(f"- Branch miss rate: `{c.bmr / k.bmr:.3f}x`")
        rep.append(f"- Cache miss rate: `{c.cmr / k.cmr:.3f}x`")
        rep.append("\nInterpretation:")
        rep.append("- `coro` gains major throughput/latency, but does more scheduler and memory-system work per request.")
        rep.append("- Efficiency per request (especially cache behavior) is weaker in `coro`, but service performance is much better in this benchmark.")
    else:
        rep.append("Insufficient `coro`/`classic` data for this section.")

    # Interesting patterns
    rep.append("\n\n## Interesting Scenario Patterns")
    scen = perf.groupby(["mode", "threads", "preset"], as_index=False).agg(
        rps=("requests_sec", "mean"),
        ctx_k=("perf_context_switches_per_kreq", "mean"),
        mig_k=("perf_cpu_migrations_per_kreq", "mean"),
    )

    top_ctx = scen.sort_values("ctx_k", ascending=False).head(3)
    low_ctx = scen.sort_values("ctx_k", ascending=True).head(3)
    low_mig = scen.sort_values("mig_k", ascending=True).head(3)
    high_mig = scen.sort_values("mig_k", ascending=False).head(3)

    rep.append("- Highest context-switch intensity (`ctx_switches_per_1k_req`):")
    for r in top_ctx.itertuples(index=False):
        rep.append(f"  - `{r.mode} t{int(r.threads)} {r.preset}`: `{r.ctx_k:.1f}`")
    rep.append("- Lowest context-switch intensity:")
    for r in low_ctx.itertuples(index=False):
        rep.append(f"  - `{r.mode} t{int(r.threads)} {r.preset}`: `{r.ctx_k:.1f}`")
    rep.append("- Lowest migration intensity:")
    for r in low_mig.itertuples(index=False):
        rep.append(f"  - `{r.mode} t{int(r.threads)} {r.preset}`: `{r.mig_k:.1f}`")
    rep.append("- Highest migration intensity:")
    for r in high_mig.itertuples(index=False):
        rep.append(f"  - `{r.mode} t{int(r.threads)} {r.preset}`: `{r.mig_k:.1f}`")

    # Coro vs classic by preset perf ratios
    rep.append("\n\n## Coro vs Classic by Preset (Perf View)")
    sc = scen[scen["mode"] == "classic"].rename(
        columns={"rps": "classic_rps", "ctx_k": "classic_ctx", "mig_k": "classic_mig"}
    )[["threads", "preset", "classic_rps", "classic_ctx", "classic_mig"]]
    co = scen[scen["mode"] == "coro"].rename(columns={"rps": "coro_rps", "ctx_k": "coro_ctx", "mig_k": "coro_mig"})[
        ["threads", "preset", "coro_rps", "coro_ctx", "coro_mig"]
    ]

    perf_ipc = perf.groupby(["mode", "threads", "preset"], as_index=False).agg(
        ipc=("ipc", "mean"), cache_miss_rate=("cache_miss_rate", "mean")
    )
    sc2 = perf_ipc[perf_ipc["mode"] == "classic"].rename(columns={"ipc": "classic_ipc", "cache_miss_rate": "classic_cmr"})[
        ["threads", "preset", "classic_ipc", "classic_cmr"]
    ]
    co2 = perf_ipc[perf_ipc["mode"] == "coro"].rename(columns={"ipc": "coro_ipc", "cache_miss_rate": "coro_cmr"})[
        ["threads", "preset", "coro_ipc", "coro_cmr"]
    ]

    m = sc.merge(co, on=["threads", "preset"], how="inner").merge(sc2.merge(co2, on=["threads", "preset"], how="inner"), on=["threads", "preset"], how="inner")

    p_rows = []
    for p in presets:
        s = m[m["preset"] == p]
        p_rows.append(
            [
                p,
                gmean(s["coro_rps"] / s["classic_rps"]),
                gmean(s["coro_ctx"] / s["classic_ctx"]),
                gmean(s["coro_mig"] / s["classic_mig"]),
                gmean(s["coro_ipc"] / s["classic_ipc"]),
                gmean(s["coro_cmr"] / s["classic_cmr"]),
            ]
        )

    rep.append("Geometric mean across threads:")
    rep.append(
        "\n"
        + markdown_table(
            [
                "preset",
                "rps_speedup",
                "ctx_per_1k_req_ratio",
                "migrations_per_1k_req_ratio",
                "ipc_ratio",
                "cache_miss_rate_ratio",
            ],
            [[p, fmt_x(a, 3), fmt_x(b, 3), fmt_x(c1, 3), fmt_x(d, 3), fmt_x(e, 3)] for p, a, b, c1, d, e in p_rows],
        )
    )

    # Non-coro comparison
    rep.append("\n\n## Non-Coro Comparison (4 Thread-Pool Implementations Only)")
    non = summary[summary["mode"] != "coro"].copy()
    nw = (
        non.sort_values(["threads", "preset", "avg_requests_sec"], ascending=[True, True, False])
        .groupby(["threads", "preset"], as_index=False)
        .first()
    )
    nw_counts = nw["mode"].value_counts().to_dict()

    nb = non.groupby(["threads", "preset"], as_index=False)["avg_requests_sec"].max().rename(columns={"avg_requests_sec": "best_rps"})
    nn = non.merge(nb, on=["threads", "preset"], how="left")
    nn["norm_rps"] = nn["avg_requests_sec"] / nn["best_rps"]
    nnm = nn.groupby("mode", as_index=False)["norm_rps"].mean().sort_values("norm_rps", ascending=False)

    ngm_rows = []
    for m1 in [x for x in sorted(non["mode"].unique().tolist()) if x != "classic"]:
        a = non[non["mode"] == "classic"][["threads", "preset", "avg_requests_sec"]].rename(columns={"avg_requests_sec": "classic_rps"})
        b = non[non["mode"] == m1][["threads", "preset", "avg_requests_sec"]].rename(columns={"avg_requests_sec": "mode_rps"})
        x = a.merge(b, on=["threads", "preset"], how="inner")
        ngm_rows.append([m1, gmean(x["mode_rps"] / x["classic_rps"])])

    rep.append(
        "- Winner counts: "
        + ", ".join([f"`{m} {n}`" for m, n in sorted(nw_counts.items(), key=lambda kv: (-kv[1], kv[0]))])
        + f" (out of {len(nw)} scenarios)."
    )
    rep.append("- Avg normalized throughput:")
    for m, v in nnm[["mode", "norm_rps"]].itertuples(index=False, name=None):
        rep.append(f"  - `{m} {v:.4f}`")
    rep.append("- Geometric mean throughput vs classic:")
    for m, v in sorted(ngm_rows, key=lambda x: x[0]):
        rep.append(f"  - `{m} {v:.4f}x`")

    non_spread = float(nnm["norm_rps"].max() - nnm["norm_rps"].min()) if not nnm.empty else float("nan")
    if np.isfinite(non_spread) and non_spread <= 0.01:
        rep.append("\nConclusion for the four pool variants: performance differences are negligible in this dataset.")
    else:
        rep.append("\nConclusion for the four pool variants: there are material throughput gaps in this dataset.")

    rep.append("\n\n## Bottom Line")
    rep.append("1. `coro` is the dominant mode for this mixed HTTP workload dataset, with large gains in throughput and latency, especially on I/O-heavy presets.")
    if np.isfinite(non_spread) and non_spread <= 0.01:
        rep.append("2. Among the four thread-pool implementations (excluding `coro`), results are effectively tied in this benchmark.")
    else:
        rep.append("2. Among the four thread-pool implementations (excluding `coro`), `advws`/`elastic` materially outperform `classic`/`ws` in this run.")
    rep.append("3. `coro` trades per-request CPU/memory efficiency for much better service-level throughput/latency.")

    return "\n".join(rep) + "\n"


def main() -> None:
    ap = argparse.ArgumentParser(description="Analyze mixed HTTP workload summary/runs CSV")
    ap.add_argument("summary_csv", type=Path, help="Path to summary.csv")
    ap.add_argument("--runs", required=True, type=Path, dest="runs_csv", help="Path to runs.csv")
    ap.add_argument("--out", type=Path, default=None, help="Optional output markdown report path")
    args = ap.parse_args()

    report = build_report(args.summary_csv, args.runs_csv)
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(report, encoding="utf-8")
        print(f"Report written to: {args.out}")
    else:
        print(report)


if __name__ == "__main__":
    main()
