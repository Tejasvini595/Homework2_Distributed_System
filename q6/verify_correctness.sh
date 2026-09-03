#!/bin/bash
# verify_correctness.sh
# Runs the sequential reference and the MPI program (at several process
# counts) on a set of test graphs and diffs their (sorted) outputs.
#
# Usage: ./verify_correctness.sh [MPI_BIN] [SEQ_BIN] [graph1.txt graph2.txt ...]
# Defaults: MPI_BIN=./q6_mpi  SEQ_BIN=./q6_sequential
# If no graphs are given, it looks for t_*.txt and sample.txt in the cwd.

set -u

MPI_BIN="${1:-./q6_mpi}"
SEQ_BIN="${2:-./q6_sequential}"
shift 2 2>/dev/null || true

GRAPHS=("$@")
if [ ${#GRAPHS[@]} -eq 0 ]; then
    GRAPHS=(sample.txt t_*.txt)
fi

PROCS=(1 2 4 8)
PASS=0
FAIL=0

for g in "${GRAPHS[@]}"; do
    [ -f "$g" ] || continue

    ref_out=$(mktemp)
    "$SEQ_BIN" "$g" | sort -n -k1,1 > "$ref_out"

    for p in "${PROCS[@]}"; do
        mpi_out=$(mktemp)
        mpirun --allow-run-as-root --oversubscribe -np "$p" "$MPI_BIN" "$g" 2>/dev/null \
            | sort -n -k1,1 > "$mpi_out"

        if diff -q "$ref_out" "$mpi_out" > /dev/null; then
            echo "PASS  $g  P=$p"
            PASS=$((PASS+1))
        else
            echo "FAIL  $g  P=$p"
            echo "  --- diff (ref vs mpi), first 10 lines ---"
            diff "$ref_out" "$mpi_out" | head -10
            FAIL=$((FAIL+1))
        fi
        rm -f "$mpi_out"
    done
    rm -f "$ref_out"
done

echo ""
echo "===================================="
echo "Total: $((PASS+FAIL))   Pass: $PASS   Fail: $FAIL"
echo "===================================="

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
