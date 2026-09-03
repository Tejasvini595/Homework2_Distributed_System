#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

struct ServerStats {
    long long count = 0;
    double sumResponseTime = 0.0;
};

struct EndpointStats {
    long long count = 0;
    long long totalBytes = 0;
};

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::istream* in = &std::cin;
    std::ostream* out = &std::cout;
    std::ifstream fin;
    std::ofstream fout;

    if (argc >= 2) {
        fin.open(argv[1]);
        if (!fin) {
            std::cerr << "Error: cannot open input file " << argv[1] << "\n";
            return 1;
        }
        in = &fin;
    }
    if (argc >= 3) {
        fout.open(argv[2]);
        if (!fout) {
            std::cerr << "Error: cannot open output file " << argv[2] << "\n";
            return 1;
        }
        out = &fout;
    }

    long long N;
    long long K;
    long long S;
    (*in) >> N >> K >> S; // S (seed) is part of the input format but not needed for analytics

    long long totalRequests = 0, successful = 0, failed = 0;
    double sumResponseTime = 0.0;
    double minResponseTime = std::numeric_limits<double>::infinity();
    double maxResponseTime = -std::numeric_limits<double>::infinity();
    long long totalBytes = 0;
    long long status2xx = 0, status3xx = 0, status4xx = 0, status5xx = 0;

    std::unordered_map<int, ServerStats> serverMap;
    std::unordered_map<int, EndpointStats> endpointMap;
    std::unordered_map<long long, long long> intervalCount;

    serverMap.reserve(1024);
    endpointMap.reserve(1024);

    auto t0 = std::chrono::high_resolution_clock::now();

    for (long long i = 0; i < N; i++) 
    {
        long long timestamp;
        int server_id, endpoint_id, user_id, status_code;
        double response_time;
        long long bytes_sent;

        (*in) >> timestamp >> server_id >> endpoint_id >> user_id
              >> status_code >> response_time >> bytes_sent;

        totalRequests++;
        if (status_code < 400) 
        successful++; 
        else 
        failed++;

        sumResponseTime += response_time;
        if (response_time < minResponseTime) 
        minResponseTime = response_time;
        if (response_time > maxResponseTime) 
        maxResponseTime = response_time;

        totalBytes += bytes_sent;

        int cls = status_code / 100;
        if (cls == 2) status2xx++;
        else if (cls == 3) status3xx++;
        else if (cls == 4) status4xx++;
        else if (cls == 5) status5xx++;

        ServerStats& ss = serverMap[server_id];
        ss.count++;
        ss.sumResponseTime += response_time;

        EndpointStats& es = endpointMap[endpoint_id];
        es.count++;
        es.totalBytes += bytes_sent;

        long long interval = timestamp / 60;
        intervalCount[interval]++;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    double avgResponseTime = (totalRequests > 0) ? (sumResponseTime / totalRequests) : 0.0;
    if (totalRequests == 0) { minResponseTime = 0.0; maxResponseTime = 0.0; }

    // Busiest interval: highest count, tie -> smallest interval id
    long long busiestInterval = 0, busiestCount = 0;
    bool first = true;
    for (const auto& kv : intervalCount) {
        if (first || kv.second > busiestCount ||
            (kv.second == busiestCount && kv.first < busiestInterval)) {
            busiestInterval = kv.first;
            busiestCount = kv.second;
            first = false;
        }
    }

    // Top-K servers by request count (desc), tie -> id asc
    std::vector<std::pair<int, ServerStats>> servers(serverMap.begin(), serverMap.end());
    std::sort(servers.begin(), servers.end(), [](const auto& a, const auto& b) {
        if (a.second.count != b.second.count) return a.second.count > b.second.count;
        return a.first < b.first;
    });

    // Top-K endpoints by request count (desc), tie -> id asc
    std::vector<std::pair<int, EndpointStats>> endpoints(endpointMap.begin(), endpointMap.end());
    std::sort(endpoints.begin(), endpoints.end(), [](const auto& a, const auto& b) {
        if (a.second.count != b.second.count) return a.second.count > b.second.count;
        return a.first < b.first;
    });

    (*out) << std::fixed << std::setprecision(2);
    (*out) << "TOTAL_REQUESTS " << totalRequests << "\n";
    (*out) << "SUCCESSFUL_REQUESTS " << successful << "\n";
    (*out) << "FAILED_REQUESTS " << failed << "\n";
    (*out) << "AVERAGE_RESPONSE_TIME " << avgResponseTime << "\n";
    (*out) << "MIN_RESPONSE_TIME " << minResponseTime << "\n";
    (*out) << "MAX_RESPONSE_TIME " << maxResponseTime << "\n";
    (*out) << "TOTAL_BYTES " << totalBytes << "\n";
    (*out) << "STATUS_2XX " << status2xx << "\n";
    (*out) << "STATUS_3XX " << status3xx << "\n";
    (*out) << "STATUS_4XX " << status4xx << "\n";
    (*out) << "STATUS_5XX " << status5xx << "\n";
    (*out) << "BUSIEST_INTERVAL " << busiestInterval << " " << busiestCount << "\n";

    (*out) << "TOP_SERVERS\n";
    long long kServers = std::min<long long>(K, (long long)servers.size());
    for (long long i = 0; i < kServers; i++) {
        int id = servers[i].first;
        const ServerStats& ss = servers[i].second;
        double avg = (ss.count > 0) ? (ss.sumResponseTime / ss.count) : 0.0;
        (*out) << id << " " << ss.count << " " << avg << "\n";
    }

    (*out) << "TOP_ENDPOINTS\n";
    long long kEndpoints = std::min<long long>(K, (long long)endpoints.size());
    for (long long i = 0; i < kEndpoints; i++) {
        int id = endpoints[i].first;
        const EndpointStats& es = endpoints[i].second;
        (*out) << id << " " << es.count << " " << es.totalBytes << "\n";
    }

    std::cerr << "Processed N=" << N << " requests in " << elapsed << " sec\n";

    return 0;
}
