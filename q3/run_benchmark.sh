#!/bin/bash
# run_benchmark.sh
# Runs bitonic_sort for P = 1,2,4,8 across several input sizes N,
# and records timing output (from stderr) into results.csv.
#
# Usage: ./run_benchmark.sh
#
# Adjust NLIST / PLIST as needed. N values must be powers of 2.

set -e

NLIST="65536 262144 1048576 4194304"   # 2^16 .. 2^22
PLIST="1 2 4 8"
SEED=42
OUT=results.csv

echo "N,P,time_sec,correctness" > "$OUT"

for N in $NLIST; do
    for P in $PLIST; do
        LINE=$(mpirun --oversubscribe -np "$P" ./bitonic_sort "$N" "$SEED" 2>&1 >/dev/null)
        # LINE looks like: N=... P=... time=0.001234 sec  correctness=PASS
        TIME=$(echo "$LINE" | sed -n 's/.*time=\([0-9.]*\) sec.*/\1/p')
        CORR=$(echo "$LINE" | sed -n 's/.*correctness=\([A-Z]*\).*/\1/p')
        echo "$N,$P,$TIME,$CORR" | tee -a "$OUT"
    done
done

echo "Benchmark complete. Results in $OUT"
