#!/bin/bash
#SBATCH --job-name=q6-cc-benchmark
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=4
#SBATCH --cpus-per-task=1
#SBATCH --mem-per-cpu=4G
#SBATCH --time=02:00:00
#SBATCH --output=benchmark_%j.log
#SBATCH --error=benchmark_%j.err
#SBATCH --partition=debug

# ------------------------------------------------------------
# This allocates 2 nodes x 4 tasks/node = 8 ranks total.
# We then run mpirun -np P for P in {1,2,4,8} INSIDE this single
# allocation (mpirun just uses as many ranks as requested out of
# what's been allocated) -- this matches the pattern in the
# benchmark template provided for the assignment.
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
mpic++ -O2 -std=c++17 q6_mpi.cpp -o q6_mpi
if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

echo "Compiling sequential reference..."
g++ -O2 -std=c++17 q6_sequential.cpp -o q6_sequential
if [ $? -ne 0 ]; then
    echo "Sequential compilation failed!"
    exit 1
fi

echo ""
echo "Running correctness verification..."
chmod +x verify_correctness.sh
./verify_correctness.sh ./q6_mpi ./q6_sequential sample.txt t_v1.txt t_random.txt \
    t_chain.txt t_star.txt t_clustered.txt t_disc.txt t_dupes.txt
CORRECTNESS_STATUS=$?

echo ""
echo "Starting benchmark sweep (P = 1,2,4,8 across graph sizes/types)..."
chmod +x run_benchmarks.sh
./run_benchmarks.sh "benchmark_results_${SLURM_JOB_ID}.csv"

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
echo "Plots:   speedup_vs_p.png, efficiency_vs_p.png"
echo "========================================="
