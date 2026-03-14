#!/usr/bin/env python3
"""Generate matrix multiplication performance plots from summary.csv.

Plot drawing uses only the Python standard library. PNG export is supported
when a rasterizer is available in the active environment.

Usage:
  python3 plot_matrix_mul_summary.py /path/to/summary.csv
  python3 plot_matrix_mul_summary.py /path/to/summary.csv --outdir /path/to/plots
"""

from __future__ import annotations

import argparse
import csv
import importlib.util
import math
import shutil
import subprocess
import tempfile
from collections import Counter, defaultdict
from html import escape
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


MODE_ORDER = ["classic", "coro", "ws", "advws", "elastic"]
MODE_COLORS = {
    "classic": "#1f77b4",
    "coro": "#2ca02c",
    "ws": "#d62728",
    "advws": "#ff7f0e",
    "elastic": "#9467bd",
}
PRESET_ORDER = ["light", "mid", "heavy"]

OUTPUT_MODE = "svg"
PNG_SCALE = 4.0
PNG_BACKEND: Optional[str] = None


class SvgCanvas:
    def __init__(self, width: int, height: int) -> None:
        self.width = width
        self.height = height
        self.parts: List[str] = [
            '<?xml version="1.0" encoding="UTF-8"?>',
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
            f'<rect x="0" y="0" width="{width}" height="{height}" fill="white"/>',
        ]

    def line(self, x1: float, y1: float, x2: float, y2: float, color: str = "#000", width: float = 1.0, dash: str = "") -> None:
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(
            f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" '
            f'stroke="{color}" stroke-width="{width:.2f}"{dash_attr}/>'
        )

    def rect(
        self,
        x: float,
        y: float,
        w: float,
        h: float,
        fill: str = "none",
        stroke: str = "#000",
        stroke_width: float = 1.0,
        rx: float = 0.0,
    ) -> None:
        self.parts.append(
            f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width:.2f}" rx="{rx:.2f}"/>'
        )

    def circle(self, cx: float, cy: float, r: float, fill: str = "#000", stroke: str = "none", stroke_width: float = 1.0) -> None:
        self.parts.append(
            f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}" fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width:.2f}"/>'
        )

    def polyline(self, points: List[Tuple[float, float]], color: str = "#000", width: float = 1.5) -> None:
        if len(points) < 2:
            return
        pts = " ".join(f"{x:.2f},{y:.2f}" for x, y in points)
        self.parts.append(
            f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="{width:.2f}" stroke-linejoin="round" stroke-linecap="round"/>'
        )

    def text(
        self,
        x: float,
        y: float,
        content: str,
        size: int = 12,
        color: str = "#000",
        anchor: str = "start",
        weight: str = "normal",
    ) -> None:
        self.parts.append(
            f'<text x="{x:.2f}" y="{y:.2f}" fill="{color}" font-size="{size}" font-family="Arial, sans-serif" '
            f'text-anchor="{anchor}" font-weight="{weight}">{escape(content)}</text>'
        )

    def render(self) -> str:
        return "\n".join(self.parts + ["</svg>"]) + "\n"

    def save(self, path: Path) -> None:
        path.write_text(self.render(), encoding="utf-8")


def detect_png_backend() -> Optional[str]:
    if importlib.util.find_spec("cairosvg") is not None:
        return "cairosvg"
    if shutil.which("rsvg-convert"):
        return "rsvg-convert"
    if shutil.which("inkscape"):
        return "inkscape"
    return None


def resolve_output_mode(requested: str) -> Tuple[str, Optional[str]]:
    backend = detect_png_backend()
    if requested == "auto":
        return ("png", backend) if backend else ("svg", None)
    if requested == "svg":
        return "svg", None
    if requested == "png":
        if backend is None:
            raise SystemExit("PNG output requested, but no rasterizer is available. Install cairosvg, rsvg-convert, or inkscape.")
        return "png", backend
    if requested == "both":
        if backend is None:
            raise SystemExit("Both output formats requested, but no PNG rasterizer is available. Install cairosvg, rsvg-convert, or inkscape.")
        return "both", backend
    raise SystemExit(f"Unsupported output format: {requested}")


def rasterize_svg(svg_path: Path, png_path: Path, scale: float, backend: str) -> None:
    if backend == "cairosvg":
        import cairosvg

        cairosvg.svg2png(url=str(svg_path), write_to=str(png_path), scale=scale)
        return

    if backend == "rsvg-convert":
        subprocess.run(
            ["rsvg-convert", "-f", "png", "-o", str(png_path), "-z", str(scale), str(svg_path)],
            check=True,
        )
        return

    if backend == "inkscape":
        subprocess.run(
            [
                "inkscape",
                str(svg_path),
                "--export-type=png",
                f"--export-filename={png_path}",
                f"--export-dpi={96.0 * scale}",
            ],
            check=True,
        )
        return

    raise SystemExit(f"Unsupported PNG backend: {backend}")


def save_plot(canvas: SvgCanvas, outdir: Path, stem: str) -> None:
    svg_path = outdir / f"{stem}.svg"
    png_path = outdir / f"{stem}.png"

    if OUTPUT_MODE in {"svg", "both"}:
        canvas.save(svg_path)

    if OUTPUT_MODE in {"png", "both"}:
        if PNG_BACKEND is None:
            raise SystemExit("PNG output selected without an available rasterizer backend.")

        if OUTPUT_MODE == "both":
            rasterize_svg(svg_path, png_path, PNG_SCALE, PNG_BACKEND)
            return

        with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False, encoding="utf-8", dir=outdir) as tmp:
            tmp_path = Path(tmp.name)
            tmp.write(canvas.render())

        try:
            rasterize_svg(tmp_path, png_path, PNG_SCALE, PNG_BACKEND)
        finally:
            tmp_path.unlink(missing_ok=True)


def plot_ref_name(stem: str) -> str:
    if OUTPUT_MODE == "png":
        return f"{stem}.png"
    if OUTPUT_MODE == "both":
        return f"{stem}.svg / {stem}.png"
    return f"{stem}.svg"


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


def avg(values: List[float]) -> Optional[float]:
    vals = [v for v in values if math.isfinite(v)]
    if not vals:
        return None
    return sum(vals) / len(vals)


def fmt_tick(v: float) -> str:
    if v == 0:
        return "0"
    if abs(v) >= 100:
        return f"{v:.0f}"
    if abs(v) >= 10:
        return f"{v:.1f}"
    if abs(v) >= 1:
        return f"{v:.2f}"
    if abs(v) >= 0.1:
        return f"{v:.3f}"
    return f"{v:.4f}"


def linear_ticks(vmin: float, vmax: float, n: int = 5) -> List[float]:
    if not math.isfinite(vmin) or not math.isfinite(vmax):
        return [0.0, 1.0]
    if vmax <= vmin:
        return [vmin, vmin + 1.0]
    step = (vmax - vmin) / max(1, n - 1)
    return [vmin + i * step for i in range(n)]


def log_ticks(vmin: float, vmax: float) -> List[float]:
    if vmin <= 0 or vmax <= 0:
        return [1.0, 10.0]
    p0 = int(math.floor(math.log10(vmin)))
    p1 = int(math.ceil(math.log10(vmax)))
    ticks: List[float] = []
    for p in range(p0, p1 + 1):
        base = 10 ** p
        for m in (1, 2, 5):
            t = m * base
            if vmin <= t <= vmax:
                ticks.append(float(t))
    if len(ticks) > 10:
        ticks = [float(10**p) for p in range(p0, p1 + 1)]
    if not ticks:
        ticks = [vmin, vmax]
    return sorted(set(ticks))


def quantile(sorted_vals: List[float], q: float) -> float:
    if not sorted_vals:
        return float("nan")
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    pos = (len(sorted_vals) - 1) * q
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return sorted_vals[lo]
    frac = pos - lo
    return sorted_vals[lo] * (1.0 - frac) + sorted_vals[hi] * frac


def load_rows(csv_path: Path) -> List[Dict[str, object]]:
    rows: List[Dict[str, object]] = []
    with csv_path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(
                {
                    "preset": r.get("preset", ""),
                    "mode": r.get("mode", ""),
                    "n": to_int(r.get("n", "0")),
                    "threads": to_int(r.get("threads", "0")),
                    "exit_code": to_int(r.get("exit_code", "1")),
                    "best_s": to_float(r.get("best_s", "")),
                    "avg_s": to_float(r.get("avg_s", "")),
                }
            )
    return rows


def valid_rows(rows: List[Dict[str, object]]) -> List[Dict[str, object]]:
    return [
        r
        for r in rows
        if int(r["exit_code"]) == 0 and float(r["best_s"]) > 0 and math.isfinite(float(r["best_s"]))
    ]


def modes_present(rows: List[Dict[str, object]]) -> List[str]:
    present = sorted({str(r["mode"]) for r in rows})
    ordered = [m for m in MODE_ORDER if m in present]
    ordered.extend([m for m in present if m not in ordered])
    return ordered


def presets_present(rows: List[Dict[str, object]]) -> List[str]:
    present = sorted({str(r["preset"]) for r in rows})
    ordered = [p for p in PRESET_ORDER if p in present]
    ordered.extend([p for p in present if p not in ordered])
    return ordered


def scenario_groups(rows: List[Dict[str, object]]) -> Dict[Tuple[str, int], List[Dict[str, object]]]:
    groups: Dict[Tuple[str, int], List[Dict[str, object]]] = defaultdict(list)
    for r in rows:
        key = (str(r["preset"]), int(r["threads"]))
        groups[key].append(r)
    return groups


def draw_legend(c: SvgCanvas, modes: List[str], x: float, y: float, per_row: int = 5, x_gap: int = 165) -> None:
    for i, mode in enumerate(modes):
        row = i // per_row
        col = i % per_row
        lx = x + col * x_gap
        ly = y + row * 22
        color = MODE_COLORS.get(mode, "#555555")
        c.line(lx, ly - 5, lx + 18, ly - 5, color=color, width=2.5)
        c.circle(lx + 9, ly - 5, 3, fill=color)
        c.text(lx + 24, ly - 1, mode, size=13)


def draw_line_plot_area(
    c: SvgCanvas,
    x: float,
    y: float,
    w: float,
    h: float,
    title: str,
    x_vals: List[int],
    series: Dict[str, List[Optional[float]]],
    y_label: str,
    y_log: bool = False,
    y_ref: Optional[float] = None,
    show_y_labels: bool = True,
    fixed_y_range: Optional[Tuple[float, float]] = None,
) -> None:
    c.rect(x, y, w, h, fill="none", stroke="#999999")

    all_y = [v for vals in series.values() for v in vals if v is not None and math.isfinite(v)]
    if fixed_y_range is not None:
        y_min, y_max = fixed_y_range
    elif all_y:
        y_min, y_max = min(all_y), max(all_y)
    else:
        y_min, y_max = 0.0, 1.0

    if y_log:
        y_min = max(y_min, 1e-9)
        y_max = max(y_max, y_min * 1.05)
        y_ticks = log_ticks(y_min, y_max)
        ly0 = math.log10(y_min)
        ly1 = math.log10(y_max)

        def y_to_px(v: float) -> float:
            lv = math.log10(max(v, 1e-12))
            t = 0.0 if ly1 == ly0 else (lv - ly0) / (ly1 - ly0)
            return y + h - t * h

    else:
        if y_max <= y_min:
            y_max = y_min + 1.0
        pad = (y_max - y_min) * 0.08
        y_min -= pad
        y_max += pad
        y_ticks = linear_ticks(y_min, y_max, n=6)

        def y_to_px(v: float) -> float:
            t = 0.0 if y_max == y_min else (v - y_min) / (y_max - y_min)
            return y + h - t * h

    n = len(x_vals)

    def x_to_px(i: int) -> float:
        if n <= 1:
            return x + w / 2.0
        return x + (i / (n - 1)) * w

    for yt in y_ticks:
        py = y_to_px(yt)
        c.line(x, py, x + w, py, color="#e6e6e6", width=1)
        if show_y_labels:
            c.text(x - 8, py + 4, fmt_tick(yt), size=11, color="#444444", anchor="end")

    for i, xv in enumerate(x_vals):
        px = x_to_px(i)
        c.line(px, y, px, y + h, color="#f1f1f1", width=1)
        c.text(px, y + h + 18, str(xv), size=11, color="#333333", anchor="middle")

    if y_ref is not None and (not y_log or y_ref > 0):
        py = y_to_px(y_ref)
        c.line(x, py, x + w, py, color="#333333", width=1.2, dash="5,4")

    c.line(x, y + h, x + w, y + h, color="#666666", width=1.3)
    c.line(x, y, x, y + h, color="#666666", width=1.3)

    for mode in modes_present_from_series(series):
        color = MODE_COLORS.get(mode, "#333333")
        pts: List[Tuple[float, float]] = []
        vals = series.get(mode, [])
        for i, val in enumerate(vals):
            if val is None or not math.isfinite(val):
                continue
            if y_log and val <= 0:
                continue
            pts.append((x_to_px(i), y_to_px(val)))
        c.polyline(pts, color=color, width=2.2)
        for px, py in pts:
            c.circle(px, py, 2.8, fill=color)

    c.text(x + w / 2.0, y - 12, title, size=13, anchor="middle", weight="bold")
    c.text(x + w / 2.0, y + h + 38, "threads", size=12, anchor="middle")
    if show_y_labels:
        c.text(x - 12, y - 12, y_label, size=11, color="#444444", anchor="end")


def modes_present_from_series(series: Dict[str, List[Optional[float]]]) -> List[str]:
    present = [m for m in MODE_ORDER if m in series]
    present.extend([m for m in series.keys() if m not in present])
    return present


def plot_runtime_vs_threads_by_preset(rows: List[Dict[str, object]], outdir: Path) -> None:
    presets = presets_present(rows)
    modes = modes_present(rows)
    threads = sorted({int(r["threads"]) for r in rows})

    if not presets:
        return

    panel_w = 360
    panel_h = 300
    left = 70
    top = 90
    gap = 35
    width = left + len(presets) * panel_w + (len(presets) - 1) * gap + 80
    height = 520
    c = SvgCanvas(width, height)
    c.text(width / 2.0, 36, "Matrix Multiplication Runtime Scaling by Threads", size=20, anchor="middle", weight="bold")

    all_best = [float(r["best_s"]) for r in rows if float(r["best_s"]) > 0]
    y_range = (min(all_best), max(all_best))

    for i, preset in enumerate(presets):
        x0 = left + i * (panel_w + gap)
        series: Dict[str, List[Optional[float]]] = {}
        for mode in modes:
            vals: List[Optional[float]] = []
            for t in threads:
                matched = [float(r["best_s"]) for r in rows if r["preset"] == preset and r["mode"] == mode and r["threads"] == t]
                vals.append(avg(matched) if matched else None)
            series[mode] = vals
        draw_line_plot_area(
            c=c,
            x=x0,
            y=top,
            w=panel_w,
            h=panel_h,
            title=preset,
            x_vals=threads,
            series=series,
            y_label="best_s (s, log)",
            y_log=True,
            show_y_labels=(i == 0),
            fixed_y_range=y_range,
        )

    draw_legend(c, modes, x=80, y=448, per_row=5, x_gap=140)
    save_plot(c, outdir, "01_runtime_vs_threads_by_preset")


def plot_speedup_vs_classic(rows: List[Dict[str, object]], outdir: Path) -> None:
    groups = scenario_groups(rows)
    threads = sorted({int(r["threads"]) for r in rows})
    modes = [m for m in modes_present(rows) if m != "classic"]

    series: Dict[str, List[Optional[float]]] = {}
    for mode in modes:
        vals: List[Optional[float]] = []
        for t in threads:
            ratios: List[float] = []
            for (_preset, th), rs in groups.items():
                if th != t:
                    continue
                by_mode = {str(r["mode"]): r for r in rs}
                c = by_mode.get("classic")
                x = by_mode.get(mode)
                if not c or not x:
                    continue
                tc = float(c["best_s"])
                tx = float(x["best_s"])
                if tc > 0 and tx > 0:
                    ratios.append(tc / tx)
            vals.append(gmean(ratios))
        series[mode] = vals

    c = SvgCanvas(930, 520)
    c.text(465, 34, "Matrix Multiplication Speedup vs Classic", size=20, anchor="middle", weight="bold")
    draw_line_plot_area(
        c=c,
        x=90,
        y=90,
        w=700,
        h=300,
        title="Geometric mean speedup by thread",
        x_vals=threads,
        series=series,
        y_label="speedup",
        y_log=False,
        y_ref=1.0,
        show_y_labels=True,
    )
    draw_legend(c, modes, x=120, y=448, per_row=4, x_gap=170)
    save_plot(c, outdir, "02_speedup_vs_classic_by_thread")


def plot_best_mode_heatmap(rows: List[Dict[str, object]], outdir: Path) -> None:
    groups = scenario_groups(rows)
    presets = presets_present(rows)
    threads = sorted({int(r["threads"]) for r in rows})
    modes = modes_present(rows)

    c = SvgCanvas(980, 560)
    c.text(490, 34, "Best Mode per Preset x Thread", size=20, anchor="middle", weight="bold")

    left = 170
    top = 90
    cell_w = 110
    cell_h = 95

    for i, t in enumerate(threads):
        c.text(left + i * cell_w + cell_w / 2.0, top - 14, f"t{t}", size=13, anchor="middle", weight="bold")
    for j, preset in enumerate(presets):
        c.text(left - 12, top + j * cell_h + cell_h / 2.0 + 5, preset, size=13, anchor="end", weight="bold")

    for j, preset in enumerate(presets):
        for i, t in enumerate(threads):
            rs = groups.get((preset, t), [])
            fill = "#f5f5f5"
            label = "-"
            if rs:
                winner = min(rs, key=lambda r: float(r["best_s"]))
                mode = str(winner["mode"])
                fill = MODE_COLORS.get(mode, "#bbbbbb")
                label = mode
            x = left + i * cell_w
            y = top + j * cell_h
            c.rect(x, y, cell_w, cell_h, fill=fill, stroke="#ffffff", stroke_width=2)
            c.text(x + cell_w / 2.0, y + cell_h / 2.0 + 4, label, size=12, anchor="middle", color="#111111", weight="bold")

    c.rect(left, top, cell_w * len(threads), cell_h * len(presets), fill="none", stroke="#555555", stroke_width=1.5)
    c.text(left + cell_w * len(threads) / 2.0, top + cell_h * len(presets) + 30, "threads", size=12, anchor="middle")
    c.text(72, top + cell_h * len(presets) / 2.0, "preset", size=12, anchor="middle")

    c.text(800, 92, "Legend", size=13, weight="bold")
    draw_legend(c, modes, x=790, y=116, per_row=1, x_gap=140)
    save_plot(c, outdir, "03_best_mode_heatmap")


def plot_wins(rows: List[Dict[str, object]], outdir: Path) -> None:
    groups = scenario_groups(rows)
    modes = modes_present(rows)
    threads = sorted({int(r["threads"]) for r in rows})

    overall = Counter()
    by_thread: Dict[int, Counter] = defaultdict(Counter)
    for (_preset, t), rs in groups.items():
        winner = min(rs, key=lambda r: float(r["best_s"]))
        mode = str(winner["mode"])
        overall[mode] += 1
        by_thread[t][mode] += 1

    c = SvgCanvas(1100, 560)
    c.text(550, 34, "Scenario Wins", size=20, anchor="middle", weight="bold")

    # Left chart: overall bar chart
    lx, ly, lw, lh = 80, 90, 420, 320
    c.rect(lx, ly, lw, lh, fill="none", stroke="#999999")
    max_left = max(1, max((overall[m] for m in modes), default=1))
    y_ticks = linear_ticks(0, float(max_left), 6)
    for yt in y_ticks:
        py = ly + lh - (0 if max_left == 0 else (yt / max_left) * lh)
        c.line(lx, py, lx + lw, py, color="#ececec")
        c.text(lx - 8, py + 4, fmt_tick(yt), size=11, anchor="end", color="#444444")
    n = len(modes)
    bw = lw / max(1, n)
    for i, mode in enumerate(modes):
        v = overall[mode]
        h = 0 if max_left == 0 else (v / max_left) * lh
        x = lx + i * bw + 10
        c.rect(x, ly + lh - h, bw - 20, h, fill=MODE_COLORS.get(mode, "#777777"), stroke="none")
        c.text(x + (bw - 20) / 2.0, ly + lh + 18, mode, size=11, anchor="middle")
    c.text(lx + lw / 2.0, ly - 10, "Total Wins", size=13, anchor="middle", weight="bold")

    # Right chart: stacked wins by thread
    rx, ry, rw, rh = 590, 90, 420, 320
    c.rect(rx, ry, rw, rh, fill="none", stroke="#999999")
    max_right = max(1, max((sum(by_thread[t].values()) for t in threads), default=1))
    y_ticks = linear_ticks(0, float(max_right), 6)
    for yt in y_ticks:
        py = ry + rh - (0 if max_right == 0 else (yt / max_right) * rh)
        c.line(rx, py, rx + rw, py, color="#ececec")
        c.text(rx - 8, py + 4, fmt_tick(yt), size=11, anchor="end", color="#444444")

    n_t = len(threads)
    bw = rw / max(1, n_t)
    for i, t in enumerate(threads):
        x = rx + i * bw + 12
        width = bw - 24
        bottom = ry + rh
        for mode in modes:
            v = by_thread[t][mode]
            h = 0 if max_right == 0 else (v / max_right) * rh
            if h > 0:
                c.rect(x, bottom - h, width, h, fill=MODE_COLORS.get(mode, "#777777"), stroke="none")
                bottom -= h
        c.text(x + width / 2.0, ry + rh + 18, f"t{t}", size=11, anchor="middle")
    c.text(rx + rw / 2.0, ry - 10, "Wins by Thread", size=13, anchor="middle", weight="bold")

    draw_legend(c, modes, x=120, y=458, per_row=5, x_gap=170)
    save_plot(c, outdir, "04_wins_overall_and_by_thread")


def plot_stability(rows: List[Dict[str, object]], outdir: Path) -> None:
    modes = modes_present(rows)
    threads = sorted({int(r["threads"]) for r in rows})

    series: Dict[str, List[Optional[float]]] = {}
    for mode in modes:
        vals: List[Optional[float]] = []
        for t in threads:
            ratios = [
                float(r["avg_s"]) / float(r["best_s"])
                for r in rows
                if r["mode"] == mode and r["threads"] == t and float(r["best_s"]) > 0 and float(r["avg_s"]) > 0
            ]
            vals.append(avg(ratios))
        series[mode] = vals

    c = SvgCanvas(930, 520)
    c.text(465, 34, "Runtime Stability by Mode", size=20, anchor="middle", weight="bold")
    draw_line_plot_area(
        c=c,
        x=90,
        y=90,
        w=700,
        h=300,
        title="avg_s / best_s by thread",
        x_vals=threads,
        series=series,
        y_label="ratio",
        y_log=False,
        y_ref=1.0,
        show_y_labels=True,
    )
    draw_legend(c, modes, x=120, y=448, per_row=5, x_gap=140)
    save_plot(c, outdir, "05_stability_ratio_by_mode")


def plot_scaling_efficiency(rows: List[Dict[str, object]], outdir: Path) -> None:
    presets = presets_present(rows)
    modes = modes_present(rows)
    threads = sorted({int(r["threads"]) for r in rows})

    t1_map: Dict[Tuple[str, str], float] = {}
    for preset in presets:
        for mode in modes:
            vals = [float(r["best_s"]) for r in rows if r["preset"] == preset and r["mode"] == mode and r["threads"] == 1]
            if vals:
                t1_map[(preset, mode)] = vals[0]

    series: Dict[str, List[Optional[float]]] = {}
    for mode in modes:
        vals: List[Optional[float]] = []
        for t in threads:
            effs: List[float] = []
            for preset in presets:
                b = t1_map.get((preset, mode))
                cur = [float(r["best_s"]) for r in rows if r["preset"] == preset and r["mode"] == mode and r["threads"] == t]
                if not b or not cur:
                    continue
                if b > 0 and cur[0] > 0:
                    speedup = b / cur[0]
                    effs.append(speedup / t)
            vals.append(gmean(effs))
        series[mode] = vals

    c = SvgCanvas(930, 520)
    c.text(465, 34, "Scaling Efficiency vs Ideal", size=20, anchor="middle", weight="bold")
    draw_line_plot_area(
        c=c,
        x=90,
        y=90,
        w=700,
        h=300,
        title="efficiency = (t1/time_t) / threads",
        x_vals=threads,
        series=series,
        y_label="efficiency",
        y_log=False,
        y_ref=1.0,
        show_y_labels=True,
    )
    draw_legend(c, modes, x=120, y=448, per_row=5, x_gap=140)
    save_plot(c, outdir, "06_scaling_efficiency_by_thread")


def plot_gflops(rows: List[Dict[str, object]], outdir: Path) -> None:
    modes = modes_present(rows)
    threads = sorted({int(r["threads"]) for r in rows})

    series: Dict[str, List[Optional[float]]] = {}
    for mode in modes:
        vals: List[Optional[float]] = []
        for t in threads:
            gvals: List[float] = []
            for r in rows:
                if r["mode"] != mode or r["threads"] != t:
                    continue
                n = float(r["n"])
                best_s = float(r["best_s"])
                if n > 0 and best_s > 0:
                    gvals.append((2.0 * n * n * n) / best_s / 1e9)
            vals.append(avg(gvals))
        series[mode] = vals

    c = SvgCanvas(930, 520)
    c.text(465, 34, "Compute Throughput by Mode", size=20, anchor="middle", weight="bold")
    draw_line_plot_area(
        c=c,
        x=90,
        y=90,
        w=700,
        h=300,
        title="Approx GFLOP/s from best_s",
        x_vals=threads,
        series=series,
        y_label="GFLOP/s",
        y_log=False,
        show_y_labels=True,
    )
    draw_legend(c, modes, x=120, y=448, per_row=5, x_gap=140)
    save_plot(c, outdir, "07_gflops_by_thread")


def plot_runtime_boxplots(rows: List[Dict[str, object]], outdir: Path) -> None:
    modes = modes_present(rows)
    vals_by_mode: Dict[str, List[float]] = {}
    for mode in modes:
        vals = sorted(float(r["best_s"]) for r in rows if r["mode"] == mode and float(r["best_s"]) > 0)
        vals_by_mode[mode] = vals

    all_vals = [v for vals in vals_by_mode.values() for v in vals]
    y_min = max(min(all_vals), 1e-9)
    y_max = max(all_vals)
    ticks = log_ticks(y_min, y_max)
    ly0 = math.log10(y_min)
    ly1 = math.log10(y_max)

    def y_to_px(v: float, top: float, h: float) -> float:
        lv = math.log10(max(v, 1e-12))
        t = 0.0 if ly1 == ly0 else (lv - ly0) / (ly1 - ly0)
        return top + h - t * h

    c = SvgCanvas(930, 520)
    c.text(465, 34, "Runtime Distribution by Mode", size=20, anchor="middle", weight="bold")

    x0, y0, w, h = 90, 90, 700, 300
    c.rect(x0, y0, w, h, fill="none", stroke="#999999")

    for t in ticks:
        py = y_to_px(t, y0, h)
        c.line(x0, py, x0 + w, py, color="#ececec")
        c.text(x0 - 8, py + 4, fmt_tick(t), size=11, anchor="end", color="#444444")

    n = len(modes)
    slot = w / max(1, n)
    for i, mode in enumerate(modes):
        vals = vals_by_mode.get(mode, [])
        if not vals:
            continue
        q1 = quantile(vals, 0.25)
        q2 = quantile(vals, 0.50)
        q3 = quantile(vals, 0.75)
        lo = vals[0]
        hi = vals[-1]

        cx = x0 + i * slot + slot / 2.0
        bw = slot * 0.46
        color = MODE_COLORS.get(mode, "#777777")

        y_lo = y_to_px(lo, y0, h)
        y_q1 = y_to_px(q1, y0, h)
        y_q2 = y_to_px(q2, y0, h)
        y_q3 = y_to_px(q3, y0, h)
        y_hi = y_to_px(hi, y0, h)

        c.line(cx, y_hi, cx, y_q3, color="#555555")
        c.line(cx, y_q1, cx, y_lo, color="#555555")
        c.line(cx - bw * 0.3, y_hi, cx + bw * 0.3, y_hi, color="#555555")
        c.line(cx - bw * 0.3, y_lo, cx + bw * 0.3, y_lo, color="#555555")
        c.rect(cx - bw / 2.0, y_q3, bw, max(1.0, y_q1 - y_q3), fill=color, stroke="#333333", stroke_width=1)
        c.line(cx - bw / 2.0, y_q2, cx + bw / 2.0, y_q2, color="#111111", width=1.5)
        c.text(cx, y0 + h + 18, mode, size=11, anchor="middle")

    c.text(x0 + w / 2.0, y0 - 10, "best_s distribution (log scale)", size=13, anchor="middle", weight="bold")
    c.text(62, y0 - 10, "seconds", size=11)
    save_plot(c, outdir, "08_runtime_boxplots")


def write_plot_index(outdir: Path) -> None:
    lines = [
        "# Plot Index",
        "",
        f"- {plot_ref_name('01_runtime_vs_threads_by_preset')}: Runtime scaling by threads for each matrix-size preset.",
        f"- {plot_ref_name('02_speedup_vs_classic_by_thread')}: Geometric mean speedup vs classic by thread count.",
        f"- {plot_ref_name('03_best_mode_heatmap')}: Winning mode for each preset x thread scenario.",
        f"- {plot_ref_name('04_wins_overall_and_by_thread')}: Total wins and per-thread win breakdown.",
        f"- {plot_ref_name('05_stability_ratio_by_mode')}: Stability ratio (avg/best) by mode and thread.",
        f"- {plot_ref_name('06_scaling_efficiency_by_thread')}: Speedup/threads relative to each mode's t1 baseline.",
        f"- {plot_ref_name('07_gflops_by_thread')}: Approximate GFLOP/s by mode across thread counts.",
        f"- {plot_ref_name('08_runtime_boxplots')}: Runtime distribution of best_s by mode.",
    ]
    (outdir / "PLOTS.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate matrix multiplication plots as SVG or PNG.")
    parser.add_argument("csv", type=Path, help="Path to summary.csv")
    parser.add_argument("--outdir", type=Path, default=None, help="Output directory for plots")
    parser.add_argument(
        "--format",
        choices=["auto", "svg", "png", "both"],
        default="auto",
        help="Output format. 'auto' prefers PNG when a rasterizer is available, otherwise falls back to SVG.",
    )
    parser.add_argument(
        "--png-scale",
        type=float,
        default=4.0,
        help="Rasterization scale factor used for PNG output.",
    )
    args = parser.parse_args()

    global OUTPUT_MODE, PNG_SCALE, PNG_BACKEND
    OUTPUT_MODE, PNG_BACKEND = resolve_output_mode(args.format)
    PNG_SCALE = args.png_scale

    csv_path = args.csv
    outdir = args.outdir if args.outdir is not None else (csv_path.parent / "plots")
    outdir.mkdir(parents=True, exist_ok=True)

    rows_all = load_rows(csv_path)
    rows = valid_rows(rows_all)
    if not rows:
        raise SystemExit("No valid rows with exit_code=0 and best_s > 0.")

    plot_runtime_vs_threads_by_preset(rows, outdir)
    plot_speedup_vs_classic(rows, outdir)
    plot_best_mode_heatmap(rows, outdir)
    plot_wins(rows, outdir)
    plot_stability(rows, outdir)
    plot_scaling_efficiency(rows, outdir)
    plot_gflops(rows, outdir)
    plot_runtime_boxplots(rows, outdir)
    write_plot_index(outdir)


if __name__ == "__main__":
    main()
