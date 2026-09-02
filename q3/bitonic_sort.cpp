// bitonic_sort.cpp
// Distributed Bitonic Sort using MPI
// Homework 2 - Distributed Algorithms - Q3
//
// Usage:
//   mpirun -np P ./bitonic_sort N [seed]
//
// Requirements:
//   - N and P must both be powers of 2
//   - N must be divisible by P
//
// Compilation:
//   mpic++ -O2 -std=c++17 -o bitonic_sort bitonic_sort.cpp

#include <mpi.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <vector>

// -------------------- Helpers --------------------

static bool isPowerOfTwo(long long x) {
    return x > 0 && (x & (x - 1)) == 0;
}

static int log2i(int x) {
    int r = 0;
    while ((1 << r) < x) r++;
    return r;
}

// Merge two ascending-sorted vectors 'mine' and 'other' (equal length n each)
// and keep either the lower n elements or the upper n elements of the
// merged (2n)-length ascending sequence, writing the result back into
// 'mine' (still ascending).
static void compareSplit(std::vector<int>& mine, const std::vector<int>& other, bool keepLow) {
    int n = (int)mine.size();
    std::vector<int> merged;
    merged.reserve(2 * n);

    int i = 0, j = 0;
    while (i < n && j < n) {
        if (mine[i] <= other[j]) merged.push_back(mine[i++]);
        else merged.push_back(other[j++]);
    }
    while (i < n) merged.push_back(mine[i++]);
    while (j < n) merged.push_back(other[j++]);

    if (keepLow) {
        mine.assign(merged.begin(), merged.begin() + n);
    } else {
        mine.assign(merged.begin() + n, merged.end());
    }
}

// -------------------- Main --------------------

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, P;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &P);

    if (argc < 2) {
        if (rank == 0) {
            fprintf(stderr, "Usage: %s N [seed]\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    long long N = atoll(argv[1]);
    unsigned int seed = (argc >= 3) ? (unsigned int)atoll(argv[2]) : 42u;

    // ---- Validate constraints ----
    if (!isPowerOfTwo(P)) {
        if (rank == 0) fprintf(stderr, "Error: number of processes P=%d must be a power of 2.\n", P);
        MPI_Finalize();
        return 1;
    }
    if (!isPowerOfTwo(N)) {
        if (rank == 0) fprintf(stderr, "Error: N=%lld must be a power of 2.\n", N);
        MPI_Finalize();
        return 1;
    }
    if (N % P != 0) {
        if (rank == 0) fprintf(stderr, "Error: N=%lld must be divisible by P=%d.\n", N, P);
        MPI_Finalize();
        return 1;
    }

    int localN = (int)(N / P);

    // ---- Generate / distribute data (Master = rank 0) ----
    std::vector<int> fullData;
    if (rank == 0) {
        fullData.resize(N);
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> dist(-1000000, 1000000);
        for (long long k = 0; k < N; k++) fullData[k] = dist(rng);
    }

    std::vector<int> local(localN);
    MPI_Scatter(rank == 0 ? fullData.data() : nullptr, localN, MPI_INT,
                local.data(), localN, MPI_INT,
                0, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();

    double comp_time = 0.0;
    double comm_time = 0.0;

    // ---- Step 1: local sort (ascending) ----
    // Note on algorithm formulation: Each rank sorts its local chunk ascending.
    // The direction of pairwise compares in the hypercube network (keepLow)
    // is dynamically determined by rank bit combinations (Grama et al. formulation),
    // which is mathematically equivalent to alternating initial sort directions.
    double tc0 = MPI_Wtime();
    std::sort(local.begin(), local.end());
    comp_time += (MPI_Wtime() - tc0);

    // ---- Step 2: bitonic merge network across processes ----
    int d = log2i(P); // number of processes = 2^d

    for (int i = 0; i < d; i++) {
        for (int j = i; j >= 0; j--) {
            int partner = rank ^ (1 << j);

            // ascending block if bit (i+1) of rank is 0
            bool ascendingBlock = (((rank >> (i + 1)) & 1) == 0);

            bool keepLow;
            if (rank < partner) {
                keepLow = ascendingBlock;   // ascending -> keep smaller half
            } else {
                keepLow = !ascendingBlock;  // descending -> keep larger half
            }

            std::vector<int> other(localN);

            double tm0 = MPI_Wtime();
            MPI_Sendrecv(local.data(), localN, MPI_INT, partner, 0,
                         other.data(), localN, MPI_INT, partner, 0,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            comm_time += (MPI_Wtime() - tm0);

            tc0 = MPI_Wtime();
            compareSplit(local, other, keepLow);
            comp_time += (MPI_Wtime() - tc0);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double t1 = MPI_Wtime();
    double total_time = t1 - t0;

    double max_comp_time = 0.0;
    double max_comm_time = 0.0;
    MPI_Reduce(&comp_time, &max_comp_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&comm_time, &max_comm_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // ---- Step 3: gather sorted chunks back at master ----
    std::vector<int> sorted;
    if (rank == 0) sorted.resize(N);
    MPI_Gather(local.data(), localN, MPI_INT,
               rank == 0 ? sorted.data() : nullptr, localN, MPI_INT,
               0, MPI_COMM_WORLD);

    if (rank == 0) {
        // ---- Correctness verification against sequential sort ----
        std::vector<int> reference = fullData;
        std::sort(reference.begin(), reference.end());

        bool correct = (reference == sorted);

        fprintf(stderr, "N=%lld P=%d  time=%.6f sec  comp_time=%.6f sec  comm_time=%.6f sec  correctness=%s\n",
                N, P, total_time, max_comp_time, max_comm_time, correct ? "PASS" : "FAIL");

        // Print sorted output (redirect to a file for large N; see README)
        if (N <= 1000) {
            for (long long k = 0; k < N; k++) {
                printf("%d%c", sorted[k], (k + 1 < N) ? ' ' : '\n');
            }
        } else {
            // For large N, avoid flooding stdout; just confirm.
            printf("Sorted %lld elements. correctness=%s\n", N, correct ? "PASS" : "FAIL");
        }
    }

    MPI_Finalize();
    return 0;
}
