#!/bin/bash
#SBATCH --job-name=q7-log-analytics
#SBATCH --time=00:10:00
#SBATCH --output=q7_%j.log
#SBATCH --partition=debug

# ------------------------------------------------------------
# Run a single P value against a single input file. Submit with:
#
#   sbatch --nodes=1 --ntasks-per-node=1 run_single.slurm input.txt   # P=1
#   sbatch --nodes=1 --ntasks-per-node=2 run_single.slurm input.txt   # P=2
#   sbatch --nodes=1 --ntasks-per-node=4 run_single.slurm input.txt   # P=4
#   sbatch --nodes=2 --ntasks-per-node=4 run_single.slurm input.txt   # P=8
#
# (nodes/ntasks-per-node passed on the sbatch command line override
# any #SBATCH defaults in this file, so P = nodes * ntasks-per-node.)
# ------------------------------------------------------------

module load hpcx-2.7.0/hpcx-ompi

INPUT="${1:-test_small.txt}"

echo "Job ID: $SLURM_JOB_ID"
echo "Nodes: $SLURM_NODELIST"
echo "Tasks: $SLURM_NTASKS"

mpic++ -O2 -std=c++17 log_analytics_mpi.cpp -o log_analytics_mpi

mpirun --bind-to none --mca coll ^hcoll -np "$SLURM_NTASKS" ./log_analytics_mpi "$INPUT"
