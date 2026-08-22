
#!/bin/bash

#SBATCH --job-name=q6-cc
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=4
#SBATCH --time=00:05:00
#SBATCH --output=q6_%j.log
#SBATCH --partition=debug

module load hpcx-2.7.0/hpcx-ompi

echo "Job ID: $SLURM_JOB_ID"
echo "Nodes: $SLURM_NODELIST"
echo "Tasks: $SLURM_NTASKS"

mpic++ -O2 -std=c++17 q6.cpp -o q6

mpirun --bind-to none --mca coll ^hcoll -np $SLURM_NTASKS ./q6 sample.txt