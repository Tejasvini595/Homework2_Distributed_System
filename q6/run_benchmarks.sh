#!/bin/bash
# run_benchmarks.sh
# Runs q6_mpi across P = 1,2,4,8 for a set of generated graphs and
# records timing (total, computation, communication, other) to a CSV.
#
# Intended to be invoked from inside a SLURM job (see benchmark.slurm)
# where SLURM_NTASKS/SLURM_NNODES already give you up to 8 ranks
# allocated; mpirun -np P below just uses a subset of that allocation.
#
# Usage: ./run_benchmarks.sh [output_csv]

set -u
OUT_CSV="${1:-benchmark_results.csv}"
MPI_BIN=./q6_mpi
GEN=./generate_graph.py
GDIR=./bench_graphs

mkdir -p "$GDIR"

echo "graph,type,V,E,P,iterations,time_seconds,comp_time,comm_time,other_time" > "$OUT_CSV"

# Graph set: matches the (V, E) sizes your friend used, named
# graph_<V>_<E> for direct comparison, plus the chain/clustered
# graphs from the diameter-effect analysis already in the report.
GRAPH_SPECS=(
   "random 1000 3000 rand_1k"
    "random 10000 30000 rand_10k"
    "random 100000 300000 rand_100k"
    "random 100000 1000000 rand_100k_dense"
)

CHAIN_CLUSTERED_SPECS=(
    "chain 2000 0 chain_2k"
    "chain 20000 0 chain_20k"
    "clustered 10000 0 clustered_10k"
    "clustered 100000 0 clustered_100k"
)

for spec in "${GRAPH_SPECS[@]}"; do
    read -r type V E label <<< "$spec"
    fname="$GDIR/${label}.txt"

    if [ ! -f "$fname" ]; then
        python3 "$GEN" --type random --V "$V" --E "$E" --out "$fname"
    fi

    for P in 1 2 4 8; do
        echo ">>> $label (V=$V, E=$E) P=$P"
        err_log=$(mktemp)
        mpirun --bind-to none -np "$P" "$MPI_BIN" "$fname" > /dev/null 2> "$err_log"

        iters=$(grep -oP 'Iterations:\s*\K[0-9]+' "$err_log")
        t=$(grep -oP 'Time:\s*\K[0-9.eE+-]+' "$err_log" | head -1)
        comp=$(grep -oP 'CompTime:\s*\K[0-9.eE+-]+' "$err_log")
        comm=$(grep -oP 'CommTime:\s*\K[0-9.eE+-]+' "$err_log")
        other=$(grep -oP 'OtherTime:\s*\K[0-9.eE+-]+' "$err_log")

        echo "$label,$type,$V,$E,$P,$iters,$t,$comp,$comm,$other" >> "$OUT_CSV"
        rm -f "$err_log"
    done
done

for spec in "${CHAIN_CLUSTERED_SPECS[@]}"; do
    read -r type V E label <<< "$spec"
    fname="$GDIR/${label}.txt"

    if [ ! -f "$fname" ]; then
        if [ "$type" == "clustered" ]; then
            python3 "$GEN" --type clustered --V "$V" --clusters 20 --out "$fname"
        else
            python3 "$GEN" --type "$type" --V "$V" --out "$fname"
        fi
    fi

    # actual edge count = V-1 for chain; unknown ahead of time for
    # clustered, so recover it by counting adjacency-list entries.
    real_E=$(awk 'NR==1{next} {s+=$1} END{print s/2}' "$fname")

    for P in 1 2 4 8; do
        echo ">>> $label (V=$V, E=$real_E) P=$P"
        err_log=$(mktemp)
        mpirun --bind-to none -np "$P" "$MPI_BIN" "$fname" > /dev/null 2> "$err_log"

        iters=$(grep -oP 'Iterations:\s*\K[0-9]+' "$err_log")
        t=$(grep -oP 'Time:\s*\K[0-9.eE+-]+' "$err_log" | head -1)
        comp=$(grep -oP 'CompTime:\s*\K[0-9.eE+-]+' "$err_log")
        comm=$(grep -oP 'CommTime:\s*\K[0-9.eE+-]+' "$err_log")
        other=$(grep -oP 'OtherTime:\s*\K[0-9.eE+-]+' "$err_log")

        echo "$label,$type,$V,$real_E,$P,$iters,$t,$comp,$comm,$other" >> "$OUT_CSV"
        rm -f "$err_log"
    done
done

echo ""
echo "Done. Results written to $OUT_CSV"
