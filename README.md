# Q7 — Large-Scale Server Log Analytics

Sequential and MPI implementations of the server-log analytics pipeline
described in the assignment, plus a reproducible dataset generator,
correctness verification, and a benchmarking/plotting pipeline.

## Files

| File                     | Purpose                                                                 |
|--------------------------|--------------------------------------------------------------------------|
| `log_analytics_seq.cpp`   | Sequential reference implementation                                      |
| `log_analytics_mpi.cpp`   | Parallel MPI implementation (parallel file I/O + local reduce + merge)   |
| `generate_log_data.cpp`   | Reproducible, fixed-seed dataset generator                               |
| `verify_correctness.sh`   | Runs sequential + MPI (P = 1,2,3,4,8,16) on a battery of logs and diffs output |
| `run_benchmarks.sh`       | Runs sequential + MPI at P = 1,2,4,8 across multiple input sizes, logs timings to CSV |
| `plot_results.py`         | Computes speedup/efficiency from the CSV and produces plots              |
| `benchmark.slurm`         | Single SLURM job: compile, verify correctness, run full benchmark sweep, plot |
| `run_single.slurm`        | Parametrized SLURM script to run one P value against one input file      |
| `test_small.txt`          | Small, hand-verifiable 6-request test case                               |
| `t_single.txt`, `t_edge_k.txt`, `t_allfail.txt` | Hand-crafted edge cases (N=1, K larger than distinct IDs, all-failed requests) |
| `log_data_1M.txt`, `output_1M.txt` | A pre-generated 1M-request example dataset and its expected output |

## What it computes

Reads `N K S` on the first line (N = number of log lines, K = top-K count,
S = seed — carried through the input format but not needed by the
analytics), then N lines of:
```
timestamp server_id endpoint_id user_id status_code response_time bytes_sent
```

Outputs, in order:
```
TOTAL_REQUESTS <value>
SUCCESSFUL_REQUESTS <value>
FAILED_REQUESTS <value>
AVERAGE_RESPONSE_TIME <value>
MIN_RESPONSE_TIME <value>
MAX_RESPONSE_TIME <value>
TOTAL_BYTES <value>
STATUS_2XX <value>
STATUS_3XX <value>
STATUS_4XX <value>
STATUS_5XX <value>
BUSIEST_INTERVAL <interval_id> <count>
TOP_SERVERS
<server_id> <request_count> <average_response_time>
...
TOP_ENDPOINTS
<endpoint_id> <request_count> <total_bytes>
...
```

Notes:
- A request is successful if `status_code < 400`.
- `BUSIEST_INTERVAL` uses `timestamp / 60` (integer division) as the
  interval id; ties broken by smallest interval id.
- `TOP_SERVERS` / `TOP_ENDPOINTS` are sorted by decreasing request count,
  then increasing ID — if fewer than K distinct servers/endpoints exist,
  all of them are printed (no padding).
- Response-time values are printed with 2 decimal places
  (`std::setprecision(2)`).
- Diagnostics (elapsed time, iteration/rank info) go to **stderr**; only
  the required output goes to **stdout** or the given output file.

## MPI parallelization strategy

**Data distribution is parallel file I/O, not a single-rank read + scatter.**
Every rank independently opens the same input file (it lives on a shared
filesystem) and works out its own contiguous **byte range**:
`[rank * data_size / P, (rank+1) * data_size / P)`, where `data_size`
excludes the header line. Each boundary is snapped forward to the start of
the next full line (`skip_to_line_start`), so every log line is parsed by
exactly one rank and adjacent ranks' boundaries agree exactly (both compute
the identical boundary offset independently, since it's a pure function of
the byte offset and file contents) — no communication is needed to hand out
the raw records at all. This is the same "file split" idea used by
big-data log processors (e.g. Hadoop's `TextInputFormat`).

An earlier version of this program had rank 0 read and parse the entire
file and then `MPI_Scatterv` the parsed columns out to everyone. Measured
locally, rank 0's serial read completely dominated the runtime (e.g. on the
1M-row dataset: 0.58s spent reading on rank 0 vs. 0.006s of actual local
computation) and the program showed **no speedup at all** regardless of P.
Switching to parallel per-rank file I/O turned that same 1M-row case from
~0.58s (any P) into ~0.19s at P=4 — see [Benchmark results](#benchmark-results-local-validation)
below. This is worth calling out explicitly because it's the single
biggest factor in whether this kind of embarrassingly-parallel-looking
workload actually parallelizes: if the read is serial, nothing else
matters.

Each rank then does a **single pass** over its own byte range: it reads
that exact slice into a private buffer (`ifstream::read`) and parses it
with its own `istringstream`, accumulating local partial statistics
(running sums/min/max for response time, byte totals, per-status-class
counts, and three local hash maps: server → {count, sum_rt}, endpoint →
{count, total_bytes}, interval → count). Parsing from a private buffer
(rather than checking the shared file stream's position against a byte
boundary on every iteration) sidesteps a real bug we hit: formatted `>>`
extraction leaves the stream positioned right after the last digit of a
field, *before* consuming the trailing newline, which makes a live
`tellg() < boundary` check unreliable right at a boundary and caused
requests near a chunk edge to be double-counted in an intermediate version.

**Reduction:** the handful of scalars (successful/failed counts, response
time sum/min/max, total bytes, status-class counts) are combined with
`MPI_Reduce` (`MPI_SUM` / `MPI_MIN` / `MPI_MAX`). The three hash maps are
combined by having each rank `MPI_Gather` its distinct-key count, then
`MPI_Gatherv` its (key, aggregate...) rows to rank 0, which merges them
into global maps. This gather is cheap regardless of N because its size is
proportional to the number of **distinct** servers/endpoints/intervals,
not to the number of records — for realistic cardinalities (tens to
low-thousands of servers/endpoints, up to `time_range/60` intervals) this
stays tiny even for a log with tens of millions of rows. Rank 0 then sorts
for Top-K and finds the busiest interval, and writes the output.

## Compilation

```bash
# Local (OpenMPI is available via the system module):
module load mpi/openmpi-x86_64      # or: module load hpcx-2.7.0/hpcx-ompi on the cluster
mpic++ -O2 -std=c++17 -o log_analytics_mpi log_analytics_mpi.cpp
g++    -O2 -std=c++17 -o log_analytics_seq log_analytics_seq.cpp
g++    -O2 -std=c++17 -o generate_log_data generate_log_data.cpp
```

## Running

```bash
./log_analytics_seq input.txt output.txt        # or: ./log_analytics_seq < input.txt > output.txt
mpirun -np <P> ./log_analytics_mpi input.txt output.txt
```

Example against the small hand-verified case:
```bash
./log_analytics_seq test_small.txt out.txt
mpirun --oversubscribe -np 4 ./log_analytics_mpi test_small.txt out_mpi.txt
diff out.txt out_mpi.txt   # empty
```
(`--oversubscribe` is only needed when P exceeds the number of physical
cores available, e.g. testing P=16 on an 8-core laptop; drop it on the
cluster where each rank gets a real allocated core.)

## Dataset generation

`generate_log_data.cpp` produces input files in the exact format expected
above.

**Determinism:** uses `std::mt19937` seeded with the given `SEED` — a
fully specified, platform-independent algorithm, so the same seed always
produces byte-identical output regardless of machine/OS/compiler (verified
by generating the same dataset twice and diffing — identical; a different
seed diverges as expected).

**Generation method / parameters:**

| Field | Distribution |
|---|---|
| `timestamp` | uniform integer, `[0, time_range_seconds - 1]` |
| `server_id` | uniform integer, `[1, num_servers]` |
| `endpoint_id` | uniform integer, `[1, num_endpoints]` |
| `user_id` | uniform integer, `[1, num_users]` |
| `status_code` | weighted discrete choice over `{200,201,204,301,302,400,401,403,404,500,502,503}` with weights `{50,10,5,5,3,6,4,3,6,3,3,2}` (out of 100) — mimics realistic traffic where most responses are 2xx |
| `response_time` | uniform real, `[1.0, 2000.0]` ms |
| `bytes_sent` | uniform integer, `[100, 100000]` |

**Usage:**
```bash
./generate_log_data N K SEED [num_servers] [num_endpoints] [num_users] [time_range_seconds] [output_file]
```
Optional args default to: `num_servers=50`, `num_endpoints=100`,
`num_users=10000`, `time_range_seconds=86400` (24h), and
`output_file="log_data_<N>_<SEED>.txt"` if omitted.

```bash
# Small correctness-test dataset
./generate_log_data 10000 5 42

# Larger benchmarking dataset, custom entity counts and a 1-hour window
./generate_log_data 1000000 10 42 100 200 50000 3600 log_data_1M.txt
```

## Correctness verification

`verify_correctness.sh` runs `log_analytics_seq` once per test log, runs
`log_analytics_mpi` at P = 1, 2, 3, 4, 8, 16 on the same log, and diffs the
outputs directly (the output format is fully deterministic and already
sorted per the spec, so no re-sorting is needed before diffing, unlike a
per-line/per-vertex output).

```bash
chmod +x verify_correctness.sh
./verify_correctness.sh ./log_analytics_mpi ./log_analytics_seq \
    test_small.txt t_single.txt t_edge_k.txt t_allfail.txt
```
With no arguments it defaults to `test_small.txt` + all `t_*.txt` files,
and additionally generates (once) and checks two more reproducible
datasets (`t_gen_small.txt`, N=2000; `t_gen_medium.txt`, N=50000).

Test set and what each covers:
- `test_small.txt` — the assignment-style 6-request hand example.
- `t_single.txt` — N=1 (smallest possible input; also exercises P > N).
- `t_edge_k.txt` — K=10 but only 2 distinct servers / 3 distinct
  endpoints, to check Top-K doesn't pad missing entries, and includes a
  tied timestamp/interval case.
- `t_allfail.txt` — every request has status ≥ 500 (all failed, exercises
  `STATUS_5XX`-only and zero-successful-requests paths).
- `t_gen_small.txt`, `t_gen_medium.txt` — larger randomly generated
  datasets (reproducible via `generate_log_data`, seeds 11 and 22).

We ran this locally at every process count above, including **P=16 on a
7-line file** (P > N) and P=1 (verifies the MPI code path degenerates
correctly to a single full-file range): **48/48 combinations passed**
(6 process counts × 8 test files, byte-identical output to the sequential
reference in every case).

## Benchmarking

`run_benchmarks.sh` generates a fixed sequence of datasets (N = 10K, 100K,
500K, 1M, 2M; fixed seed 42, K=10, 50 servers, 100 endpoints, 10K users,
24h time window) and times `log_analytics_seq` once and
`log_analytics_mpi` at P = 1, 2, 4, 8 on each, writing
`dataset,N,impl,P,time_seconds` rows to a CSV. Timings are parsed from each
program's own stderr diagnostics and include reading the input (I/O) plus
computing and merging results — i.e. total wall-clock time for the whole
job, not just the compute-only inner loop, since I/O time is exactly the
thing this parallelization strategy targets.

```bash
chmod +x run_benchmarks.sh
./run_benchmarks.sh benchmark_results.csv
python3 plot_results.py benchmark_results.csv
```
Produces `speedup_vs_p.png` (`T_seq / T_mpi(P)` vs P, one line per
dataset), `efficiency_vs_p.png` (`Speedup / P`), `time_vs_n.png`
(execution time vs input size, log-log), and `summary_table.csv`.

### Running the full pipeline on the cluster (SLURM)

```bash
sbatch benchmark.slurm
```
This requests 2 nodes × 4 tasks/node (8 ranks total) and internally runs
`mpirun -np P` for P in `{1,2,4,8}` from within that single allocation —
P=8 spans both nodes, P=1/2/4 use a subset. It compiles everything, runs
`verify_correctness.sh`, runs the benchmark sweep, and generates plots in
one job.

To test one specific P/node layout in isolation instead:
```bash
sbatch --nodes=1 --ntasks-per-node=1 run_single.slurm test_small.txt   # P=1
sbatch --nodes=1 --ntasks-per-node=2 run_single.slurm test_small.txt   # P=2
sbatch --nodes=1 --ntasks-per-node=4 run_single.slurm test_small.txt   # P=4
sbatch --nodes=2 --ntasks-per-node=4 run_single.slurm test_small.txt   # P=8
```

## Benchmark results (local validation)

The numbers below (`benchmark_results_local.csv`, `summary_table.csv`,
and the three `.png` plots already checked into this folder) were produced
by running `run_benchmarks.sh` on a **single 16-core local machine**
(OpenMPI, `--bind-to none`), as a sanity check that the implementation and
the benchmarking/plotting pipeline work end-to-end and actually show
speedup. They are **not** a substitute for the official multi-node cluster
run — re-run `sbatch benchmark.slurm` on the cluster (2 nodes × 4
tasks/node) for the numbers to report, since only that run exercises real
inter-node network communication for the `Gatherv` reduction step.

| N | P=1 | P=2 | P=4 | P=8 |
|---|---|---|---|---|
| 10,000   | 0.87x | 1.99x | 2.11x | 1.91x |
| 100,000  | 0.96x | 1.80x | 3.37x | 3.52x |
| 500,000  | 0.97x | 1.84x | 3.84x | 3.81x |
| 1,000,000| 0.96x | 1.74x | 3.29x | 3.96x |
| 2,000,000| 0.96x | 1.88x | 3.68x | 3.43x |

(Speedup = `T_seq / T_mpi(P)`; full per-run times in `summary_table.csv`.)

## Analysis

**Computation.** Per rank, local work is O(local_n) — a single linear
pass accumulating sums/min/max, updating two small hash maps (server,
endpoint), and one interval-count hash map. This is cheap and scales
perfectly with P; it is not the bottleneck at any size we tested (compute
time alone, isolated from I/O, was under 10ms even at N=2M).

**Data distribution.** This is the design's central point: distribution
is done as independent parallel file reads (byte-range file splitting), not
as a network transfer from a single source rank. At P=1 the whole job's
extra MPI overhead (one open/seek/read of the same file, one `Gatherv`)
costs about 1–5% over the plain sequential program — visible as
speedup(P=1) being slightly *below* 1.0 in the table above. That's the
honest cost of being an MPI program at all; it disappears once P>1 gives
back much more than that in parallel I/O throughput.

**Communication.** The only communication is the final reduction: 10
scalar `MPI_Reduce`s (O(1) each) plus 3 `Gather`/`Gatherv` pairs whose
size is proportional to the number of *distinct* keys, not to N. For our
generated datasets (50 servers, 100 endpoints, up to 1440 one-minute
intervals over a 24h window) this stays under a few thousand `long
long`/`double` values total, regardless of whether N is 10K or 2M — so
communication cost is essentially flat while computation and I/O grow
linearly with N. This is why efficiency climbs with N in the table above
(bigger jobs amortize the fixed reduction/startup cost over more useful
parallel work) and is a property that would degrade if the key space
itself scaled with N (e.g. one distinct endpoint per request) — the
gather size would then grow with N too, and this design would need real
key-hashing/partitioning across ranks instead of a full gather to rank 0.

**Scalability.** Speedup scales well from P=1→4 (roughly linear, e.g.
3.3–3.8x at P=4 for N≥100K) but flattens or slightly regresses from P=4→8
on our 16-core test machine, and efficiency correspondingly drops from
~0.9 to ~0.4–0.5 at P=8. The likely cause on a single machine is that 8
ranks contending for the same disk/page-cache and OS scheduler stop
getting proportionally more effective I/O bandwidth once you're using
half the machine's cores just for this job (other local system tasks +
OpenMPI's own bookkeeping share the rest) — this should look different
on the cluster's actual 2-node allocation, where P=8 gets real additional
hardware (a second node) rather than oversubscribing the same 16 cores.
Small inputs (N=10K) show poor/negative scaling at higher P for the
classic reason: fixed per-rank overhead (process startup, one file
open/seek, `MPI_Gather`/`MPI_Reduce` latency) stops being negligible
once the actual per-rank workload shrinks to a few hundred/thousand
records.

## Running on the cluster over SSH

```bash
scp -r log_analytics_q7 <user>@rce.iiit.ac.in:~/
ssh <user>@rce.iiit.ac.in
cd log_analytics_q7
sbatch benchmark.slurm
squeue -u $USER                       # watch it run
cat benchmark_<jobid>.log             # once finished
```
Bring results back to your local machine (run locally, not over SSH):
```bash
scp <user>@rce.iiit.ac.in:~/log_analytics_q7/{benchmark_results_*.csv,summary_table.csv,*.png} .
```
