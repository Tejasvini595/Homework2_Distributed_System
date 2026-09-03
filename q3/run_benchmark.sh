#!/bin/bash
# run_benchmark.sh
# Runs bitonic_sort for P = 1,2,4,8 across several input sizes N,
# and records timing & comp/comm breakdown into results.csv.
#
# Usage: ./run_benchmark.sh
#
# Adjust NLIST / PLIST as needed. N values must be powers of 2.

set -e

NLIST="65536 262144 1048576 4194304"   # 2^16 .. 2^22
PLIST="1 2 4 8"
SEED=42
OUT=results.csv

echo "N,P,time_sec,comp_time_sec,comm_time_sec,correctness" > "$OUT"

TOTAL=0
PASS=0
FAIL=0

for N in $NLIST; do
    for P in $PLIST; do
        TOTAL=$((TOTAL + 1))
        LINE=$(mpirun --oversubscribe -np "$P" ./bitonic_sort "$N" "$SEED" 2>&1 >/dev/null)
        # LINE looks like: N=65536 P=1  time=0.003560 sec  comp_time=0.003560 sec  comm_time=0.000000 sec  correctness=PASS
        TIME=$(echo "$LINE" | sed -n 's/.* time=\([0-9.]*\) sec.*/\1/p')
        COMP=$(echo "$LINE" | sed -n 's/.*comp_time=\([0-9.]*\) sec.*/\1/p')
        COMM=$(echo "$LINE" | sed -n 's/.*comm_time=\([0-9.]*\) sec.*/\1/p')
        CORR=$(echo "$LINE" | sed -n 's/.*correctness=\([A-Z]*\).*/\1/p')
        
        if [ "$CORR" = "PASS" ]; then
            PASS=$((PASS + 1))
        else
            FAIL=$((FAIL + 1))
        fi
        
        echo "$N,$P,$TIME,${COMP:-0.000000},${COMM:-0.000000},$CORR" | tee -a "$OUT"
    done
done

echo ""
echo "Total: $TOTAL   Pass: $PASS   Fail: $FAIL"
echo "Benchmark complete. Results written to $OUT"

