#!/usr/bin/env python3
import csv
import sys
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    print("matplotlib not installed. Run: pip install matplotlib --break-system-packages")
    sys.exit(1)


def read_csv(path):
    data = defaultdict(dict)
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            label = row["graph"]
            P = int(row["P"])
            if not row.get("time_seconds"):
                continue  # blank/failed run row
            try:
                entry = {
                    "V": int(row["V"]),
                    "E": int(float(row.get("E", 0) or 0)),
                    "iterations": int(row["iterations"]) if row["iterations"] else None,
                    "time": float(row["time_seconds"]),
                    "comp": float(row["comp_time"]) if row.get("comp_time") else None,
                    "comm": float(row["comm_time"]) if row.get("comm_time") else None,
                    "other": float(row["other_time"]) if row.get("other_time") else None,
                }
            except ValueError:
                continue
            data[label][P] = entry
    return data


def plot_speedup_efficiency(data):
    fig1, ax1 = plt.subplots(figsize=(7, 5))
    fig2, ax2 = plt.subplots(figsize=(7, 5))
    fig2b, ax2b = plt.subplots(figsize=(7, 5))

    summary_rows = []

    for label, p_to_e in sorted(data.items()):
        if 1 not in p_to_e:
            print(f"Skipping {label}: no P=1 baseline recorded")
            continue

        t1 = p_to_e[1]["time"]
        ps = sorted(p_to_e.keys())
        speedups = [t1 / p_to_e[p]["time"] for p in ps]
        efficiencies = [s / p for s, p in zip(speedups, ps)]
        efficiencies_pct = [100.0 * e for e in efficiencies]

        ax1.plot(ps, speedups, marker="o", label=label)
        ax2.plot(ps, efficiencies, marker="o", label=label)
        ax2b.plot(ps, efficiencies_pct, marker="o", label=label)

        for p, s, e in zip(ps, speedups, efficiencies):
            entry = p_to_e[p]
            comm_pct = (100.0 * entry["comm"] / entry["time"]
                        if entry["comm"] is not None and entry["time"] else "")
            summary_rows.append({
                "graph": label,
                "P": p,
                "time_seconds": entry["time"],
                "speedup": round(s, 4),
                "efficiency": round(e, 4),
                "comp_time": entry["comp"] if entry["comp"] is not None else "",
                "comm_time": entry["comm"] if entry["comm"] is not None else "",
                "other_time": entry["other"] if entry["other"] is not None else "",
                "comm_overhead_pct": round(comm_pct, 2) if comm_pct != "" else "",
            })

    all_ps = sorted({p for d in data.values() for p in d.keys()})
    if all_ps:
        ax1.plot(all_ps, all_ps, linestyle="--", color="gray", label="ideal")

    ax1.set_xlabel("Number of processes (P)")
    ax1.set_ylabel("Speedup  T(1)/T(P)")
    ax1.set_title("Speedup vs P")
    ax1.legend(fontsize=8)
    ax1.grid(True, alpha=0.3)
    fig1.tight_layout()
    fig1.savefig("speedup_vs_p.png", dpi=150)

    ax2.axhline(1.0, linestyle="--", color="gray", label="ideal")
    ax2.set_xlabel("Number of processes (P)")
    ax2.set_ylabel("Efficiency  Speedup/P")
    ax2.set_title("Efficiency vs P")
    ax2.legend(fontsize=8)
    ax2.grid(True, alpha=0.3)
    fig2.tight_layout()
    fig2.savefig("efficiency_vs_p.png", dpi=150)

    ax2b.axhline(100.0, linestyle="--", color="gray", label="ideal")
    ax2b.set_xlabel("Number of processes (P)")
    ax2b.set_ylabel("Efficiency (%)")
    ax2b.set_title("Efficiency vs P (%)")
    ax2b.legend(fontsize=8)
    ax2b.grid(True, alpha=0.3)
    fig2b.tight_layout()
    fig2b.savefig("efficiency_vs_p_pct.png", dpi=150)

    return summary_rows


def plot_comm_overhead(data):
    has_breakdown = any(
        entry["comm"] is not None
        for p_to_e in data.values()
        for entry in p_to_e.values()
    )
    if not has_breakdown:
        print("Skipping comm_overhead_vs_p.png: CSV has no comm_time column "
              "(recompile q6_mpi.cpp with timing instrumentation and rerun benchmarks)")
        return

    fig, ax = plt.subplots(figsize=(7, 5))
    for label, p_to_e in sorted(data.items()):
        ps = sorted(p_to_e.keys())
        pct, valid_ps = [], []
        for p in ps:
            entry = p_to_e[p]
            if entry["comm"] is not None and entry["time"]:
                pct.append(100.0 * entry["comm"] / entry["time"])
                valid_ps.append(p)
        if valid_ps:
            ax.plot(valid_ps, pct, marker="o", label=label)

    ax.set_xlabel("Number of processes (P)")
    ax.set_ylabel("Communication / total time (%)")
    ax.set_title("Communication Overhead vs P")
    ax.set_ylim(0, 105)
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig("comm_overhead_vs_p.png", dpi=150)


def plot_time_vs_p(data):
    fig, ax = plt.subplots(figsize=(7, 5))
    for label, p_to_e in sorted(data.items()):
        ps = sorted(p_to_e.keys())
        times = [p_to_e[p]["time"] for p in ps]
        ax.plot(ps, times, marker="o", label=label)
    ax.set_yscale("log")
    ax.set_xlabel("Number of processes (P)")
    ax.set_ylabel("Total execution time (s, log scale)")
    ax.set_title("Execution Time vs P")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3, which="both")
    fig.tight_layout()
    fig.savefig("time_vs_p.png", dpi=150)


def plot_time_vs_e(data):
    by_p = defaultdict(list)
    for label, p_to_e in data.items():
        for p, entry in p_to_e.items():
            if entry["E"] > 0:
                by_p[p].append((entry["E"], entry["time"]))

    if not by_p:
        print("Skipping time_vs_e.png: no graphs have an edge count (E) recorded")
        return

    fig, ax = plt.subplots(figsize=(7, 5))
    for p in sorted(by_p.keys()):
        pts = sorted(by_p[p])
        es = [e for e, t in pts]
        ts = [t for e, t in pts]
        ax.plot(es, ts, marker="o", label=f"P = {p}")

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Number of edges (E)")
    ax.set_ylabel("Total execution time (s)")
    ax.set_title("Execution Time vs Number of Edges")
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3, which="both")
    fig.tight_layout()
    fig.savefig("time_vs_e.png", dpi=150)


def main():
    in_csv = sys.argv[1] if len(sys.argv) > 1 else "benchmark_results.csv"

    data = read_csv(in_csv)
    if not data:
        print(f"No usable rows found in {in_csv}")
        sys.exit(1)

    summary_rows = plot_speedup_efficiency(data)
    plot_time_vs_p(data)
    plot_time_vs_e(data)
    plot_comm_overhead(data)

    with open("summary_table.csv", "w", newline="") as f:
        fieldnames = ["graph", "P", "time_seconds", "speedup", "efficiency",
                      "comp_time", "comm_time", "other_time", "comm_overhead_pct"]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(summary_rows)

    print("Wrote speedup_vs_p.png, efficiency_vs_p.png, efficiency_vs_p_pct.png, "
          "time_vs_p.png, time_vs_e.png, comm_overhead_vs_p.png, summary_table.csv")


if __name__ == "__main__":
    main()
