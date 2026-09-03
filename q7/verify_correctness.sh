#!/bin/bash
# verify_correctness.sh
# Runs the sequential reference and the MPI program (at several process
# counts) on a set of test logs and diffs their outputs exactly (output is
# fully deterministic and already sorted per the spec, so no need to
# re-sort before diffing, unlike a per-line/per-vertex output format).
#
# Usage: ./verify_correctness.sh [MPI_BIN] [SEQ_BIN] [log1.txt log2.txt ...]
# Defaults: MPI_BIN=./log_analytics_mpi  SEQ_BIN=./log_analytics_seq
# If no logs are given, it looks for test_small.txt and t_*.txt in the cwd.

set -u

MPI_BIN="${1:-./log_analytics_mpi}"
SEQ_BIN="${2:-./log_analytics_seq}"
shift 2 2>/dev/null || true

LOGS=("$@")
if [ ${#LOGS[@]} -eq 0 ]; then
    LOGS=(test_small.txt t_*.txt)
fi

# Generate a couple of larger reproducible correctness datasets if missing.
GEN=./generate_log_data
[ -f t_gen_small.txt ]  || "$GEN" 2000  5 11 20 30 500   3600  t_gen_small.txt  >/dev/null 2>&1
[ -f t_gen_medium.txt ] || "$GEN" 50000 10 22 100 200 5000 86400 t_gen_medium.txt >/dev/null 2>&1
LOGS+=(t_gen_small.txt t_gen_medium.txt)

PROCS=(1 2 3 4 8 16)
PASS=0
FAIL=0

for g in "${LOGS[@]}"; do
    [ -f "$g" ] || continue

    ref_out=$(mktemp)
    "$SEQ_BIN" "$g" "$ref_out" 2>/dev/null

    for p in "${PROCS[@]}"; do
        mpi_out=$(mktemp)
        mpirun --oversubscribe -np "$p" "$MPI_BIN" "$g" "$mpi_out" 2>/dev/null

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
