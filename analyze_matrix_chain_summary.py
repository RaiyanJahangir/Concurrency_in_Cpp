#!/usr/bin/env python3
"""Analyze matrix-chain benchmark summary.csv and report per-mode performance.

Usage:
  python analyze_matrix_chain_summary.py /path/to/summary.csv
  python analyze_matrix_chain_summary.py /path/to/summary.csv --out /path/to/report.md
"""

from __future__ import annotations

import argparse
import csv
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


Row = Dict[str, str]
ScenarioKey = Tuple[str, str, int]  # bench, preset, threads


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


def load_rows(csv_path: Path) -> List[Row]:
    with csv_path.open("r", newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def scenario_groups(rows: List[Row], valid_only: bool = True) -> Dict[ScenarioKey, List[Row]]:
    groups: Dict[ScenarioKey, List[Row]] = defaultdict(list)
    for r in rows:
        t = to_float(r.get("bench_best_s", ""))
        if valid_only and not (t > 0 and math.isfinite(t)):
            continue
        key = (r.get("bench", ""), r.get("preset", ""), to_int(r.get("threads", "0")))
        groups[key].append(r)
    return groups


def best_row(rows: List[Row]) -> Row:
    return min(rows, key=lambda r: to_float(r.get("bench_best_s", ""), float("inf")))


def normalized_runtime_by_mode(groups: Dict[ScenarioKey, List[Row]]) -> Dict[str, float]:
    norms: Dict[str, List[float]] = defaultdict(list)
    for rs in groups.values():
        best = to_float(best_row(rs).get("bench_best_s", ""), float("inf"))
        if not (best > 0 and math.isfinite(best)):
            continue
        for r in rs:
            t = to_float(r.get("bench_best_s", ""))
            if t > 0 and math.isfinite(t):
                norms[r.get("mode", "")].append(t / best)
    return {m: sum(v) / len(v) for m, v in norms.items() if v}


def speedup_vs_classic(groups: Dict[ScenarioKey, List[Row]], mode: str) -> Optional[float]:
    ratios: List[float] = []
    for rs in groups.values():
        by_mode = {r.get("mode", ""): r for r in rs}
        c = by_mode.get("classic")
        x = by_mode.get(mode)
        if not c or not x:
            continue
        tc = to_float(c.get("bench_best_s", ""))
        tx = to_float(x.get("bench_best_s", ""))
        if tc > 0 and tx > 0 and math.isfinite(tc) and math.isfinite(tx):
            ratios.append(tc / tx)
    return gmean(ratios)


def speedup_vs_classic_by_filter(
    groups: Dict[ScenarioKey, List[Row]],
    mode: str,
    scenario_filter,
) -> Tuple[Optional[float], int]:
    ratios: List[float] = []
    for key, rs in groups.items():
        if not scenario_filter(key):
            continue
        by_mode = {r.get("mode", ""): r for r in rs}
        c = by_mode.get("classic")
        x = by_mode.get(mode)
        if not c or not x:
            continue
        tc = to_float(c.get("bench_best_s", ""))
        tx = to_float(x.get("bench_best_s", ""))
        if tc > 0 and tx > 0 and math.isfinite(tc) and math.isfinite(tx):
            ratios.append(tc / tx)
    return gmean(ratios), len(ratios)


def markdown_table(headers: List[str], rows: List[List[str]]) -> str:
    out = ["| " + " | ".join(headers) + " |", "|" + "|".join(["---"] * len(headers)) + "|"]
    for r in rows:
        out.append("| " + " | ".join(r) + " |")
    return "\n".join(out)


def build_report(csv_path: Path) -> str:
    rows = load_rows(csv_path)
    groups_all = scenario_groups(rows, valid_only=False)
    groups = scenario_groups(rows, valid_only=True)

    total_rows = len(rows)
    total_scenarios = len(groups_all)
    valid_rows = sum(len(v) for v in groups.values())

    invalid = []
    for r in rows:
        t = to_float(r.get("bench_best_s", ""))
        if not (t > 0 and math.isfinite(t)):
            invalid.append([
                r.get("bench", ""),
                r.get("preset", ""),
                r.get("mode", ""),
                r.get("threads", ""),
                r.get("bench_best_s", ""),
            ])

    winner_counts = Counter()
    winner_counts_by_thread: Dict[int, Counter] = defaultdict(Counter)
    for (bench, preset, threads), rs in groups.items():
        del bench, preset
        winner = best_row(rs).get("mode", "")
        winner_counts[winner] += 1
        winner_counts_by_thread[threads][winner] += 1

    norm = normalized_runtime_by_mode(groups)
    modes = sorted({r.get("mode", "") for r in rows if r.get("mode")})
    speedups = {m: speedup_vs_classic(groups, m) for m in modes if m != "classic"}

    by_level_rows = []
    for lvl in ["light", "mid", "heavy"]:
        gm, n = speedup_vs_classic_by_filter(groups, "coro", lambda key, l=lvl: key[1].endswith("_" + l))
        by_level_rows.append([lvl, f"{gm:.3f}x" if gm else "NA", str(n)])

    gflops_by_mode: Dict[str, List[float]] = defaultdict(list)
    for r in rows:
        g = to_float(r.get("gflops_best", ""))
        if g > 0 and math.isfinite(g):
            gflops_by_mode[r.get("mode", "")].append(g)

    gflops_rows: List[List[str]] = []
    for m in sorted(gflops_by_mode.keys()):
        vals = gflops_by_mode[m]
        avg_g = sum(vals) / len(vals)
        best_g = max(vals)
        gflops_rows.append([m, f"{avg_g:.3f}", f"{best_g:.3f}", str(len(vals))])

    threads = sorted({to_int(r.get("threads", "0")) for r in rows if to_int(r.get("threads", "0")) > 0})
    thread_rows = []
    for t in threads:
        total = sum(winner_counts_by_thread[t].values())
        parts = [f"{m}:{c}" for m, c in winner_counts_by_thread[t].most_common()]
        thread_rows.append([f"t{t}", str(total), ", ".join(parts)])

    report: List[str] = []
    report.append("# Matrix Chain Summary Analysis\n")
    report.append(f"Source: `{csv_path}`")
    report.append(f"Rows: {total_rows}; valid rows (bench_best_s>0): {valid_rows}; scenarios: {total_scenarios}")

    if invalid:
        report.append("\n## Invalid/zero timing rows")
        report.append(markdown_table(["bench", "preset", "mode", "threads", "bench_best_s"], invalid))

    report.append("\n## Scenario Winners (best runtime count)")
    report.append(markdown_table(["mode", "wins"], [[m, str(c)] for m, c in winner_counts.most_common()]))

    report.append("\n## Avg Normalized Runtime by Mode (1.0 = scenario best)")
    norm_rows = [[m, f"{norm[m]:.3f}"] for m in sorted(norm.keys(), key=lambda x: norm[x])]
    report.append(markdown_table(["mode", "avg_norm_runtime"], norm_rows))

    report.append("\n## Geometric Mean Speedup vs Classic (>1 faster)")
    sp_rows = [[m, f"{speedups[m]:.3f}x" if speedups[m] else "NA"] for m in sorted(speedups.keys())]
    report.append(markdown_table(["mode", "speedup_vs_classic"], sp_rows))

    report.append("\n## Coroutine Speedup vs Classic by Preset Level")
    report.append(markdown_table(["level", "coro_speedup_vs_classic", "n_scenarios"], by_level_rows))

    report.append("\n## GFLOP/s (best-time derived)")
    report.append(markdown_table(["mode", "avg_gflops", "best_gflops", "n_rows"], gflops_rows))

    report.append("\n## Winner Counts by Thread")
    report.append(markdown_table(["thread", "scenario_count", "winner_breakdown"], thread_rows))

    return "\n\n".join(report) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyze matrix-chain benchmark summary.csv")
    parser.add_argument("csv", type=Path, help="Path to summary.csv")
    parser.add_argument("--out", type=Path, default=None, help="Optional output markdown report path")
    args = parser.parse_args()

    report = build_report(args.csv)
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(report, encoding="utf-8")
        print(f"Report written to: {args.out}")
    else:
        print(report)


if __name__ == "__main__":
    main()
