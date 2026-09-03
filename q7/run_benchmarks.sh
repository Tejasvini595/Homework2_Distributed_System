#!/bin/bash
# run_benchmarks.sh
# Runs log_analytics_seq once and log_analytics_mpi at P = 1,2,4,8 for a
# fixed set of generated datasets (varying N), and records timings to a CSV:
#   dataset,N,impl,P,time_seconds,comp_time,comm_time,other_time
#
# Timings are parsed from each program's own stderr diagnostics:
#   sequential -> "Processed N=... requests in X sec" (comp/comm/other left
#                 blank: the sequential program does no MPI communication,
#                 so that breakdown doesn't apply to it)
#   mpi        -> "Total time: X sec" (MPI_Reduce MAX across ranks, printed by
#                 rank 0; reflects the slowest rank, not just rank 0's own time)
#                 comp_time <- "Parallel read+compute time (max over ranks): X sec"
#                 comm_time <- "Reduction/merge time: X sec" (also max over ranks)
#                 other_time <- total - comp - comm (barrier/setup slack)
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

echo "dataset,N,impl,P,time_seconds,comp_time,comm_time,other_time" > "$OUT_CSV"

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
    echo "$label,$N,seq,1,$t_seq,,," >> "$OUT_CSV"
    rm -f "$err_log"

    for P in 1 2 4 8; do
        echo ">>> $label (N=$N) mpi P=$P"
        err_log=$(mktemp)
        mpirun $MPIRUN_FLAGS -np "$P" "$MPI_BIN" "$fname" /dev/null 2> "$err_log"
        t_mpi=$(grep -oP 'Total time:\s*\K[0-9.eE+-]+' "$err_log")
        t_comp=$(grep -oP 'Parallel read\+compute time[^:]*:\s*\K[0-9.eE+-]+' "$err_log")
        t_comm=$(grep -oP 'Reduction/merge time:\s*\K[0-9.eE+-]+' "$err_log")
        t_other=$(awk -v t="$t_mpi" -v c="$t_comp" -v m="$t_comm" \
            'BEGIN { v = t - c - m; if (v < 0) v = 0; print v }')
        echo "$label,$N,mpi,$P,$t_mpi,$t_comp,$t_comm,$t_other" >> "$OUT_CSV"
        rm -f "$err_log"
    done
done

echo ""
echo "Done. Results written to $OUT_CSV"
