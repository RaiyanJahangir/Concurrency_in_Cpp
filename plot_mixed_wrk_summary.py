#!/usr/bin/env python3
"""Generate plots for mixed HTTP workload results.

Usage:
  python plot_mixed_wrk_summary.py \
    results/mixed_wrk_perf_20260303_151037/summary.csv \
    --runs results/mixed_wrk_perf_20260303_151037/runs.csv \
    --outdir results/mixed_wrk_perf_20260303_151037/plots
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import matplotlib
import numpy as np
import pandas as pd
from matplotlib.patches import Patch

matplotlib.use("Agg")
import matplotlib.pyplot as plt


MODE_ORDER = ["classic", "coro", "ws", "advws", "elastic"]
MODE_COLORS = {
    "classic": "#1f77b4",
    "coro": "#2ca02c",
    "ws": "#d62728",
    "advws": "#ff7f0e",
    "elastic": "#9467bd",
}


def gmean(vals: pd.Series) -> float:
    vals = vals[(vals > 0) & np.isfinite(vals)]
    if vals.empty:
        return float("nan")
    return float(np.exp(np.log(vals).mean()))


def parse_latency_to_ms(s: object) -> float:
    if s is None or (isinstance(s, float) and math.isnan(s)):
        return float("nan")
    x = str(s).strip()
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


def modes_present(df: pd.DataFrame) -> list[str]:
    present = sorted(df["mode"].dropna().unique().tolist())
    ordered = [m for m in MODE_ORDER if m in present]
    ordered.extend([m for m in present if m not in ordered])
    return ordered


def plot_throughput_vs_threads(summary: pd.DataFrame, outdir: Path) -> None:
    presets = sorted(summary["preset"].unique().tolist())
    threads = sorted(summary["threads"].unique().tolist())
    modes = modes_present(summary)

    fig, axes = plt.subplots(2, 3, figsize=(15, 8), sharex=True)
    axes = axes.flatten()
    for i, preset in enumerate(presets):
        ax = axes[i]
        sub = summary[summary["preset"] == preset]
        for mode in modes:
            s = sub[sub["mode"] == mode].sort_values("threads")
            if not s.empty:
                ax.plot(
                    s["threads"],
                    s["avg_requests_sec"],
                    marker="o",
                    label=mode,
                    color=MODE_COLORS.get(mode),
                    linewidth=1.6,
                )
        ax.set_title(preset)
        ax.set_xscale("log", base=2)
        ax.set_xticks(threads)
        ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
        ax.grid(alpha=0.3)
    for ax in axes:
        ax.set_xlabel("threads")
    axes[0].set_ylabel("avg requests/sec")
    axes[3].set_ylabel("avg requests/sec")
    handles = [plt.Line2D([0], [0], color=MODE_COLORS[m], marker="o", label=m) for m in modes]
    fig.legend(handles=handles, loc="upper center", ncol=len(handles))
    fig.suptitle("Throughput Scaling by Preset")
    fig.tight_layout()
    fig.savefig(outdir / "01_throughput_vs_threads_by_preset.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def plot_latency_vs_threads(summary: pd.DataFrame, outdir: Path) -> None:
    presets = sorted(summary["preset"].unique().tolist())
    threads = sorted(summary["threads"].unique().tolist())
    modes = modes_present(summary)

    fig, axes = plt.subplots(2, 3, figsize=(15, 8), sharex=True)
    axes = axes.flatten()
    for i, preset in enumerate(presets):
        ax = axes[i]
        sub = summary[summary["preset"] == preset]
        for mode in modes:
            s = sub[sub["mode"] == mode].sort_values("threads")
            if not s.empty:
                ax.plot(
                    s["threads"],
                    s["avg_latency_avg"],
                    marker="o",
                    label=mode,
                    color=MODE_COLORS.get(mode),
                    linewidth=1.6,
                )
        ax.set_title(preset)
        ax.set_xscale("log", base=2)
        ax.set_xticks(threads)
        ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
        ax.set_yscale("log")
        ax.grid(alpha=0.3)
    for ax in axes:
        ax.set_xlabel("threads")
    axes[0].set_ylabel("avg latency (ms, log)")
    axes[3].set_ylabel("avg latency (ms, log)")
    handles = [plt.Line2D([0], [0], color=MODE_COLORS[m], marker="o", label=m) for m in modes]
    fig.legend(handles=handles, loc="upper center", ncol=len(handles))
    fig.suptitle("Latency Scaling by Preset")
    fig.tight_layout()
    fig.savefig(outdir / "02_latency_vs_threads_by_preset.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def plot_winner_heatmap(summary: pd.DataFrame, outdir: Path) -> None:
    modes = modes_present(summary)
    mode_to_idx = {m: i for i, m in enumerate(modes)}
    threads = sorted(summary["threads"].unique().tolist())
    presets = sorted(summary["preset"].unique().tolist())
    mat = np.full((len(presets), len(threads)), np.nan)

    for i, preset in enumerate(presets):
        for j, t in enumerate(threads):
            sub = summary[(summary["preset"] == preset) & (summary["threads"] == t)]
            if sub.empty:
                continue
            winner = sub.sort_values("avg_requests_sec", ascending=False).iloc[0]["mode"]
            mat[i, j] = mode_to_idx[winner]

    fig, ax = plt.subplots(figsize=(8.5, 4.5))
    cmap = plt.get_cmap("tab10", len(modes))
    ax.imshow(mat, aspect="auto", cmap=cmap, vmin=0, vmax=len(modes) - 1)
    ax.set_xticks(np.arange(len(threads)))
    ax.set_xticklabels([str(t) for t in threads])
    ax.set_yticks(np.arange(len(presets)))
    ax.set_yticklabels(presets)
    ax.set_xlabel("threads")
    ax.set_ylabel("preset")
    ax.set_title("Winner by Throughput (preset x threads)")
    legend_handles = [Patch(color=cmap(mode_to_idx[m]), label=m) for m in modes]
    ax.legend(handles=legend_handles, title="winner", bbox_to_anchor=(1.02, 1), loc="upper left")
    fig.tight_layout()
    fig.savefig(outdir / "03_winner_heatmap.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def plot_speedup_vs_classic(summary: pd.DataFrame, outdir: Path) -> None:
    modes = [m for m in modes_present(summary) if m != "classic"]
    threads = sorted(summary["threads"].unique().tolist())
    fig, ax = plt.subplots(figsize=(8, 5))

    for mode in modes:
        xs, ys = [], []
        for t in threads:
            g = summary[summary["threads"] == t]
            merged = (
                g[g["mode"] == "classic"][["preset", "avg_requests_sec"]]
                .rename(columns={"avg_requests_sec": "classic_rps"})
                .merge(
                    g[g["mode"] == mode][["preset", "avg_requests_sec"]].rename(
                        columns={"avg_requests_sec": "mode_rps"}
                    ),
                    on="preset",
                    how="inner",
                )
            )
            if merged.empty:
                continue
            speedup = gmean(merged["mode_rps"] / merged["classic_rps"])
            if np.isfinite(speedup):
                xs.append(t)
                ys.append(speedup)
        if xs:
            ax.plot(xs, ys, marker="o", color=MODE_COLORS.get(mode), label=mode)

    ax.axhline(1.0, linestyle="--", color="black", linewidth=1)
    ax.set_xscale("log", base=2)
    ax.set_xticks(threads)
    ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
    ax.set_xlabel("threads")
    ax.set_ylabel("geo mean throughput speedup vs classic")
    ax.set_title("Speedup vs Classic by Thread")
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "04_speedup_vs_classic_by_thread.png", dpi=170)
    plt.close(fig)


def prepare_runs(runs: pd.DataFrame) -> pd.DataFrame:
    df = runs.copy()
    num_cols = [
        "requests",
        "requests_sec",
        "perf_available",
        "perf_context_switches",
        "perf_cpu_migrations",
        "perf_cycles",
        "perf_instructions",
        "perf_branches",
        "perf_branch_misses",
        "perf_cache_references",
        "perf_cache_misses",
    ]
    for c in num_cols:
        df[c] = pd.to_numeric(df[c], errors="coerce")
    df["lat_avg_ms"] = df["lat_avg"].map(parse_latency_to_ms)
    df["p99_ms"] = df["p99"].map(parse_latency_to_ms)
    perf = df[df["perf_available"] == 1].copy()
    for c in ["perf_context_switches", "perf_cpu_migrations", "perf_cycles", "perf_instructions", "perf_cache_misses"]:
        perf[f"{c}_per_kreq"] = perf[c] / perf["requests"] * 1000.0
    perf["ipc"] = perf["perf_instructions"] / perf["perf_cycles"]
    perf["branch_miss_rate"] = perf["perf_branch_misses"] / perf["perf_branches"]
    perf["cache_miss_rate"] = perf["perf_cache_misses"] / perf["perf_cache_references"]
    return perf


def plot_perf_per_kreq(perf: pd.DataFrame, outdir: Path) -> None:
    modes = modes_present(perf)
    agg = perf.groupby("mode", as_index=False).agg(
        ctx_k=("perf_context_switches_per_kreq", "mean"),
        mig_k=("perf_cpu_migrations_per_kreq", "mean"),
        cycles_k=("perf_cycles_per_kreq", "mean"),
        ins_k=("perf_instructions_per_kreq", "mean"),
        cmiss_k=("perf_cache_misses_per_kreq", "mean"),
    )
    agg["mode"] = pd.Categorical(agg["mode"], categories=modes, ordered=True)
    agg = agg.sort_values("mode")

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.5))
    x = np.arange(len(agg))
    colors = [MODE_COLORS.get(m, "#999999") for m in agg["mode"]]

    axes[0].bar(x - 0.16, agg["ctx_k"], width=0.32, label="ctx switches / 1k req", color="#4c78a8")
    axes[0].bar(x + 0.16, agg["mig_k"], width=0.32, label="cpu migrations / 1k req", color="#f58518")
    axes[0].set_xticks(x)
    axes[0].set_xticklabels(agg["mode"], rotation=20)
    axes[0].set_title("Scheduler Activity per 1k Requests")
    axes[0].grid(alpha=0.3, axis="y")
    axes[0].legend(fontsize=8)

    axes[1].bar(x - 0.22, agg["cycles_k"], width=0.22, label="cycles", color="#54a24b")
    axes[1].bar(x, agg["ins_k"], width=0.22, label="instructions", color="#b279a2")
    axes[1].bar(x + 0.22, agg["cmiss_k"], width=0.22, label="cache misses", color="#e45756")
    axes[1].set_yscale("log")
    axes[1].set_xticks(x)
    axes[1].set_xticklabels(agg["mode"], rotation=20)
    axes[1].set_title("Perf Counters per 1k Requests (log scale)")
    axes[1].grid(alpha=0.3, axis="y")
    axes[1].legend(fontsize=8)

    for ax in axes:
        for i, c in enumerate(colors):
            ax.axvspan(i - 0.5, i + 0.5, color=c, alpha=0.03)

    fig.tight_layout()
    fig.savefig(outdir / "05_perf_counters_per_kreq.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def plot_latency_tail(perf: pd.DataFrame, outdir: Path) -> None:
    modes = modes_present(perf)
    g = perf.groupby(["mode", "threads"], as_index=False).agg(
        lat_avg_ms=("lat_avg_ms", "mean"),
        p99_ms=("p99_ms", "mean"),
    )
    threads = sorted(g["threads"].unique().tolist())
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.5), sharex=True)
    for mode in modes:
        s = g[g["mode"] == mode].sort_values("threads")
        if s.empty:
            continue
        axes[0].plot(s["threads"], s["lat_avg_ms"], marker="o", color=MODE_COLORS.get(mode), label=mode)
        axes[1].plot(s["threads"], s["p99_ms"], marker="o", color=MODE_COLORS.get(mode), label=mode)
    for ax, ttl in zip(axes, ["Average Latency", "Tail Latency (p99)"]):
        ax.set_title(ttl)
        ax.set_xscale("log", base=2)
        ax.set_xticks(threads)
        ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
        ax.set_yscale("log")
        ax.grid(alpha=0.3)
        ax.set_xlabel("threads")
    axes[0].set_ylabel("latency (ms, log)")
    axes[0].legend()
    fig.tight_layout()
    fig.savefig(outdir / "06_latency_and_p99_vs_threads.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def plot_stability(perf: pd.DataFrame, outdir: Path) -> None:
    grp = perf.groupby(["mode", "threads", "preset"], as_index=False)["requests_sec"].agg(["mean", "std"]).reset_index()
    grp["cv"] = grp["std"] / grp["mean"]
    modes = modes_present(grp)

    fig, ax = plt.subplots(figsize=(8, 4.5))
    data = [grp[grp["mode"] == m]["cv"].dropna().values for m in modes]
    bp = ax.boxplot(data, patch_artist=True, tick_labels=modes, showfliers=True)
    for patch, m in zip(bp["boxes"], modes):
        patch.set_facecolor(MODE_COLORS.get(m))
        patch.set_alpha(0.5)
    ax.set_ylabel("CV of requests/sec across 3 reps")
    ax.set_title("Run-to-Run Stability by Mode")
    ax.grid(alpha=0.3, axis="y")
    fig.tight_layout()
    fig.savefig(outdir / "07_stability_cv_boxplot.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def plot_tradeoff_scatter(perf: pd.DataFrame, outdir: Path) -> None:
    scen = perf.groupby(["mode", "threads", "preset"], as_index=False).agg(
        rps=("requests_sec", "mean"),
        ctx_k=("perf_context_switches_per_kreq", "mean"),
        cachemr=("cache_miss_rate", "mean"),
    )
    modes = modes_present(scen)
    fig, ax = plt.subplots(figsize=(8, 5))
    for mode in modes:
        s = scen[scen["mode"] == mode]
        ax.scatter(
            s["ctx_k"],
            s["rps"],
            s=np.clip(s["cachemr"] * 3000, 20, 260),
            alpha=0.75,
            color=MODE_COLORS.get(mode),
            label=mode,
            edgecolors="none",
        )
    ax.set_xlabel("context switches per 1k requests")
    ax.set_ylabel("requests/sec")
    ax.set_title("Throughput vs Scheduling Overhead (bubble=size cache miss rate)")
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "08_throughput_vs_ctx_scatter.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def write_plots_md(outdir: Path) -> None:
    text = """# Mixed Workload Plots

- `01_throughput_vs_threads_by_preset.png`: Throughput scaling for each preset.
- `02_latency_vs_threads_by_preset.png`: Average latency scaling for each preset.
- `03_winner_heatmap.png`: Best-throughput mode by `(preset, threads)`.
- `04_speedup_vs_classic_by_thread.png`: Geometric mean throughput speedup vs `classic`.
- `05_perf_counters_per_kreq.png`: Normalized perf counters (scheduler + micro-arch).
- `06_latency_and_p99_vs_threads.png`: Average and p99 latency trends by thread count.
- `07_stability_cv_boxplot.png`: Run-to-run stability (CV of requests/sec).
- `08_throughput_vs_ctx_scatter.png`: Throughput vs scheduling overhead tradeoff.
"""
    (outdir / "PLOTS.md").write_text(text, encoding="utf-8")


def plot_non_coro_throughput(summary: pd.DataFrame, outdir: Path) -> None:
    df = summary[summary["mode"] != "coro"].copy()
    presets = sorted(df["preset"].unique().tolist())
    threads = sorted(df["threads"].unique().tolist())
    modes = modes_present(df)

    fig, axes = plt.subplots(2, 3, figsize=(15, 8), sharex=True)
    axes = axes.flatten()
    for i, preset in enumerate(presets):
        ax = axes[i]
        sub = df[df["preset"] == preset]
        for mode in modes:
            s = sub[sub["mode"] == mode].sort_values("threads")
            if s.empty:
                continue
            ax.plot(
                s["threads"],
                s["avg_requests_sec"],
                marker="o",
                label=mode,
                color=MODE_COLORS.get(mode),
                linewidth=1.8,
            )
        ax.set_title(preset)
        ax.set_xscale("log", base=2)
        ax.set_xticks(threads)
        ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
        ax.grid(alpha=0.3)
    for ax in axes:
        ax.set_xlabel("threads")
    axes[0].set_ylabel("avg requests/sec")
    axes[3].set_ylabel("avg requests/sec")
    handles = [plt.Line2D([0], [0], color=MODE_COLORS[m], marker="o", label=m) for m in modes]
    fig.legend(handles=handles, loc="upper center", ncol=len(handles))
    fig.suptitle("Non-Coro Throughput by Preset")
    fig.tight_layout()
    fig.savefig(outdir / "09_non_coro_throughput_by_preset.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def plot_non_coro_zoom(summary: pd.DataFrame, outdir: Path) -> None:
    df = summary[summary["mode"] != "coro"].copy()
    presets = sorted(df["preset"].unique().tolist())
    threads = sorted(df["threads"].unique().tolist())
    modes = modes_present(df)

    fig, axes = plt.subplots(2, 3, figsize=(15, 8), sharex=True)
    axes = axes.flatten()
    for i, preset in enumerate(presets):
        ax = axes[i]
        sub = df[df["preset"] == preset].copy()
        ymin = sub["avg_requests_sec"].min()
        ymax = sub["avg_requests_sec"].max()
        pad = max((ymax - ymin) * 0.3, max(ymax * 0.0008, 0.5))
        for mode in modes:
            s = sub[sub["mode"] == mode].sort_values("threads")
            if s.empty:
                continue
            ax.plot(
                s["threads"],
                s["avg_requests_sec"],
                marker="o",
                label=mode,
                color=MODE_COLORS.get(mode),
                linewidth=1.8,
            )
        ax.set_ylim(ymin - pad, ymax + pad)
        ax.set_title(f"{preset} (zoom)")
        ax.set_xscale("log", base=2)
        ax.set_xticks(threads)
        ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
        ax.grid(alpha=0.3)
    for ax in axes:
        ax.set_xlabel("threads")
    axes[0].set_ylabel("avg requests/sec (zoomed)")
    axes[3].set_ylabel("avg requests/sec (zoomed)")
    handles = [plt.Line2D([0], [0], color=MODE_COLORS[m], marker="o", label=m) for m in modes]
    fig.legend(handles=handles, loc="upper center", ncol=len(handles))
    fig.suptitle("Non-Coro Throughput by Preset (Zoomed Y)")
    fig.tight_layout()
    fig.savefig(outdir / "10_non_coro_throughput_zoomed.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def plot_non_coro_delta_vs_classic(summary: pd.DataFrame, outdir: Path) -> None:
    df = summary[summary["mode"] != "coro"].copy()
    presets = sorted(df["preset"].unique().tolist())
    threads = sorted(df["threads"].unique().tolist())
    modes = [m for m in modes_present(df) if m != "classic"]

    fig, axes = plt.subplots(2, 3, figsize=(15, 8), sharex=True)
    axes = axes.flatten()
    for i, preset in enumerate(presets):
        ax = axes[i]
        sub = df[df["preset"] == preset]
        base = sub[sub["mode"] == "classic"][["threads", "avg_requests_sec"]].rename(
            columns={"avg_requests_sec": "classic_rps"}
        )
        for mode in modes:
            cur = sub[sub["mode"] == mode][["threads", "avg_requests_sec"]].rename(
                columns={"avg_requests_sec": "mode_rps"}
            )
            m = base.merge(cur, on="threads", how="inner").sort_values("threads")
            if m.empty:
                continue
            delta_pct = (m["mode_rps"] / m["classic_rps"] - 1.0) * 100.0
            ax.plot(
                m["threads"],
                delta_pct,
                marker="o",
                label=mode,
                color=MODE_COLORS.get(mode),
                linewidth=1.8,
            )
        ax.axhline(0.0, color="black", linestyle="--", linewidth=1)
        ax.set_title(f"{preset} (delta vs classic)")
        ax.set_xscale("log", base=2)
        ax.set_xticks(threads)
        ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
        ax.grid(alpha=0.3)
    for ax in axes:
        ax.set_xlabel("threads")
    axes[0].set_ylabel("throughput delta vs classic (%)")
    axes[3].set_ylabel("throughput delta vs classic (%)")
    handles = [plt.Line2D([0], [0], color=MODE_COLORS[m], marker="o", label=m) for m in modes]
    fig.legend(handles=handles, loc="upper center", ncol=len(handles))
    fig.suptitle("Non-Coro Throughput Delta vs Classic")
    fig.tight_layout()
    fig.savefig(outdir / "11_non_coro_delta_vs_classic.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def plot_non_coro_latency_delta(summary: pd.DataFrame, outdir: Path) -> None:
    df = summary[summary["mode"] != "coro"].copy()
    presets = sorted(df["preset"].unique().tolist())
    threads = sorted(df["threads"].unique().tolist())
    modes = [m for m in modes_present(df) if m != "classic"]

    fig, axes = plt.subplots(2, 3, figsize=(15, 8), sharex=True)
    axes = axes.flatten()
    for i, preset in enumerate(presets):
        ax = axes[i]
        sub = df[df["preset"] == preset]
        base = sub[sub["mode"] == "classic"][["threads", "avg_latency_avg"]].rename(
            columns={"avg_latency_avg": "classic_lat"}
        )
        for mode in modes:
            cur = sub[sub["mode"] == mode][["threads", "avg_latency_avg"]].rename(
                columns={"avg_latency_avg": "mode_lat"}
            )
            m = base.merge(cur, on="threads", how="inner").sort_values("threads")
            if m.empty:
                continue
            delta_pct = (m["mode_lat"] / m["classic_lat"] - 1.0) * 100.0
            ax.plot(
                m["threads"],
                delta_pct,
                marker="o",
                label=mode,
                color=MODE_COLORS.get(mode),
                linewidth=1.8,
            )
        ax.axhline(0.0, color="black", linestyle="--", linewidth=1)
        ax.set_title(f"{preset} (lat delta vs classic)")
        ax.set_xscale("log", base=2)
        ax.set_xticks(threads)
        ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
        ax.grid(alpha=0.3)
    for ax in axes:
        ax.set_xlabel("threads")
    axes[0].set_ylabel("avg latency delta vs classic (%)")
    axes[3].set_ylabel("avg latency delta vs classic (%)")
    handles = [plt.Line2D([0], [0], color=MODE_COLORS[m], marker="o", label=m) for m in modes]
    fig.legend(handles=handles, loc="upper center", ncol=len(handles))
    fig.suptitle("Non-Coro Latency Delta vs Classic")
    fig.tight_layout()
    fig.savefig(outdir / "12_non_coro_latency_delta_vs_classic.png", dpi=170, bbox_inches="tight")
    plt.close(fig)


def write_non_coro_plots_md(outdir: Path) -> None:
    text = """# Non-Coro Focused Plots

- `09_non_coro_throughput_by_preset.png`: Non-coro throughput only.
- `10_non_coro_throughput_zoomed.png`: Same as above with zoomed y-axis per preset.
- `11_non_coro_delta_vs_classic.png`: Throughput percentage delta vs classic.
- `12_non_coro_latency_delta_vs_classic.png`: Latency percentage delta vs classic.
"""
    (outdir / "PLOTS_NON_CORO.md").write_text(text, encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("summary_csv", type=Path)
    ap.add_argument("--runs", required=True, type=Path, dest="runs_csv")
    ap.add_argument("--outdir", type=Path, default=None)
    ap.add_argument(
        "--plot",
        default="all",
        choices=[
            "all",
            "01",
            "02",
            "03",
            "04",
            "05",
            "06",
            "07",
            "08",
            "09",
            "10",
            "11",
            "12",
            "noncoro",
            "md",
        ],
        help="Generate a single plot id or all.",
    )
    args = ap.parse_args()

    summary = pd.read_csv(args.summary_csv)
    runs = pd.read_csv(args.runs_csv)
    outdir = args.outdir or args.summary_csv.parent / "plots"
    outdir.mkdir(parents=True, exist_ok=True)

    if args.plot in ("all", "01"):
        plot_throughput_vs_threads(summary, outdir)
    if args.plot in ("all", "02"):
        plot_latency_vs_threads(summary, outdir)
    if args.plot in ("all", "03"):
        plot_winner_heatmap(summary, outdir)
    if args.plot in ("all", "04"):
        plot_speedup_vs_classic(summary, outdir)

    if args.plot in ("all", "05", "06", "07", "08"):
        perf = prepare_runs(runs)
        if args.plot in ("all", "05"):
            plot_perf_per_kreq(perf, outdir)
        if args.plot in ("all", "06"):
            plot_latency_tail(perf, outdir)
        if args.plot in ("all", "07"):
            plot_stability(perf, outdir)
    if args.plot in ("all", "08"):
        plot_tradeoff_scatter(perf, outdir)
    if args.plot in ("all", "09", "noncoro"):
        plot_non_coro_throughput(summary, outdir)
    if args.plot in ("all", "10", "noncoro"):
        plot_non_coro_zoom(summary, outdir)
    if args.plot in ("all", "11", "noncoro"):
        plot_non_coro_delta_vs_classic(summary, outdir)
    if args.plot in ("all", "12", "noncoro"):
        plot_non_coro_latency_delta(summary, outdir)
    if args.plot in ("all", "md"):
        write_plots_md(outdir)
    if args.plot in ("all", "noncoro"):
        write_non_coro_plots_md(outdir)

    print(f"Done plot={args.plot} -> {outdir}")


if __name__ == "__main__":
    main()
