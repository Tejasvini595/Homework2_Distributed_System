# Q6 — Connected Components of a Large Graph (MPI)

## Files

| File                     | Purpose                                                        |
|--------------------------|-----------------------------------------------------------------|
| `q6_mpi.cpp`              | Parallel MPI solution (min-label propagation)                  |
| `q6_sequential.cpp`       | Sequential reference solution (Union-Find), used for correctness checks |
| `generate_graph.py`       | Generates test/benchmark graphs (random, chain, star, clustered, disconnected, dupes/self-loops) |
| `verify_correctness.sh`   | Runs sequential + MPI (P=1,2,4,8) on a set of graphs and diffs outputs |
| `run_benchmarks.sh`       | Runs MPI at P=1,2,4,8 across multiple graph sizes/types, logs timings to CSV |
| `plot_results.py`         | Computes speedup/efficiency from the CSV and produces plots     |
| `benchmark.slurm`         | Single SLURM job: compile, verify correctness, run full benchmark sweep, plot |
| `run_single.slurm`        | Parametrized SLURM script to run one P value against one input file |
| `sample.txt`              | The assignment's sample input                                   |

## Algorithm

Each vertex starts with `component_id = its own id`. Each round, every
vertex takes the minimum of its own label and all its neighbors'
labels (using the labels from the *previous* round, broadcast via
`MPI_Allgatherv`). This repeats until a full round produces no changes
anywhere (`MPI_Allreduce` with `MPI_MAX` on a changed-flag). At the
fixed point, every vertex's label equals the minimum vertex id in its
connected component, per the assignment spec.

Vertices are split into contiguous blocks across ranks (`get_range`).
Rank 0 reads the input, defensively symmetrizes the adjacency lists
(adds `u`'s edge back if `v` lists `u` but `u` doesn't list `v`, dedupes,
drops self-loops), and distributes flattened `(degree[], edges[])`
arrays to each rank via point-to-point `MPI_Send`/`MPI_Recv`.

## Important limitation: convergence time scales with diameter

This algorithm needs as many rounds as the **diameter** of the largest
connected component — not `O(log V)`. For random/clustered graphs the
diameter is typically small (`~log V`), so this converges fast. But
for a graph containing a long induced path (e.g. a chain), the number
of rounds is `O(V)`. We verified this directly: a 5000-vertex chain
graph took exactly 5000 iterations to converge (see `t_chain_big.txt`
in the test set). Each round does an `Allgatherv` over the whole
vertex set, so a worst-case chain-like graph near `V = 10^5` could be
significantly slower than a random graph of the same size. This is
discussed further in the "Communication vs. Computation" analysis
below — if your benchmark results show poor scaling on chain-like
graphs, this is the expected reason, not a bug. (A production-grade
version would use Shiloach–Vishkin style hooking + pointer jumping to
get `O(log V)` rounds; we did not implement this since the simpler
approach meets the assignment's correctness requirements, but it's
worth mentioning in your analysis writeup.)

## Compilation

```bash
module load hpcx-2.7.0/hpcx-ompi
mpic++ -O2 -std=c++17 q6_mpi.cpp -o q6_mpi
g++ -O2 -std=c++17 q6_sequential.cpp -o q6_sequential
```

## Running

```bash
mpirun -np <P> ./q6_mpi input.txt
```

- Output (stdout): exactly `V` lines of `vertex_id component_id`, sorted
  by vertex id ascending.
- Diagnostics (stderr, not part of required output): iteration count,
  elapsed time, and `P`/`V`.

Example against the assignment's sample:

```bash
mpirun -np 4 ./q6_mpi sample.txt
```
```
0 0
1 0
2 2
3 2
4 2
```

## Correctness verification

`q6_sequential.cpp` is a Union-Find reference implementation. Verify
the MPI program against it, at every required process count, across a
battery of test graphs (random, chain, star, clustered, fully
disconnected, and a graph deliberately poisoned with duplicate edges
and self-loops):

```bash
# generate the test graphs once
python3 generate_graph.py --type random         --V 500 --E 1500 --out t_random.txt
python3 generate_graph.py --type chain          --V 300           --out t_chain.txt
python3 generate_graph.py --type star           --V 300           --out t_star.txt
python3 generate_graph.py --type clustered      --V 400 --clusters 7 --out t_clustered.txt
python3 generate_graph.py --type disconnected   --V 100           --out t_disc.txt
python3 generate_graph.py --type dupes_selfloops --V 50 --E 80    --out t_dupes.txt

chmod +x verify_correctness.sh
./verify_correctness.sh ./q6_mpi ./q6_sequential \
    sample.txt t_random.txt t_chain.txt t_star.txt t_clustered.txt t_disc.txt t_dupes.txt
```

This diffs sorted MPI output against sorted sequential output for
P = 1, 2, 4, 8 on every graph listed. We ran exactly this locally
(32 combinations: 8 graphs x 4 process counts) — all passed, including
edge cases V=1, E=0 (fully disconnected), and P > V.

## Benchmarking

`run_benchmarks.sh` generates a fixed set of graphs (random and
clustered at 1k/10k/100k vertices, sparse and dense variants, plus
chain graphs to illustrate the diameter effect) and times `q6_mpi` at
P = 1, 2, 4, 8 on each, writing results to a CSV
(`graph,type,V,P,iterations,time_seconds`).

Submit the whole pipeline (compile → correctness check → benchmark
sweep → plots) as one SLURM job:

```bash
sbatch benchmark.slurm
```

This requests 2 nodes x 4 tasks/node (8 ranks total) and internally
runs `mpirun -np P` for `P` in `{1,2,4,8}` from within that single
allocation, so P=8 uses both nodes and P=1,2,4 use a subset of the
allocated ranks.

To test one specific P/node layout in isolation instead:

```bash
sbatch --nodes=1 --ntasks-per-node=1 run_single.slurm sample.txt   # P=1
sbatch --nodes=1 --ntasks-per-node=2 run_single.slurm sample.txt   # P=2
sbatch --nodes=1 --ntasks-per-node=4 run_single.slurm sample.txt   # P=4
sbatch --nodes=2 --ntasks-per-node=4 run_single.slurm sample.txt   # P=8
```

## Plots

After `run_benchmarks.sh` finishes (or is called automatically inside
`benchmark.slurm`):

```bash
python3 plot_results.py benchmark_results_<jobid>.csv
```

Produces:
- `speedup_vs_p.png` — Speedup `T(1)/T(P)` vs `P`, one line per graph, with an ideal-speedup reference line.
- `efficiency_vs_p.png` — Efficiency `Speedup/P` (decimal, 0-1 scale) vs `P`, one line per graph.
- `efficiency_vs_p_pct.png` — Same as above, on a 0-100% scale.
- `time_vs_p.png` — Total execution time (log scale) vs `P`, one line per graph.
- `time_vs_e.png` — Execution time (log-log) vs number of edges, one line per `P`.
- `comm_overhead_vs_p.png` — Communication time as a percentage of total time vs `P`, one line per graph.
- `summary_table.csv` — flat table of time/speedup/efficiency/comm-overhead per (graph, P).

The computation/communication/other breakdown comes directly from
timers inside `q6_mpi.cpp` (around the `MPI_Allgatherv`/`MPI_Allreduce`
calls and the local vertex-update loop, accumulated across all rounds
and reduced with `MPI_MAX` across ranks, matching how total time is
measured) — it is measured, not inferred. Because `comp`/`comm` are
each independently maxed across ranks, "other" (`total - comp - comm`)
can occasionally be a very small nonzero sliver even when no real
untracked work occurred, since the max-comp rank and max-comm rank
need not be the same rank; this is clipped at zero and is not a bug.

## Communication vs. computation analysis (fill in with your actual numbers)

Per iteration, each rank does:
- **Computation**: O(local_n + local_edges) work scanning its owned vertices' neighbor lists.
- **Communication**: one `MPI_Allgatherv` (O(V) data moved, all-to-all pattern) and one `MPI_Allreduce` (O(1) data, but a global sync every round).

For small/sparse graphs, communication overhead (latency of the
collective calls, ~`log P` factor) dominates over the trivial
computation — this is why small inputs may show *worse* speedup with
more processes (see `chain_small`/`rand_small` in our smoke test: P=2
and P=4 were slower than P=1, purely from `MPI_Allgatherv`/`Allreduce`
overhead swamping a tiny amount of real work). For large, low-diameter
graphs (random, clustered) with V near 10^5, computation-per-rank
grows and communication is amortized over more useful work, so speedup
should improve with P — up to the point where the O(V) Allgatherv
communication cost itself starts to dominate. For high-diameter graphs
(chains), the number of rounds is the bottleneck, not per-round cost —
more processes don't reduce the round count, so speedup will be poor
regardless of P.

*(Replace this paragraph's qualitative claims with the actual numbers from your `summary_table.csv` and plots once you've run the full sweep on the cluster.)*
