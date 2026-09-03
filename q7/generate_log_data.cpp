#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " N K SEED [num_servers] [num_endpoints] [num_users] [time_range_seconds] [output_file]\n";
        return 1;
    }

    long long N = atoll(argv[1]);
    long long K = atoll(argv[2]);
    unsigned int seed = (unsigned int)atoll(argv[3]);

    int numServers   = (argc >= 5) ? atoi(argv[4]) : 50;
    int numEndpoints = (argc >= 6) ? atoi(argv[5]) : 100;
    int numUsers     = (argc >= 7) ? atoi(argv[6]) : 10000;
    long long timeRange = (argc >= 8) ? atoll(argv[7]) : 86400; // 24h, in seconds

    std::string outputFile;
    if (argc >= 9) {
        outputFile = argv[8];
    } else {
        outputFile = "log_data_" + std::to_string(N) + "_" + std::to_string(seed) + ".txt";
    }

    if (N <= 0 || K <= 0 || numServers <= 0 || numEndpoints <= 0 || numUsers <= 0 || timeRange <= 0) {
        std::cerr << "Error: N, K, num_servers, num_endpoints, num_users, time_range_seconds must all be positive.\n";
        return 1;
    }

    std::mt19937 rng(seed); // deterministic given the same seed

    std::uniform_int_distribution<long long> timestampDist(0, timeRange - 1);
    std::uniform_int_distribution<int> serverDist(1, numServers);
    std::uniform_int_distribution<int> endpointDist(1, numEndpoints);
    std::uniform_int_distribution<int> userDist(1, numUsers);
    std::uniform_int_distribution<long long> bytesDist(100, 100000);
    std::uniform_real_distribution<double> responseTimeDist(1.0, 2000.0);

    // Realistic-ish status code mix: mostly 2xx, some 3xx, fewer 4xx/5xx.
    std::vector<int> statusCodes = {200, 201, 204, 301, 302, 400, 401, 403, 404, 500, 502, 503};
    std::vector<double> statusWeights = {50, 10, 5, 5, 3, 6, 4, 3, 6, 3, 3, 2}; // sums to 100
    std::discrete_distribution<int> statusDist(statusWeights.begin(), statusWeights.end());

    std::ofstream out(outputFile);
    if (!out) {
        std::cerr << "Error: cannot open output file " << outputFile << "\n";
        return 1;
    }

    out << N << " " << K << " " << seed << "\n";

    // Reserve a reasonably sized buffer to reduce I/O overhead for large N.
    std::string buffer;
    buffer.reserve(1 << 20);

    for (long long i = 0; i < N; i++) {
        long long ts = timestampDist(rng);
        int server = serverDist(rng);
        int endpoint = endpointDist(rng);
        int user = userDist(rng);
        int status = statusCodes[statusDist(rng)];
        double rt = responseTimeDist(rng);
        long long bytes = bytesDist(rng);

        char line[256];
        int len = snprintf(line, sizeof(line), "%lld %d %d %d %d %.2f %lld\n",
                            ts, server, endpoint, user, status, rt, bytes);
        buffer.append(line, len);

        if (buffer.size() > (1 << 20)) {
            out << buffer;
            buffer.clear();
        }
    }
    if (!buffer.empty()) out << buffer;

    out.close();

    std::cerr << "Generated " << N << " log entries -> " << outputFile << "\n";
    std::cerr << "Parameters: K=" << K << " SEED=" << seed
              << " num_servers=" << numServers << " num_endpoints=" << numEndpoints
              << " num_users=" << numUsers << " time_range_seconds=" << timeRange << "\n";

    return 0;
}
