#!/bin/bash
#SBATCH --job-name=q7-log-analytics-benchmark
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=4
#SBATCH --cpus-per-task=1
#SBATCH --mem-per-cpu=4G
#SBATCH --time=02:00:00
#SBATCH --output=benchmark_%j.log
#SBATCH --error=benchmark_%j.err
#SBATCH --partition=debug

# ------------------------------------------------------------
# This allocates 2 nodes x 4 tasks/node = 8 ranks total. We then run
# mpirun -np P for P in {1,2,4,8} INSIDE this single allocation
# (mpirun just uses as many ranks as requested out of what's been
# allocated), so P=8 spans both nodes and P=1,2,4 use a subset.
# ------------------------------------------------------------

module load hpcx-2.7.0/hpcx-ompi

echo "========================================="
echo "SLURM Job ID: $SLURM_JOB_ID"
echo "Allocated nodes: $SLURM_NNODES"
echo "Total tasks: $SLURM_NTASKS"
echo "Node list: $SLURM_NODELIST"
echo "========================================="
echo ""

echo "Compiling MPI program..."
mpic++ -O2 -std=c++17 log_analytics_mpi.cpp -o log_analytics_mpi
if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

echo "Compiling sequential reference..."
g++ -O2 -std=c++17 log_analytics_seq.cpp -o log_analytics_seq
if [ $? -ne 0 ]; then
    echo "Sequential compilation failed!"
    exit 1
fi

echo "Compiling dataset generator..."
g++ -O2 -std=c++17 generate_log_data.cpp -o generate_log_data
if [ $? -ne 0 ]; then
    echo "Generator compilation failed!"
    exit 1
fi

echo ""
echo "Running correctness verification..."
chmod +x verify_correctness.sh
./verify_correctness.sh ./log_analytics_mpi ./log_analytics_seq \
    test_small.txt t_single.txt t_edge_k.txt t_allfail.txt
CORRECTNESS_STATUS=$?

echo ""
echo "Starting benchmark sweep (N = 10K..2M, P = 1,2,4,8)..."
chmod +x run_benchmarks.sh
MPIRUN_FLAGS="--bind-to none --mca coll ^hcoll" ./run_benchmarks.sh "benchmark_results_${SLURM_JOB_ID}.csv"

echo ""
echo "Generating plots..."
python3 plot_results.py "benchmark_results_${SLURM_JOB_ID}.csv"

echo ""
echo "========================================="
if [ $CORRECTNESS_STATUS -ne 0 ]; then
    echo "WARNING: correctness verification reported failures - check log above"
fi
echo "Benchmark completed!"
echo "Results: benchmark_results_${SLURM_JOB_ID}.csv"
echo "Plots:   speedup_vs_p.png, efficiency_vs_p.png, efficiency_vs_p_pct.png, time_vs_n.png, time_vs_p.png, comm_overhead_vs_p.png"
echo "========================================="
