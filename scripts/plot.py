#!/usr/bin/env python3
"""Draw a per-thread CPU heatmap from a parprof timeline CSV.

Each row is a thread, each column a sampling interval; darker means busier.
Load imbalance shows up as rows that go pale early; lock contention as rows
that never get dark.

usage: plot.py timeline.csv [-o timeline.png] [--title TEXT]
"""
import argparse
import csv
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")  # render to a file; no display needed
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap
import numpy as np

# One hue, light to dark: pale = idle, dark blue = 100% CPU.
CMAP = LinearSegmentedColormap.from_list("cpu", ["#f4f8fd", "#9ec5f4", "#3987e5", "#1c5cab", "#0d366b"])
CMAP.set_bad("#d4d4d4")  # gray = thread not alive at that time


def load(path):
    samples = defaultdict(dict)  # tid -> {t_ms: cpu_pct}
    with open(path) as f:
        for row in csv.DictReader(f):
            samples[int(row["tid"])][float(row["t_ms"])] = float(row["cpu_pct"])
    return samples


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("csv")
    parser.add_argument("-o", "--output", default="timeline.png")
    parser.add_argument("--title", default="CPU use per thread")
    args = parser.parse_args()

    samples = load(args.csv)
    if not samples:
        raise SystemExit(f"{args.csv}: no samples")
    tids = sorted(samples)
    times = sorted({t for per_thread in samples.values() for t in per_thread})
    grid = np.full((len(tids), len(times)), np.nan)  # NaN = thread didn't exist yet / anymore
    column = {t: i for i, t in enumerate(times)}
    for row, tid in enumerate(tids):
        for t, pct in samples[tid].items():
            grid[row, column[t]] = min(pct, 100)

    fig, ax = plt.subplots(figsize=(10, 1.2 + 0.28 * len(tids)))
    image = ax.imshow(grid, aspect="auto", interpolation="nearest", cmap=CMAP, vmin=0, vmax=100,
                      extent=(times[0], times[-1], len(tids) - 0.5, -0.5))
    ax.set_yticks(range(len(tids)), [str(t) for t in tids], fontsize=8)
    ax.set_xlabel("time since start (ms)")
    ax.set_ylabel("thread id")
    ax.set_title(args.title, loc="left")
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    fig.colorbar(image, ax=ax, label="CPU %", fraction=0.03, pad=0.02)
    fig.savefig(args.output, dpi=130, bbox_inches="tight")
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
