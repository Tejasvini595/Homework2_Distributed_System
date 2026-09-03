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


def _to_float(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def main():
    in_csv = sys.argv[1] if len(sys.argv) > 1 else "benchmark_results.csv"

    seq_time = {}          # dataset -> T_seq
    dataset_n = {}         # dataset -> N
    mpi_time = defaultdict(dict)   # dataset -> {P: T_mpi}
    mpi_comp = defaultdict(dict)   # dataset -> {P: comp_time}
    mpi_comm = defaultdict(dict)   # dataset -> {P: comm_time}

    with open(in_csv) as f:
        reader = csv.DictReader(f)
        for row in reader:
            label = row["dataset"]
            dataset_n[label] = int(row["N"])
            if row["impl"] == "seq":
                seq_time[label] = float(row["time_seconds"])
            else:
                p = int(row["P"])
                mpi_time[label][p] = float(row["time_seconds"])
                mpi_comp[label][p] = _to_float(row.get("comp_time"))
                mpi_comm[label][p] = _to_float(row.get("comm_time"))

    fig1, ax1 = plt.subplots(figsize=(7, 5))
    fig2, ax2 = plt.subplots(figsize=(7, 5))
    fig2b, ax2b = plt.subplots(figsize=(7, 5))

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
        efficiencies_pct = [100.0 * e for e in efficiencies]

        series_label = f"{label} (N={dataset_n[label]})"
        ax1.plot(ps, speedups, marker="o", label=series_label)
        ax2.plot(ps, efficiencies, marker="o", label=series_label)
        ax2b.plot(ps, efficiencies_pct, marker="o", label=series_label)

        for p, s, e in zip(ps, speedups, efficiencies):
            comp = mpi_comp[label].get(p)
            comm = mpi_comm[label].get(p)
            t_mpi = mpi_time[label][p]
            other = None
            if comp is not None and comm is not None:
                other = max(0.0, t_mpi - comp - comm)
            comm_pct = (100.0 * comm / t_mpi) if (comm is not None and t_mpi) else ""
            summary_rows.append({
                "dataset": label,
                "N": dataset_n[label],
                "P": p,
                "time_seq": round(ts, 6),
                "time_mpi": round(t_mpi, 6),
                "speedup": round(s, 4),
                "efficiency": round(e, 4),
                "comp_time": round(comp, 6) if comp is not None else "",
                "comm_time": round(comm, 6) if comm is not None else "",
                "other_time": round(other, 6) if other is not None else "",
                "comm_overhead_pct": round(comm_pct, 2) if comm_pct != "" else "",
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

    ax2b.axhline(100.0, linestyle="--", color="gray", label="ideal")
    ax2b.set_xlabel("Number of processes (P)")
    ax2b.set_ylabel("Efficiency (%)")
    ax2b.set_title("Efficiency vs P (%)")
    ax2b.legend(fontsize=8)
    ax2b.grid(True, alpha=0.3)
    fig2b.tight_layout()
    fig2b.savefig("efficiency_vs_p_pct.png", dpi=150)

    # Time vs N: sequential line + one line per P.
    fig3, ax3 = plt.subplots(figsize=(7, 5))
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

    # Time vs P: MPI execution time (log scale), one line per dataset.
    fig4, ax4 = plt.subplots(figsize=(7, 5))
    for label in sorted(dataset_n, key=lambda l: dataset_n[l]):
        ps = sorted(mpi_time[label].keys())
        if not ps:
            continue
        times = [mpi_time[label][p] for p in ps]
        ax4.plot(ps, times, marker="o", label=f"{label} (N={dataset_n[label]})")
    ax4.set_yscale("log")
    ax4.set_xlabel("Number of processes (P)")
    ax4.set_ylabel("MPI execution time (s, log scale)")
    ax4.set_title("Execution Time vs P")
    ax4.legend(fontsize=8)
    ax4.grid(True, alpha=0.3, which="both")
    fig4.tight_layout()
    fig4.savefig("time_vs_p.png", dpi=150)

    # Communication overhead vs P: comm_time / total mpi time, one line per dataset.
    has_breakdown = any(
        mpi_comm[label].get(p) is not None
        for label in mpi_comm for p in mpi_comm[label]
    )
    if has_breakdown:
        fig5, ax5 = plt.subplots(figsize=(7, 5))
        for label in sorted(dataset_n, key=lambda l: dataset_n[l]):
            ps = sorted(mpi_time[label].keys())
            pct, valid_ps = [], []
            for p in ps:
                comm = mpi_comm[label].get(p)
                t_mpi = mpi_time[label].get(p)
                if comm is not None and t_mpi:
                    pct.append(100.0 * comm / t_mpi)
                    valid_ps.append(p)
            if valid_ps:
                ax5.plot(valid_ps, pct, marker="o", label=f"{label} (N={dataset_n[label]})")
        ax5.set_xlabel("Number of processes (P)")
        ax5.set_ylabel("Communication (reduction/merge) / total MPI time (%)")
        ax5.set_title("Communication Overhead vs P")
        ax5.set_ylim(0, 105)
        ax5.legend(fontsize=8)
        ax5.grid(True, alpha=0.3)
        fig5.tight_layout()
        fig5.savefig("comm_overhead_vs_p.png", dpi=150)
    else:
        print("Skipping comm_overhead_vs_p.png: CSV has no comp_time/comm_time columns "
              "(rerun run_benchmarks.sh to regenerate benchmark_results.csv with the breakdown)")

    with open("summary_table.csv", "w", newline="") as f:
        writer = csv.DictWriter(
            f, fieldnames=["dataset", "N", "P", "time_seq", "time_mpi", "speedup", "efficiency",
                           "comp_time", "comm_time", "other_time", "comm_overhead_pct"])
        writer.writeheader()
        writer.writerows(summary_rows)

    print("Wrote speedup_vs_p.png, efficiency_vs_p.png, efficiency_vs_p_pct.png, time_vs_n.png, "
          "time_vs_p.png, comm_overhead_vs_p.png, summary_table.csv")


if __name__ == "__main__":
    main()
