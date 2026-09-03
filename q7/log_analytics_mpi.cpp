#include <mpi.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace std;

struct ServerStats 
{
    long long count = 0;
    double sumResponseTime = 0.0;
};

struct EndpointStats 
{
    long long count = 0;
    long long totalBytes = 0;
};

// Return the offset of the start of the next full line at or after byte offset b
// data_start and file_size are the exact boundaries of the record
// area (right after the header line, and EOF) and are returned unchanged
// since they are already line-aligned.
static long long skip_to_line_start(ifstream &fin, long long b, long long data_start, long long file_size) 
{
    if (b <= data_start) 
    return data_start;
    if (b >= file_size) 
    return file_size;

    // If the byte right before b is already a newline, b is already a
    // line start, return it unchanged, otherwise the scan below would
    // consume the whole following line and overshoot by one line.
    fin.clear();
    fin.seekg(b - 1);
    char prev;
    fin.get(prev);
    if (prev == '\n') return b;

    fin.clear();
    fin.seekg(b);
    char c;
    while (fin.get(c)) {
        if (c == '\n') break;
    }
    long long pos = (long long)fin.tellg();
    if (pos < 0) return file_size; // ran off EOF while scanning for a newline
    return pos;
}

int main(int argc, char **argv) 
{
    MPI_Init(&argc, &argv);

    int rank, P;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &P);

    if (argc < 2) 
    {
        if (rank == 0)
            cerr << "Usage: mpirun -np <P> ./log_analytics_mpi input.txt [output.txt]\n";
        MPI_Finalize();
        return 1;
    }

    // Every rank opens the file itself and figures out its own byte range no communication needed to distribute the raw records.
    ifstream fin(argv[1]);
    if (!fin) 
    {
        cerr << "Error: cannot open input file " << argv[1] << " (rank " << rank << ")\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    long long N, K, S;
    fin >> N >> K >> S;
    fin.ignore(numeric_limits<streamsize>::max(), '\n');
    long long data_start = (long long)fin.tellg();

    fin.clear();
    fin.seekg(0, ios::end);
    long long file_size = (long long)fin.tellg();
    long long data_size = file_size - data_start;

    long long raw_start = data_start + (rank * data_size) / P;
    long long raw_end = data_start + ((long long)(rank + 1) * data_size) / P;

    long long my_start = skip_to_line_start(fin, raw_start, data_start, file_size);
    long long my_end = skip_to_line_start(fin, raw_end, data_start, file_size);

    MPI_Barrier(MPI_COMM_WORLD);
    double t_io_compute_start = MPI_Wtime();

    // Read this rank's exact byte range into a private buffer, then
    // parse it with its own istringstream. Parsing from a buffer that
    // contains exactly this rank's whole lines (and nothing else) avoids
    // any reliance on mid-stream tellg() positions formatted >>
    // extraction leaves the file stream sitting right after the last
    // digit of a field (before the trailing newline), which makes
    // position-based "have I crossed my byte boundary yet" checks on
    // the shared ifstream unreliable. Reading the slice up front sidesteps
    // that the buffer's own end is standard EOF, no boundary tracking
    // needed during parsing.
    long long chunk_len = my_end - my_start;
    string buffer;
    if (chunk_len > 0) 
    {
        buffer.resize((size_t)chunk_len);
        fin.clear();
        fin.seekg(my_start);
        fin.read(&buffer[0], chunk_len);
    }
    fin.close();

    long long localSuccessful = 0, localFailed = 0, localN = 0;
    double localSumResponseTime = 0.0;
    double localMinResponseTime = numeric_limits<double>::infinity();
    double localMaxResponseTime = -numeric_limits<double>::infinity();
    long long localTotalBytes = 0;
    long long localStatus2xx = 0, localStatus3xx = 0, localStatus4xx = 0, localStatus5xx = 0;

    unordered_map<int, ServerStats> localServerMap;
    unordered_map<int, EndpointStats> localEndpointMap;
    unordered_map<long long, long long> localIntervalCount;

    istringstream iss(buffer);

    long long timestamp, bytes_sent;
    int server_id, endpoint_id, user_id_unused, status_code;
    double response_time;

    while (iss >> timestamp >> server_id >> endpoint_id >> user_id_unused
               >> status_code >> response_time >> bytes_sent) 
    {
        localN++;
        if (status_code < 400) 
        localSuccessful++; 
        else 
        localFailed++;

        localSumResponseTime += response_time;
        if (response_time < localMinResponseTime) 
        localMinResponseTime = response_time;
        if (response_time > localMaxResponseTime) 
        localMaxResponseTime = response_time;

        localTotalBytes += bytes_sent;

        int cls = status_code / 100;
        if (cls == 2) 
        localStatus2xx++;
        else if (cls == 3) 
        localStatus3xx++;
        else if (cls == 4) 
        localStatus4xx++;
        else if (cls == 5) 
        localStatus5xx++;

        ServerStats &ss = localServerMap[server_id];
        ss.count++;
        ss.sumResponseTime += response_time;

        EndpointStats &es = localEndpointMap[endpoint_id];
        es.count++;
        es.totalBytes += bytes_sent;

        localIntervalCount[timestamp / 60]++;
    }

    double t_io_compute_end = MPI_Wtime();
    double t_reduce_start = t_io_compute_end;

    long long totalRequests = N, successful = 0, failed = 0, totalBytes = 0;
    long long status2xx = 0, status3xx = 0, status4xx = 0, status5xx = 0;
    double sumResponseTime = 0.0, minResponseTime = 0.0, maxResponseTime = 0.0;

    MPI_Reduce(&localSuccessful, &successful, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&localFailed, &failed, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&localSumResponseTime, &sumResponseTime, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&localMinResponseTime, &minResponseTime, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
    MPI_Reduce(&localMaxResponseTime, &maxResponseTime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&localTotalBytes, &totalBytes, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&localStatus2xx, &status2xx, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&localStatus3xx, &status3xx, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&localStatus4xx, &status4xx, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&localStatus5xx, &status5xx, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    // Gather + merge the three hash maps (server, endpoint, interval). Each rank sends its distinct keys and per-key aggregates. sizes scale with the number of distinct keys, not with N.
    auto gatherServerMap = [&](unordered_map<int, ServerStats> &local) -> unordered_map<int, ServerStats> 
    {
        int localCount = (int)local.size();
        vector<int> ids; 
        ids.reserve(localCount);
        vector<long long> counts; 
        counts.reserve(localCount);
        vector<double> sums; 
        sums.reserve(localCount);
        for (auto &kv : local) 
        {
            ids.push_back(kv.first);
            counts.push_back(kv.second.count);
            sums.push_back(kv.second.sumResponseTime);
        }

        vector<int> recvCounts(P);
        MPI_Gather(&localCount, 1, MPI_INT, recvCounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

        vector<int> recvDispls(P, 0);
        int total = 0;
        if (rank == 0) 
        {
            for (int p = 0; p < P; p++) 
            { 
                recvDispls[p] = total; total += recvCounts[p]; 
            }
        }

        vector<int> allIds(rank == 0 ? total : 0);
        vector<long long> allCounts(rank == 0 ? total : 0);
        vector<double> allSums(rank == 0 ? total : 0);

        MPI_Gatherv(ids.data(), localCount, MPI_INT, allIds.data(), recvCounts.data(), recvDispls.data(), MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Gatherv(counts.data(), localCount, MPI_LONG_LONG, allCounts.data(), recvCounts.data(), recvDispls.data(), MPI_LONG_LONG, 0, MPI_COMM_WORLD);
        MPI_Gatherv(sums.data(), localCount, MPI_DOUBLE, allSums.data(), recvCounts.data(), recvDispls.data(), MPI_DOUBLE, 0, MPI_COMM_WORLD);

        unordered_map<int, ServerStats> merged;
        if (rank == 0) 
        {
            merged.reserve(total);
            for (int i = 0; i < total; i++) 
            {
                ServerStats &s = merged[allIds[i]];
                s.count += allCounts[i];
                s.sumResponseTime += allSums[i];
            }
        }
        return merged;
    };

    auto gatherEndpointMap = [&](unordered_map<int, EndpointStats> &local) -> unordered_map<int, EndpointStats> 
    {
        int localCount = (int)local.size();
        vector<int> ids; ids.reserve(localCount);
        vector<long long> counts; counts.reserve(localCount);
        vector<long long> bytes; bytes.reserve(localCount);
        for (auto &kv : local) 
        {
            ids.push_back(kv.first);
            counts.push_back(kv.second.count);
            bytes.push_back(kv.second.totalBytes);
        }

        vector<int> recvCounts(P);
        MPI_Gather(&localCount, 1, MPI_INT, recvCounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

        vector<int> recvDispls(P, 0);
        int total = 0;
        if (rank == 0) {
            for (int p = 0; p < P; p++) 
            { 
                recvDispls[p] = total; total += recvCounts[p]; 
            }
        }

        vector<int> allIds(rank == 0 ? total : 0);
        vector<long long> allCounts(rank == 0 ? total : 0);
        vector<long long> allBytes(rank == 0 ? total : 0);

        MPI_Gatherv(ids.data(), localCount, MPI_INT,
                    allIds.data(), recvCounts.data(), recvDispls.data(), MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Gatherv(counts.data(), localCount, MPI_LONG_LONG,
                    allCounts.data(), recvCounts.data(), recvDispls.data(), MPI_LONG_LONG, 0, MPI_COMM_WORLD);
        MPI_Gatherv(bytes.data(), localCount, MPI_LONG_LONG,
                    allBytes.data(), recvCounts.data(), recvDispls.data(), MPI_LONG_LONG, 0, MPI_COMM_WORLD);

        unordered_map<int, EndpointStats> merged;
        if (rank == 0) 
        {
            merged.reserve(total);
            for (int i = 0; i < total; i++) 
            {
                EndpointStats &e = merged[allIds[i]];
                e.count += allCounts[i];
                e.totalBytes += allBytes[i];
            }
        }
        return merged;
    };

    auto gatherIntervalMap = [&](unordered_map<long long, long long> &local) -> unordered_map<long long, long long> {
        int localCount = (int)local.size();
        vector<long long> ids; ids.reserve(localCount);
        vector<long long> counts; counts.reserve(localCount);
        for (auto &kv : local) {
            ids.push_back(kv.first);
            counts.push_back(kv.second);
        }

        vector<int> recvCounts(P);
        MPI_Gather(&localCount, 1, MPI_INT, recvCounts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

        vector<int> recvDispls(P, 0);
        int total = 0;
        if (rank == 0) {
            for (int p = 0; p < P; p++) 
            { 
                recvDispls[p] = total; total += recvCounts[p]; 
            }
        }

        vector<long long> allIds(rank == 0 ? total : 0);
        vector<long long> allCounts(rank == 0 ? total : 0);

        MPI_Gatherv(ids.data(), localCount, MPI_LONG_LONG,
                    allIds.data(), recvCounts.data(), recvDispls.data(), MPI_LONG_LONG, 0, MPI_COMM_WORLD);
        MPI_Gatherv(counts.data(), localCount, MPI_LONG_LONG,
                    allCounts.data(), recvCounts.data(), recvDispls.data(), MPI_LONG_LONG, 0, MPI_COMM_WORLD);

        unordered_map<long long, long long> merged;
        if (rank == 0) {
            merged.reserve(total);
            for (int i = 0; i < total; i++) {
                merged[allIds[i]] += allCounts[i];
            }
        }
        return merged;
    };

    unordered_map<int, ServerStats> serverMap = gatherServerMap(localServerMap);
    unordered_map<int, EndpointStats> endpointMap = gatherEndpointMap(localEndpointMap);
    unordered_map<long long, long long> intervalCount = gatherIntervalMap(localIntervalCount);
    double t_reduce_end = MPI_Wtime();

    MPI_Barrier(MPI_COMM_WORLD);
    double t_total_end = MPI_Wtime();

    // Every rank's local durations differ (uneven byte-range split, uneven
    // CPU scheduling) the program isn't done until the slowest rank is,
    // so reduce with MAX to get the true parallel time rather than
    // whatever rank 0 individually measured.
    double local_io_compute = t_io_compute_end - t_io_compute_start;
    double local_reduce = t_reduce_end - t_reduce_start;
    double local_total = t_total_end - t_io_compute_start;
    double max_io_compute = 0.0, max_reduce = 0.0, max_total = 0.0;
    MPI_Reduce(&local_io_compute, &max_io_compute, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_reduce, &max_reduce, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_total, &max_total, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // Rank 0: finalize, sort, and write output.
    if (rank == 0) 
    {
        double avgResponseTime = (totalRequests > 0) ? (sumResponseTime / totalRequests) : 0.0;
        if (totalRequests == 0) 
        { 
            minResponseTime = 0.0; maxResponseTime = 0.0; 
        }

        long long busiestInterval = 0, busiestCount = 0;
        bool first = true;
        for (const auto &kv : intervalCount) {
            if (first || kv.second > busiestCount || (kv.second == busiestCount && kv.first < busiestInterval)) {
                busiestInterval = kv.first;
                busiestCount = kv.second;
                first = false;
            }
        }

        vector<pair<int, ServerStats>> servers(serverMap.begin(), serverMap.end());
        sort(servers.begin(), servers.end(), [](const auto &a, const auto &b) {
            if (a.second.count != b.second.count) 
            return a.second.count > b.second.count;
            return a.first < b.first;
        });

        vector<pair<int, EndpointStats>> endpoints(endpointMap.begin(), endpointMap.end());
        sort(endpoints.begin(), endpoints.end(), [](const auto &a, const auto &b) {
            if (a.second.count != b.second.count) return a.second.count > b.second.count;
            return a.first < b.first;
        });

        ostream *out = &cout;
        ofstream fout;
        if (argc >= 3) {
            fout.open(argv[2]);
            if (!fout) {
                cerr << "Error: cannot open output file " << argv[2] << "\n";
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            out = &fout;
        }

        (*out) << fixed << setprecision(2);
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
        long long kServers = min<long long>(K, (long long)servers.size());
        for (long long i = 0; i < kServers; i++) {
            int id = servers[i].first;
            const ServerStats &ss = servers[i].second;
            double avg = (ss.count > 0) ? (ss.sumResponseTime / ss.count) : 0.0;
            (*out) << id << " " << ss.count << " " << avg << "\n";
        }

        (*out) << "TOP_ENDPOINTS\n";
        long long kEndpoints = min<long long>(K, (long long)endpoints.size());
        for (long long i = 0; i < kEndpoints; i++) 
        {
            int id = endpoints[i].first;
            const EndpointStats &es = endpoints[i].second;
            (*out) << id << " " << es.count << " " << es.totalBytes << "\n";
        }

        cerr << "Processed N=" << N << " requests with P=" << P << " ranks\n";
        cerr << "Parallel read+compute time (max over ranks): " << max_io_compute << " sec\n";
        cerr << "Reduction/merge time:      " << max_reduce << " sec\n";
        cerr << "Total time:                " << max_total << " sec\n";
    }

    MPI_Finalize();
    return 0;
}
