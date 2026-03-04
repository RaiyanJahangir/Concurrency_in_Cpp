#!/usr/bin/env python3
"""Generate performance plots from matrix-chain benchmark summary.csv.

Usage:
  python plot_matrix_chain_summary.py /path/to/summary.csv
  python plot_matrix_chain_summary.py /path/to/summary.csv --outdir /path/to/plots
"""

from __future__ import annotations

import argparse
import csv
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.patches import Patch


MODE_ORDER = ["classic", "coro", "ws", "advws", "elastic"]
MODE_COLORS = {
    "classic": "#1f77b4",
    "coro": "#2ca02c",
    "ws": "#d62728",
    "advws": "#ff7f0e",
    "elastic": "#9467bd",
}


def to_float(value: str, default: float = float("nan")) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def to_int(value: str, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def gmean(values: Iterable[float]) -> Optional[float]:
    vals = [v for v in values if v > 0 and math.isfinite(v)]
    if not vals:
        return None
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


def load_rows(csv_path: Path) -> List[Dict[str, object]]:
    out: List[Dict[str, object]] = []
    with csv_path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            out.append(
                {
                    "bench": r.get("bench", ""),
                    "preset": r.get("preset", ""),
                    "mode": r.get("mode", ""),
                    "threads": to_int(r.get("threads", "0")),
                    "bench_best_s": to_float(r.get("bench_best_s", "")),
                    "bench_avg_s": to_float(r.get("bench_avg_s", "")),
                    "gflops_best": to_float(r.get("gflops_best", "")),
                    "time_max_rss_kb": to_float(r.get("time_max_rss_kb", "")),
                    "perf_context_switches": to_float(r.get("perf_context_switches", "")),
                    "perf_cache_misses": to_float(r.get("perf_cache_misses", "")),
                    "perf_cache_references": to_float(r.get("perf_cache_references", "")),
                }
            )
    return out


def valid_rows(rows: List[Dict[str, object]]) -> List[Dict[str, object]]:
    return [r for r in rows if isinstance(r["bench_best_s"], float) and r["bench_best_s"] > 0 and math.isfinite(r["bench_best_s"])]


def modes_present(rows: List[Dict[str, object]]) -> List[str]:
    present = sorted({str(r["mode"]) for r in rows})
    ordered = [m for m in MODE_ORDER if m in present]
    ordered.extend([m for m in present if m not in ordered])
    return ordered


def avg(values: List[float]) -> Optional[float]:
    vals = [v for v in values if math.isfinite(v)]
    if not vals:
        return None
    return sum(vals) / len(vals)


def scenario_groups(rows: List[Dict[str, object]]) -> Dict[Tuple[str, str, int], List[Dict[str, object]]]:
    groups: Dict[Tuple[str, str, int], List[Dict[str, object]]] = defaultdict(list)
    for r in rows:
        key = (str(r["bench"]), str(r["preset"]), int(r["threads"]))
        groups[key].append(r)
    return groups


def plot_runtime_vs_threads_by_preset(rows: List[Dict[str, object]], outdir: Path) -> None:
    presets = sorted({str(r["preset"]) for r in rows})
    threads = sorted({int(r["threads"]) for r in rows})
    modes = modes_present(rows)

    fig, axes = plt.subplots(1, len(presets), figsize=(5 * max(1, len(presets)), 5), sharey=True)
    if len(presets) == 1:
        axes = [axes]

    for idx, preset in enumerate(presets):
        ax = axes[idx]
        subset = [r for r in rows if r["preset"] == preset]
        for mode in modes:
            xs = []
            ys = []
            for t in threads:
                vals = [float(r["bench_best_s"]) for r in subset if r["mode"] == mode and r["threads"] == t]
                a = avg(vals)
                if a is not None:
                    xs.append(t)
                    ys.append(a)
            if xs:
                ax.plot(xs, ys, marker="o", label=mode, color=MODE_COLORS.get(mode))

        ax.set_title(preset)
        ax.set_xlabel("threads")
        ax.set_xscale("log", base=2)
        ax.set_xticks(threads)
        ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
        ax.set_yscale("log")
        ax.grid(True, alpha=0.3)

    axes[0].set_ylabel("avg bench_best_s (seconds, log)")
    handles = [Line2D([0], [0], color=MODE_COLORS.get(m), marker="o", label=m) for m in modes]
    fig.legend(handles=handles, loc="upper center", ncol=max(1, len(modes)))
    fig.suptitle("Matrix-Chain Runtime Scaling by Threads")
    fig.tight_layout()
    fig.savefig(outdir / "01_runtime_vs_threads_by_preset.png", dpi=160, bbox_inches="tight")
    plt.close(fig)


def plot_speedup_vs_classic(rows: List[Dict[str, object]], outdir: Path) -> None:
    groups = scenario_groups(rows)
    threads = sorted({int(r["threads"]) for r in rows})
    modes = [m for m in modes_present(rows) if m != "classic"]

    speedup_by_mode: Dict[str, List[Tuple[int, float]]] = defaultdict(list)
    for mode in modes:
        for t in threads:
            ratios: List[float] = []
            for (_bench, _preset, th), rs in groups.items():
                if th != t:
                    continue
                by_mode = {str(r["mode"]): r for r in rs}
                c = by_mode.get("classic")
                x = by_mode.get(mode)
                if not c or not x:
                    continue
                tc = float(c["bench_best_s"])
                tx = float(x["bench_best_s"])
                if tc > 0 and tx > 0:
                    ratios.append(tc / tx)
            gm = gmean(ratios)
            if gm is not None:
                speedup_by_mode[mode].append((t, gm))

    fig, ax = plt.subplots(figsize=(8, 5))
    for mode in modes:
        pts = speedup_by_mode.get(mode, [])
        if pts:
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            ax.plot(xs, ys, marker="o", label=mode, color=MODE_COLORS.get(mode))

    ax.axhline(1.0, color="black", linestyle="--", linewidth=1)
    ax.set_xscale("log", base=2)
    ax.set_xticks(threads)
    ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
    ax.set_xlabel("threads")
    ax.set_ylabel("geometric mean speedup vs classic")
    ax.set_title("Matrix-Chain Speedup vs Classic")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "02_speedup_vs_classic_by_thread.png", dpi=160)
    plt.close(fig)


def plot_best_mode_heatmap(rows: List[Dict[str, object]], outdir: Path) -> None:
    groups = scenario_groups(rows)
    threads = sorted({int(r["threads"]) for r in rows})
    modes = modes_present(rows)
    mode_to_idx = {m: i for i, m in enumerate(modes)}

    presets = sorted({k[1] for k in groups.keys()})
    preset_to_row = {p: i for i, p in enumerate(presets)}
    th_to_col = {t: i for i, t in enumerate(threads)}

    data = [[math.nan for _ in threads] for _ in presets]
    for (_bench, preset, t), rs in groups.items():
        winner = min(rs, key=lambda r: float(r["bench_best_s"]))
        data[preset_to_row[preset]][th_to_col[t]] = mode_to_idx[str(winner["mode"])]

    fig, ax = plt.subplots(figsize=(8, 5))
    cmap = plt.get_cmap("tab10", max(3, len(modes)))
    ax.imshow(data, aspect="auto", interpolation="nearest", cmap=cmap, vmin=0, vmax=len(modes) - 1)

    ax.set_yticks(range(len(presets)))
    ax.set_yticklabels(presets)
    ax.set_xticks(range(len(threads)))
    ax.set_xticklabels([str(t) for t in threads])
    ax.set_xlabel("threads")
    ax.set_ylabel("preset")
    ax.set_title("Best Mode per Preset x Thread")

    legend_handles = [Patch(color=cmap(mode_to_idx[m]), label=m) for m in modes]
    ax.legend(handles=legend_handles, title="winner", bbox_to_anchor=(1.02, 1), loc="upper left")

    fig.tight_layout()
    fig.savefig(outdir / "03_best_mode_heatmap.png", dpi=160)
    plt.close(fig)


def plot_wins(rows: List[Dict[str, object]], outdir: Path) -> None:
    groups = scenario_groups(rows)
    modes = modes_present(rows)
    threads = sorted({int(r["threads"]) for r in rows})

    overall = Counter()
    by_thread: Dict[int, Counter] = defaultdict(Counter)

    for (_bench, _preset, t), rs in groups.items():
        winner = min(rs, key=lambda r: float(r["bench_best_s"]))
        mode = str(winner["mode"])
        overall[mode] += 1
        by_thread[t][mode] += 1

    fig, axes = plt.subplots(1, 2, figsize=(13, 5))

    x = list(range(len(modes)))
    axes[0].bar(x, [overall[m] for m in modes], color=[MODE_COLORS.get(m) for m in modes])
    axes[0].set_xticks(x)
    axes[0].set_xticklabels(modes, rotation=25)
    axes[0].set_ylabel("wins")
    axes[0].set_title("Total Scenario Wins")
    axes[0].grid(True, axis="y", alpha=0.3)

    bottom = [0] * len(threads)
    for mode in modes:
        vals = [by_thread[t][mode] for t in threads]
        axes[1].bar([str(t) for t in threads], vals, bottom=bottom, label=mode, color=MODE_COLORS.get(mode))
        bottom = [bottom[i] + vals[i] for i in range(len(vals))]
    axes[1].set_xlabel("threads")
    axes[1].set_ylabel("wins")
    axes[1].set_title("Wins by Thread")
    axes[1].grid(True, axis="y", alpha=0.3)
    axes[1].legend()

    fig.tight_layout()
    fig.savefig(outdir / "04_wins_overall_and_by_thread.png", dpi=160)
    plt.close(fig)


def plot_stability(rows: List[Dict[str, object]], outdir: Path) -> None:
    modes = modes_present(rows)
    threads = sorted({int(r["threads"]) for r in rows})

    fig, ax = plt.subplots(figsize=(8, 5))
    for mode in modes:
        xs: List[int] = []
        ys: List[float] = []
        for t in threads:
            ratios = []
            for r in rows:
                if r["mode"] != mode or r["threads"] != t:
                    continue
                best = float(r["bench_best_s"])
                avg_s = float(r["bench_avg_s"])
                if best > 0 and avg_s > 0 and math.isfinite(best) and math.isfinite(avg_s):
                    ratios.append(avg_s / best)
            a = avg(ratios)
            if a is not None:
                xs.append(t)
                ys.append(a)
        if xs:
            ax.plot(xs, ys, marker="o", label=mode, color=MODE_COLORS.get(mode))

    ax.set_xscale("log", base=2)
    ax.set_xticks(threads)
    ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
    ax.set_xlabel("threads")
    ax.set_ylabel("avg stability ratio (bench_avg_s / bench_best_s)")
    ax.set_title("Runtime Stability by Mode")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "05_stability_ratio_by_mode.png", dpi=160)
    plt.close(fig)


def plot_efficiency_scatter(rows: List[Dict[str, object]], outdir: Path) -> None:
    modes = modes_present(rows)

    fig, axes = plt.subplots(1, 2, figsize=(13, 5))

    for mode in modes:
        subset = [r for r in rows if r["mode"] == mode]

        x1 = [float(r["bench_best_s"]) * 1e6 for r in subset if float(r["perf_context_switches"]) >= 0]
        y1 = [float(r["perf_context_switches"]) for r in subset if float(r["perf_context_switches"]) >= 0]
        axes[0].scatter(x1, y1, alpha=0.7, label=mode, s=24, color=MODE_COLORS.get(mode))

        x2 = [float(r["bench_best_s"]) * 1e6 for r in subset if float(r["time_max_rss_kb"]) > 0]
        y2 = [float(r["time_max_rss_kb"]) for r in subset if float(r["time_max_rss_kb"]) > 0]
        axes[1].scatter(x2, y2, alpha=0.7, label=mode, s=24, color=MODE_COLORS.get(mode))

    axes[0].set_xlabel("bench_best_s (microseconds)")
    axes[0].set_ylabel("perf_context_switches")
    axes[0].set_title("Runtime vs Context Switches")
    axes[0].set_xscale("log")
    axes[0].set_yscale("log")
    axes[0].grid(True, alpha=0.3)

    axes[1].set_xlabel("bench_best_s (microseconds)")
    axes[1].set_ylabel("time_max_rss_kb")
    axes[1].set_title("Runtime vs Memory (RSS)")
    axes[1].set_xscale("log")
    axes[1].grid(True, alpha=0.3)

    handles = [Line2D([0], [0], marker="o", linestyle="", color=MODE_COLORS.get(m), label=m) for m in modes]
    fig.legend(handles=handles, loc="upper center", ncol=max(1, len(modes)))
    fig.tight_layout()
    fig.savefig(outdir / "06_efficiency_scatter.png", dpi=160, bbox_inches="tight")
    plt.close(fig)


def plot_gflops(rows: List[Dict[str, object]], outdir: Path) -> None:
    threads = sorted({int(r["threads"]) for r in rows})
    modes = modes_present(rows)

    fig, ax = plt.subplots(figsize=(8, 5))
    for mode in modes:
        xs = []
        ys = []
        for t in threads:
            vals = [float(r["gflops_best"]) for r in rows if r["mode"] == mode and r["threads"] == t and float(r["gflops_best"]) > 0]
            a = avg(vals)
            if a is not None:
                xs.append(t)
                ys.append(a)
        if xs:
            ax.plot(xs, ys, marker="o", label=mode, color=MODE_COLORS.get(mode))

    ax.set_xscale("log", base=2)
    ax.set_xticks(threads)
    ax.get_xaxis().set_major_formatter(plt.ScalarFormatter())
    ax.set_xlabel("threads")
    ax.set_ylabel("avg effective GFLOP/s")
    ax.set_title("Compute Throughput by Mode")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(outdir / "07_gflops_by_thread.png", dpi=160)
    plt.close(fig)


def plot_resource_boxplots(rows: List[Dict[str, object]], outdir: Path) -> None:
    modes = modes_present(rows)
    rss_data = []
    miss_rate_data = []

    for mode in modes:
        subset = [r for r in rows if r["mode"] == mode]
        rss_vals = [float(r["time_max_rss_kb"]) for r in subset if float(r["time_max_rss_kb"]) > 0]

        rates: List[float] = []
        for r in subset:
            misses = float(r["perf_cache_misses"])
            refs = float(r["perf_cache_references"])
            if refs > 0 and misses >= 0:
                rates.append(misses / refs)

        rss_data.append(rss_vals if rss_vals else [math.nan])
        miss_rate_data.append(rates if rates else [math.nan])

    fig, axes = plt.subplots(1, 2, figsize=(13, 5))

    bp1 = axes[0].boxplot(rss_data, patch_artist=True, tick_labels=modes, showfliers=False)
    for patch, mode in zip(bp1["boxes"], modes):
        patch.set_facecolor(MODE_COLORS.get(mode, "#cccccc"))
    axes[0].set_title("RSS Distribution by Mode")
    axes[0].set_ylabel("time_max_rss_kb")
    axes[0].grid(True, axis="y", alpha=0.3)

    bp2 = axes[1].boxplot(miss_rate_data, patch_artist=True, tick_labels=modes, showfliers=False)
    for patch, mode in zip(bp2["boxes"], modes):
        patch.set_facecolor(MODE_COLORS.get(mode, "#cccccc"))
    axes[1].set_title("Cache Miss Rate by Mode")
    axes[1].set_ylabel("perf_cache_misses / perf_cache_references")
    axes[1].grid(True, axis="y", alpha=0.3)

    for ax in axes:
        for tick in ax.get_xticklabels():
            tick.set_rotation(20)

    fig.tight_layout()
    fig.savefig(outdir / "08_resource_boxplots.png", dpi=160)
    plt.close(fig)


def write_plot_index(outdir: Path) -> None:
    lines = [
        "# Plot Index",
        "",
        "- 01_runtime_vs_threads_by_preset.png: Runtime scaling by threads for each matrix-chain preset.",
        "- 02_speedup_vs_classic_by_thread.png: Geometric mean speedup vs classic by thread count.",
        "- 03_best_mode_heatmap.png: Winning mode for each preset x thread scenario.",
        "- 04_wins_overall_and_by_thread.png: Total wins and per-thread win breakdown.",
        "- 05_stability_ratio_by_mode.png: Stability ratio (avg/best) by mode and thread.",
        "- 06_efficiency_scatter.png: Runtime vs context switches and runtime vs memory.",
        "- 07_gflops_by_thread.png: Effective GFLOP/s by mode across thread counts.",
        "- 08_resource_boxplots.png: RSS and cache miss-rate distributions by mode.",
    ]
    (outdir / "PLOTS.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate plots for matrix-chain summary.csv")
    parser.add_argument("csv", type=Path, help="Path to summary.csv")
    parser.add_argument("--outdir", type=Path, default=None, help="Output directory for plots")
    args = parser.parse_args()

    csv_path = args.csv
    outdir = args.outdir if args.outdir is not None else (csv_path.parent / "plots")
    outdir.mkdir(parents=True, exist_ok=True)

    rows_all = load_rows(csv_path)
    rows = valid_rows(rows_all)
    if not rows:
        raise SystemExit("No valid rows with bench_best_s > 0.")

    plot_runtime_vs_threads_by_preset(rows, outdir)
    plot_speedup_vs_classic(rows, outdir)
    plot_best_mode_heatmap(rows, outdir)
    plot_wins(rows, outdir)
    plot_stability(rows, outdir)
    plot_efficiency_scatter(rows, outdir)
    plot_gflops(rows, outdir)
    plot_resource_boxplots(rows, outdir)
    write_plot_index(outdir)


if __name__ == "__main__":
    main()
