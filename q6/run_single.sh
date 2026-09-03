#!/bin/bash
#SBATCH --job-name=q6-cc
#SBATCH --time=00:10:00
#SBATCH --output=q6_%j.log
#SBATCH --partition=debug

# Run a single P value against a single input file. Submit with:
#
#   sbatch --nodes=1 --ntasks-per-node=1 run_single.slurm sample.txt   # P=1
#   sbatch --nodes=1 --ntasks-per-node=2 run_single.slurm sample.txt   # P=2
#   sbatch --nodes=1 --ntasks-per-node=4 run_single.slurm sample.txt   # P=4
#   sbatch --nodes=2 --ntasks-per-node=4 run_single.slurm sample.txt   # P=8
#
# (nodes/ntasks-per-node passed on the sbatch command line override
# any #SBATCH defaults in this file, so P = nodes * ntasks-per-node.)

module load hpcx-2.7.0/hpcx-ompi

INPUT="${1:-sample.txt}"

echo "Job ID: $SLURM_JOB_ID"
echo "Nodes: $SLURM_NODELIST"
echo "Tasks: $SLURM_NTASKS"

mpic++ -O2 -std=c++17 q6_mpi.cpp -o q6_mpi

mpirun --bind-to none --mca coll ^hcoll -np "$SLURM_NTASKS" ./q6_mpi "$INPUT"
