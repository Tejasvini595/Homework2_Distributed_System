#!/bin/bash
# run_benchmarks.sh
# Runs log_analytics_seq once and log_analytics_mpi at P = 1,2,4,8 for a
# fixed set of generated datasets (varying N), and records timings to a CSV:
#   dataset,N,impl,P,time_seconds
#
# Timings are parsed from each program's own stderr diagnostics:
#   sequential -> "Processed N=... requests in X sec"
#   mpi        -> "Total time: X sec" (rank 0 only; includes read on rank 0,
#                 distribution, local compute, and reduction/merge)
#
# Intended to be invoked from inside a SLURM job (see benchmark.slurm),
# which allocates up to 8 ranks; mpirun -np P below uses a subset of that
# allocation for P < 8.
#
# Usage: ./run_benchmarks.sh [output_csv]

set -u
OUT_CSV="${1:-benchmark_results.csv}"
SEQ_BIN=./log_analytics_seq
MPI_BIN=./log_analytics_mpi
GEN=./generate_log_data
DDIR=./bench_data

mkdir -p "$DDIR"

echo "dataset,N,impl,P,time_seconds" > "$OUT_CSV"

# ------------------------------------------------------------------
# Dataset set: fixed seed (42) for reproducibility, varying N to see
# how both implementations scale from small to large logs. Server/
# endpoint/user counts and the 24h time window are held constant so
# only N changes between rows of the same K.
# ------------------------------------------------------------------
SIZES=(10000 100000 500000 1000000 2000000)
K=10
SEED=42

MPIRUN_FLAGS="${MPIRUN_FLAGS:---bind-to none}"

for N in "${SIZES[@]}"; do
    label="log_${N}"
    fname="$DDIR/${label}.txt"

    if [ ! -f "$fname" ]; then
        echo ">>> generating $label (N=$N)"
        "$GEN" "$N" "$K" "$SEED" 50 100 10000 86400 "$fname" >/dev/null 2>&1
    fi

    echo ">>> $label (N=$N) sequential"
    err_log=$(mktemp)
    "$SEQ_BIN" "$fname" /dev/null 2> "$err_log"
    t_seq=$(grep -oP 'in \K[0-9.eE+-]+(?= sec)' "$err_log")
    echo "$label,$N,seq,1,$t_seq" >> "$OUT_CSV"
    rm -f "$err_log"

    for P in 1 2 4 8; do
        echo ">>> $label (N=$N) mpi P=$P"
        err_log=$(mktemp)
        mpirun $MPIRUN_FLAGS -np "$P" "$MPI_BIN" "$fname" /dev/null 2> "$err_log"
        t_mpi=$(grep -oP 'Total time:\s*\K[0-9.eE+-]+' "$err_log")
        echo "$label,$N,mpi,$P,$t_mpi" >> "$OUT_CSV"
        rm -f "$err_log"
    done
done

echo ""
echo "Done. Results written to $OUT_CSV"
