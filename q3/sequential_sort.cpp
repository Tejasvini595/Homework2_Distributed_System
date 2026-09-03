// sequential_sort.cpp
// Sequential baseline sort, used ONLY to independently verify the MPI
// bitonic sort's correctness and to compute speedup/efficiency.
//
// Usage: ./sequential_sort N [seed]
// Generates the SAME input as bitonic_sort.cpp (same seed => same data),
// sorts it sequentially, and reports the elapsed time.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s N [seed]\n", argv[0]);
        return 1;
    }
    long long N = atoll(argv[1]);
    unsigned int seed = (argc >= 3) ? (unsigned int)atoll(argv[2]) : 42u;

    std::vector<int> data(N);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(-1000000, 1000000);
    for (long long k = 0; k < N; k++) data[k] = dist(rng);

    auto t0 = std::chrono::high_resolution_clock::now();
    std::sort(data.begin(), data.end());
    auto t1 = std::chrono::high_resolution_clock::now();

    double secs = std::chrono::duration<double>(t1 - t0).count();
    fprintf(stderr, "N=%lld  sequential_time=%.6f sec\n", N, secs);

    if (N <= 1000) {
        for (long long k = 0; k < N; k++) printf("%d%c", data[k], (k + 1 < N) ? ' ' : '\n');
    } else {
        printf("Sorted %lld elements.\n", N);
    }
    return 0;
}
