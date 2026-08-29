#!/bin/bash
#SBATCH --job-name=bitonic_sort
#SBATCH --output=bitonic_%j.out
#SBATCH --error=bitonic_%j.err
#SBATCH --ntasks=8          # max process count we test (P=8); mpirun below uses -np P per run
#SBATCH --nodes=1           # increase if your cluster requires multi-node for 8 ranks
#SBATCH --time=00:15:00
#SBATCH --partition=debug   # verified partition name on this cluster (check with `sinfo`)

# Load MPI module (verified working module on this cluster)
module load openmpi/4.1.5

set -e

N=1048576   # input size (must be a power of 2), edit as needed
SEED=42

echo "=== Sequential reference ==="
./sequential_sort $N $SEED

# NOTE: this cluster's OpenMPI build was not compiled with SLURM PMI/PMIx
# support, so `srun -n P ./bitonic_sort` fails with a pmix3x_client.c
# "Unreachable" error. Since salloc/sbatch already reserved the node(s),
# mpirun launches ranks directly without needing SLURM's PMI integration.
for P in 1 2 4 8; do
    echo "=== MPI Bitonic Sort: P=$P N=$N ==="
    mpirun -np $P ./bitonic_sort $N $SEED
done
