#!/usr/bin/env python3
"""
plot_results.py - Read benchmark_results.csv (produced by run_benchmarks.sh)
and produce:
  - speedup_vs_p.png     (Speedup = T_seq/T_mpi(P) vs P, one line per dataset)
  - efficiency_vs_p.png  (Efficiency = Speedup/P vs P)
  - time_vs_n.png        (Wall-clock time vs N, sequential vs MPI at each P)
  - summary_table.csv    (flat table: dataset x P -> time, speedup, efficiency)

CSV columns expected: dataset,N,impl,P,time_seconds  (impl in {seq, mpi})

Usage: python3 plot_results.py [benchmark_results.csv]
"""
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


def main():
    in_csv = sys.argv[1] if len(sys.argv) > 1 else "benchmark_results.csv"

    seq_time = {}          # dataset -> T_seq
    dataset_n = {}         # dataset -> N
    mpi_time = defaultdict(dict)  # dataset -> {P: T_mpi}

    with open(in_csv) as f:
        reader = csv.DictReader(f)
        for row in reader:
            label = row["dataset"]
            dataset_n[label] = int(row["N"])
            if row["impl"] == "seq":
                seq_time[label] = float(row["time_seconds"])
            else:
                mpi_time[label][int(row["P"])] = float(row["time_seconds"])

    fig1, ax1 = plt.subplots(figsize=(7, 5))
    fig2, ax2 = plt.subplots(figsize=(7, 5))
    fig3, ax3 = plt.subplots(figsize=(7, 5))

    summary_rows = []

    for label in sorted(dataset_n, key=lambda l: dataset_n[l]):
        if label not in seq_time:
            print(f"Skipping {label}: no sequential baseline recorded")
            continue
        ts = seq_time[label]
        ps = sorted(mpi_time[label].keys())
        if not ps:
            continue

        speedups = [ts / mpi_time[label][p] for p in ps]
        efficiencies = [s / p for s, p in zip(speedups, ps)]

        ax1.plot(ps, speedups, marker="o", label=f"{label} (N={dataset_n[label]})")
        ax2.plot(ps, efficiencies, marker="o", label=f"{label} (N={dataset_n[label]})")

        for p, s, e in zip(ps, speedups, efficiencies):
            summary_rows.append({
                "dataset": label,
                "N": dataset_n[label],
                "P": p,
                "time_seq": round(ts, 6),
                "time_mpi": round(mpi_time[label][p], 6),
                "speedup": round(s, 4),
                "efficiency": round(e, 4),
            })

    all_ps = sorted({p for d in mpi_time.values() for p in d.keys()})
    if all_ps:
        ax1.plot(all_ps, all_ps, linestyle="--", color="gray", label="ideal")

    ax1.set_xlabel("Number of processes (P)")
    ax1.set_ylabel("Speedup  T_seq / T_mpi(P)")
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

    # Time vs N: sequential line + one line per P.
    ns_sorted = sorted(dataset_n.values())
    labels_by_n = {n: l for l, n in dataset_n.items()}
    seq_series = [seq_time[labels_by_n[n]] for n in ns_sorted if labels_by_n[n] in seq_time]
    ax3.plot(ns_sorted[:len(seq_series)], seq_series, marker="o", label="sequential")
    for p in all_ps:
        series = [mpi_time[labels_by_n[n]].get(p) for n in ns_sorted if labels_by_n[n] in mpi_time]
        if all(v is not None for v in series):
            ax3.plot(ns_sorted, series, marker="o", label=f"mpi P={p}")
    ax3.set_xlabel("N (number of log records)")
    ax3.set_ylabel("Time (seconds)")
    ax3.set_xscale("log")
    ax3.set_yscale("log")
    ax3.set_title("Execution time vs input size")
    ax3.legend(fontsize=8)
    ax3.grid(True, alpha=0.3, which="both")
    fig3.tight_layout()
    fig3.savefig("time_vs_n.png", dpi=150)

    with open("summary_table.csv", "w", newline="") as f:
        writer = csv.DictWriter(
            f, fieldnames=["dataset", "N", "P", "time_seq", "time_mpi", "speedup", "efficiency"])
        writer.writeheader()
        writer.writerows(summary_rows)

    print("Wrote speedup_vs_p.png, efficiency_vs_p.png, time_vs_n.png, summary_table.csv")


if __name__ == "__main__":
    main()
