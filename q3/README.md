# Q3 — Distributed Bitonic Sort (MPI)

Verified end-to-end on the IIIT-H `rce` cluster (`rce.iiit.ac.in`), partition
`debug`, node `node06`, OpenMPI `4.1.5`. All commands below are the exact
commands that worked, in the order that worked, including the workarounds
needed for this cluster's SLURM/OpenMPI setup.

## Files
- `bitonic_sort.cpp` — MPI bitonic sort with instrumented computation vs communication timers (main deliverable)
- `sequential_sort.cpp` — sequential baseline, same RNG/seed, used for speedup comparison
- `run_benchmark.sh` — loops over P = 1,2,4,8 and several N, records timing + comp/comm breakdown into `results.csv`, and prints pass/fail summary (`Total: 16 Pass: 16 Fail: 0`)
- `job.slurm` — SLURM batch script that runs the same sweep via `mpirun` (see Section 5 note on `srun`)

## 1. Copy files to the cluster (run on your **local** machine)

```bash
scp -r bitonic_sort_hw2 cs3401.42@rce.iiit.ac.in:~/
```

Then log in:

```bash
ssh cs3401.42@rce.iiit.ac.in
cd bitonic_sort_hw2
```

To exit the SSH session later: type `exit` (or Ctrl+D). If you're still
inside an `salloc` allocation, you may need `exit` twice — once to release
the allocation, once to close the SSH connection itself (check the prompt:
if it still shows `cs3401.42@rce...`, you're still logged in).

## 2. Load MPI module

```bash
module load openmpi/4.1.5
```

Verify:
```bash
which mpic++
mpic++ --version
```

## 3. Compile (on the login node — compiling is fine here; only running jobs is restricted)

```bash
mpic++ -O2 -std=c++17 -o bitonic_sort bitonic_sort.cpp
g++    -O2 -std=c++17 -o sequential_sort sequential_sort.cpp
ls -la
```
Expect to see `bitonic_sort` and `sequential_sort` as executable files
(green/`rwxr-xr-x`) alongside the `.cpp` sources.

## 4. Find the partition name

The login banner on this cluster warns: *"Compute nodes can be accessed
only by submitting a batch or interactive job."* So you cannot run
`mpirun`/`srun` directly on the login node — you must first get an
allocation.

```bash
sinfo
```

On this cluster there is one partition: **`debug`** (marked `*` = default),
time limit `infinite`. Node states seen: `idle` (free), `mix` (partially
used, still has room), `alloc` (fully busy), `drain*` (offline — avoid).
SLURM schedules automatically; you don't need to pick a specific node.

## 5. Get an interactive allocation and run

```bash
salloc --nodes=1 --ntasks=8 --time=00:15:00 --partition=debug
```

Wait for `salloc: Granted job allocation <id>` and `Nodes <nodeXX> are
ready for job`. Once granted:

```bash
module load openmpi/4.1.5
```

**Important — use `mpirun`, not `srun`, for launching the program.**
On this cluster's OpenMPI build, `srun -n P ./bitonic_sort ...` fails with:
```
OPAL ERROR: Unreachable in file pmix3x_client.c at line 112
The application appears to have been directly launched using "srun",
but OMPI was not built with SLURM's PMI support...
```
This happens because OpenMPI 4.1.5 here wasn't compiled with SLURM's
PMI/PMIx integration. The fix: since `salloc` has already reserved the
node(s) for you, just use `mpirun` directly — it doesn't need SLURM's PMI
support and works fine from inside the allocation:

```bash
mpirun -np 1 ./sequential_sort 1024 42
mpirun -np 1 ./bitonic_sort 1024 42
mpirun -np 2 ./bitonic_sort 1024 42
mpirun -np 4 ./bitonic_sort 1024 42
mpirun -np 8 ./bitonic_sort 1024 42
```

**Run these one command at a time**, waiting for each to finish and print
its result before typing the next — pasting several commands in quick
succession can cause `srun`/`mpirun` step collisions.

Release the allocation when done:
```bash
exit
```

## 6. Expected output per run

Each `mpirun` call prints one line like this to **stderr**:
```
N=1024 P=4  time=0.000042 sec  comp_time=0.000016 sec  comm_time=0.000026 sec  correctness=PASS
```
and either the sorted array (if N ≤ 1000) or a confirmation line to
**stdout**:
```
Sorted 1024 elements. correctness=PASS
```

**You will also see harmless warnings** — these do NOT indicate failure:
```
mca_base_component_repository_open: unable to open mca_pml_ucx: libucp.so.0: cannot open shared object file: No such file or directory (ignored)
mca_base_component_repository_open: unable to open mca_osc_ucx: libucp.so.0: cannot open shared object file: No such file or directory (ignored)
[nodeXX.local:XXXXXXX] Read -1, expected <N>, errno = 1
```
These occur because the faster UCX transport isn't installed on this
cluster, so OpenMPI automatically falls back to its shared-memory (`vader`)
transport. The program still runs correctly — always check for
`correctness=PASS` on stderr, which is the real pass/fail signal.

## 7. SLURM commands cheat sheet

| Command | Purpose |
|---|---|
| `sinfo` | List partitions/nodes and their state |
| `salloc --nodes=1 --ntasks=8 --time=00:15:00 --partition=debug` | Request an interactive allocation |
| `mpirun -np P ./bitonic_sort N SEED` | Launch P MPI ranks (inside an `salloc` session; use `mpirun`, not `srun`, on this cluster) |
| `sbatch job.slurm` | Submit a non-interactive batch job |
| `squeue -u $USER` | Check status of your own jobs |
| `sacct -j <jobid> --format=JobID,State,Elapsed,ExitCode` | Check a job's outcome after it leaves the queue |
| `scancel <jobid>` | Cancel a specific job |
| `scancel -u $USER` | Cancel all your own jobs |
| `scontrol show job <jobid>` | Detailed info about a specific job |

## 8. Benchmark script (P=1,2,4,8 sweep across sizes)

```bash
salloc --nodes=1 --ntasks=8 --time=00:20:00 --partition=debug
module load openmpi/4.1.5
chmod +x run_benchmark.sh
./run_benchmark.sh
cat results.csv
exit
```

Edit `NLIST` inside `run_benchmark.sh` to control which input sizes are
tested (each entry must be a power of 2). Results (`N,P,time_sec,comp_time_sec,comm_time_sec,correctness`)
are appended to `results.csv` for the speedup/efficiency plots required in
the deliverables.

Copy the results back to your local machine for plotting (run this on your
**local** terminal, not while SSH'd in):
```bash
scp cs3401.42@rce.iiit.ac.in:~/bitonic_sort_hw2/results.csv .
```

Speedup and efficiency:
```
Speedup(P)    = T(1) / T_parallel(P)
Efficiency(P) = Speedup(P) / P
```

## 9. Algorithm notes (how the code matches the spec)

1. **Initial Local Sort**: Each rank sorts its own `N/P` chunk based on its rank parity:
   - Even ranks (`(rank & 1) == 0`) sort ascending using `std::sort`.
   - Odd ranks (`(rank & 1) == 1`) sort descending using `std::sort(..., std::greater<int>())`.
   - This ensures adjacent initial process pairs form bitonic sequences (e.g. $P_0$ (asc) + $P_1$ (desc) = bitonic sequence).
2. **Bitonic Merge Network across Ranks**: For stage $i = 0 \dots \log_2(P)-1$ and substage $j = i \dots 0$:
   - Each rank exchanges its local chunk with `partner = rank ^ (1 << j)` via `MPI_Sendrecv`.
   - Ranks perform **position-wise compare-exchange**:
     - Ascending blocks (`((rank >> (i + 1)) & 1) == 0`): lower rank keeps `min(local[k], other[k])`, higher rank keeps `max(local[k], other[k])`.
     - Descending blocks (`((rank >> (i + 1)) & 1) != 0`): lower rank keeps `max(local[k], other[k])`, higher rank keeps `min(local[k], other[k])`.
   - Ranks **re-sort locally**:
     - Local sort direction is determined by bit $(j - 1)$ of rank when $j > 0$, or bit $(i + 1)$ when $j = 0$.
3. **Gather**: `MPI_Gather` reconstructs the full sorted array at rank 0.
4. **Correctness verification**: rank 0 independently sorts the same input with `std::sort` and compares it to the gathered result (`PASS`/`FAIL` printed to stderr).
5. Debug/info prints go to **stderr**; only the final sorted output goes to **stdout**, per the "do not print debugging information as part of the required program output" instruction.

## 10. Troubleshooting quick reference

| Symptom | Cause | Fix |
|---|---|---|
| `srun: error ... OPAL ERROR: Unreachable in file pmix3x_client.c` | OpenMPI not built with SLURM PMI support | Use `mpirun -np P ...` instead of `srun -n P ...` |
| `There are not enough slots available` (only on your own laptop, not the cluster) | Fewer CPU cores than `-np` requested | Add `--oversubscribe` (local testing only, never needed on the real cluster with a proper `salloc`) |
| `salloc: Job <id> has exceeded its time limit and its allocation has been revoked` | Interactive session left open past `--time` | Request a longer `--time`, or just re-run `salloc` |
| `mca_base_component_repository_open: unable to open mca_pml_ucx / mca_osc_ucx` and `Read -1, ... errno = 1` | UCX fast-transport library not installed; OpenMPI falls back to shared-memory transport | Harmless — ignore; check `correctness=PASS` instead |
| Compute node access refused directly from login node | Cluster policy: compute nodes only via batch/interactive job | Use `salloc` or `sbatch`, never run `mpirun`/`srun` on the login node itself |

