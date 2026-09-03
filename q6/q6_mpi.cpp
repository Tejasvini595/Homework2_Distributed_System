// q6_mpi.cpp
// Parallel Connected Components via minimum-label propagation (MPI).
//
// Algorithm:
//   Each vertex starts with component id = its own vertex id.
//   Each round: every vertex takes the MIN of its current label and the
//   current labels of all its neighbors. Repeat until no label changes
//   anywhere (global fixed point). This converges to: component id of a
//   vertex = minimum vertex id in its connected component.
//
// Note: convergence takes as many rounds as the diameter of the largest
// component (not O(log V)). For graphs with large diameter (e.g. long
// chains) this can be slow; see README for discussion.

#include <mpi.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace std;

// ------------------------------------------------------------
// Determine the contiguous range of vertices owned by a rank.
// ------------------------------------------------------------
void get_range(int V, int P, int rank, int &start, int &end)
{
    int base = V / P;
    int rem = V % P;

    start = rank * base + min(rank, rem);
    end = start + base + (rank < rem ? 1 : 0);
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int rank, P;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &P);

    // --------------------------------------------------------
    // Input file
    // --------------------------------------------------------
    if (argc < 2)
    {
        if (rank == 0)
            cerr << "Usage: mpirun -np <P> ./q6_mpi input.txt\n";
        MPI_Finalize();
        return 1;
    }

    string filename = argv[1];
    int V;
    vector<vector<int>> graph;

    // --------------------------------------------------------
    // Process 0 reads the graph and defensively symmetrizes it.
    //
    // The propagation step below only walks edges as listed for a
    // vertex; it assumes that if u is a neighbor of v, v is also
    // listed as a neighbor of u. If the input format guarantees
    // this already, the extra pass below is a cheap no-op-ish
    // safety net (it dedupes as it goes) against a generator bug.
    // --------------------------------------------------------
    if (rank == 0)
    {
        ifstream fin(filename);
        if (!fin)
        {
            cerr << "Cannot open input file\n";
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        fin >> V;
        graph.resize(V);

        for (int i = 0; i < V; i++)
        {
            int k;
            fin >> k;
            graph[i].resize(k);
            for (int j = 0; j < k; j++)
                fin >> graph[i][j];
        }
        fin.close();

        // Symmetrize + dedupe (also drops self-loops, which are
        // harmless for min-propagation but pointless to keep).
        vector<vector<char>> present; // not used; do it via sort+unique per vertex after adding reverse edges
        for (int v = 0; v < V; v++)
        {
            for (int u : graph[v])
            {
                if (u == v) continue;
                graph[u].push_back(v);
            }
        }
        for (int v = 0; v < V; v++)
        {
            sort(graph[v].begin(), graph[v].end());
            graph[v].erase(unique(graph[v].begin(), graph[v].end()), graph[v].end());
            graph[v].erase(remove(graph[v].begin(), graph[v].end(), v), graph[v].end());
        }
    }

    // Send V to everyone
    MPI_Bcast(&V, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // --------------------------------------------------------
    // Determine which vertices each process owns
    // --------------------------------------------------------
    int start, end;
    get_range(V, P, rank, start, end);
    int local_n = end - start;

    // --------------------------------------------------------
    // Distribute adjacency lists (flattened: degree array + edge array)
    // --------------------------------------------------------
    vector<int> local_degree(local_n);
    vector<int> local_edges;

    if (rank == 0)
    {
        for (int p = 0; p < P; p++)
        {
            int pstart, pend;
            get_range(V, P, p, pstart, pend);

            vector<int> degrees;
            vector<int> edges;

            for (int v = pstart; v < pend; v++)
            {
                degrees.push_back((int)graph[v].size());
                for (int u : graph[v])
                    edges.push_back(u);
            }

            if (p == 0)
            {
                local_degree = degrees;
                local_edges = edges;
            }
            else
            {
                int degree_count = (int)degrees.size();
                int edge_count = (int)edges.size();

                MPI_Send(&degree_count, 1, MPI_INT, p, 100, MPI_COMM_WORLD);
                if (degree_count > 0)
                    MPI_Send(degrees.data(), degree_count, MPI_INT, p, 101, MPI_COMM_WORLD);

                MPI_Send(&edge_count, 1, MPI_INT, p, 102, MPI_COMM_WORLD);
                if (edge_count > 0)
                    MPI_Send(edges.data(), edge_count, MPI_INT, p, 103, MPI_COMM_WORLD);
            }
        }
        // rank 0 no longer needs the full adjacency list
        graph.clear();
        graph.shrink_to_fit();
    }
    else
    {
        int degree_count;
        MPI_Recv(&degree_count, 1, MPI_INT, 0, 100, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        local_degree.resize(degree_count);
        if (degree_count > 0)
            MPI_Recv(local_degree.data(), degree_count, MPI_INT, 0, 101, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        int edge_count;
        MPI_Recv(&edge_count, 1, MPI_INT, 0, 102, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        local_edges.resize(edge_count);
        if (edge_count > 0)
            MPI_Recv(local_edges.data(), edge_count, MPI_INT, 0, 103, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    // --------------------------------------------------------
    // Initial component IDs: local_component[i] <-> vertex (start+i)
    // --------------------------------------------------------
    vector<int> local_component(local_n);
    for (int i = 0; i < local_n; i++)
        local_component[i] = start + i;

    // --------------------------------------------------------
    // Allgatherv layout (fixed for the whole run)
    // --------------------------------------------------------
    vector<int> recvcounts(P), displacements(P);
    for (int p = 0; p < P; p++)
    {
        int pstart, pend;
        get_range(V, P, p, pstart, pend);
        recvcounts[p] = pend - pstart;
    }
    displacements[0] = 0;
    for (int p = 1; p < P; p++)
        displacements[p] = displacements[p - 1] + recvcounts[p - 1];

    vector<int> global_component(V);

    // --------------------------------------------------------
    // Timing starts here (excludes I/O + distribution setup above)
    // --------------------------------------------------------
    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();

    // --------------------------------------------------------
    // Label propagation to fixed point
    // --------------------------------------------------------
    int iterations = 0;
    // Safety cap: correct execution should never need more than V
    // rounds (diameter <= V-1). Guards against a logic bug hanging.
    const int MAX_ITER = V + 2;

    // Cumulative per-phase timers (this rank's totals across all
    // rounds), used to break down where time actually goes.
    double comp_time_local = 0.0;  // local vertex/edge scan (the min-update loop)
    double comm_time_local = 0.0;  // MPI_Allgatherv + MPI_Allreduce combined

    while (true)
    {
        iterations++;

        double t_comm0 = MPI_Wtime();
        MPI_Allgatherv(
            local_component.data(), local_n, MPI_INT,
            global_component.data(), recvcounts.data(), displacements.data(), MPI_INT,
            MPI_COMM_WORLD);
        comm_time_local += MPI_Wtime() - t_comm0;

        double t_comp0 = MPI_Wtime();
        vector<int> new_component = local_component;
        int edge_index = 0;
        bool local_changed = false;

        for (int i = 0; i < local_n; i++)
        {
            int best = local_component[i];
            int degree = local_degree[i];

            for (int j = 0; j < degree; j++)
            {
                int neighbor = local_edges[edge_index++];
                best = min(best, global_component[neighbor]);
            }

            new_component[i] = best;
            if (new_component[i] != local_component[i])
                local_changed = true;
        }

        local_component.swap(new_component);
        comp_time_local += MPI_Wtime() - t_comp0;

        int local_flag = local_changed ? 1 : 0;
        int global_flag;

        double t_comm1 = MPI_Wtime();
        MPI_Allreduce(&local_flag, &global_flag, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
        comm_time_local += MPI_Wtime() - t_comm1;

        if (global_flag == 0)
            break;

        if (iterations >= MAX_ITER)
        {
            if (rank == 0)
                cerr << "WARNING: exceeded MAX_ITER (" << MAX_ITER
                     << ") without converging - possible bug\n";
            break;
        }
    }

    // Final gather (last iteration's gather already reflects final state,
    // but re-gather explicitly for clarity/robustness)
    MPI_Allgatherv(
        local_component.data(), local_n, MPI_INT,
        global_component.data(), recvcounts.data(), displacements.data(), MPI_INT,
        MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    double end_time = MPI_Wtime();
    double local_time = end_time - start_time;
    double total_time, total_comp_time, total_comm_time;
    MPI_Reduce(&local_time, &total_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&comp_time_local, &total_comp_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&comm_time_local, &total_comm_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // --------------------------------------------------------
    // Output: exactly V lines of "vertex_id component_id", sorted
    // by vertex id ascending. Nothing else goes to stdout.
    // --------------------------------------------------------
    if (rank == 0)
    {
        ios_base::sync_with_stdio(false);
        ostringstream oss;
        for (int v = 0; v < V; v++)
            oss << v << ' ' << global_component[v] << '\n';
        cout << oss.str();

        cerr << "Iterations: " << iterations << "\n";
        cerr << "Time: " << total_time << " seconds\n";
        cerr << "CompTime: " << total_comp_time << " seconds\n";
        cerr << "CommTime: " << total_comm_time << " seconds\n";
        cerr << "OtherTime: " << max(0.0, total_time - total_comp_time - total_comm_time) << " seconds\n";
        cerr << "P: " << P << "  V: " << V << "\n";
    }

    MPI_Finalize();
    return 0;
}
