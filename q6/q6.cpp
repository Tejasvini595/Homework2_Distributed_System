#include <mpi.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace std;


// ------------------------------------------------------------
// Find the owner process of vertex v.
// Vertices are divided into contiguous blocks.
// ------------------------------------------------------------
int owner_of(int v, int V, int P)
{
    int base = V / P;
    int rem = V % P;

    if (v < (base + 1) * rem)
        return v / (base + 1);

    return rem + (v - (base + 1) * rem) / base;
}


// ------------------------------------------------------------
// Determine the range of vertices owned by this process.
// ------------------------------------------------------------
void get_range(int V, int P, int rank, int &start, int &end)
{
    int base = V / P;
    int rem = V % P;

    start = rank * base + min(rank, rem);
    end = start + base + (rank < rem ? 1 : 0);
}


// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
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
        {
            cerr << "Usage: mpirun -np <P> ./cc_mpi input.txt\n";
        }

        MPI_Finalize();
        return 1;
    }

    string filename = argv[1];

    int V;

    vector<vector<int>> graph;


    // --------------------------------------------------------
    // Process 0 reads the graph
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
            {
                fin >> graph[i][j];
            }
        }

        fin.close();
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
    // Distribute adjacency lists
    //
    // For simplicity we flatten the adjacency list.
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
                degrees.push_back(graph[v].size());

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
                int degree_count = degrees.size();
                int edge_count = edges.size();

                MPI_Send(
                    &degree_count,
                    1,
                    MPI_INT,
                    p,
                    100,
                    MPI_COMM_WORLD
                );

                MPI_Send(
                    degrees.data(),
                    degree_count,
                    MPI_INT,
                    p,
                    101,
                    MPI_COMM_WORLD
                );

                MPI_Send(
                    &edge_count,
                    1,
                    MPI_INT,
                    p,
                    102,
                    MPI_COMM_WORLD
                );

                if (edge_count > 0)
                {
                    MPI_Send(
                        edges.data(),
                        edge_count,
                        MPI_INT,
                        p,
                        103,
                        MPI_COMM_WORLD
                    );
                }
            }
        }
    }
    else
    {
        int degree_count;

        MPI_Recv(
            &degree_count,
            1,
            MPI_INT,
            0,
            100,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );

        local_degree.resize(degree_count);

        MPI_Recv(
            local_degree.data(),
            degree_count,
            MPI_INT,
            0,
            101,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );


        int edge_count;

        MPI_Recv(
            &edge_count,
            1,
            MPI_INT,
            0,
            102,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );

        local_edges.resize(edge_count);

        if (edge_count > 0)
        {
            MPI_Recv(
                local_edges.data(),
                edge_count,
                MPI_INT,
                0,
                103,
                MPI_COMM_WORLD,
                MPI_STATUS_IGNORE
            );
        }
    }


    // --------------------------------------------------------
    // Initial component IDs
    //
    // local_component[i] corresponds to vertex start+i
    // --------------------------------------------------------

    vector<int> local_component(local_n);

    for (int i = 0; i < local_n; i++)
    {
        local_component[i] = start + i;
    }


    // --------------------------------------------------------
    // Build MPI_Allgatherv information
    // --------------------------------------------------------

    vector<int> recvcounts(P);
    vector<int> displacements(P);

    for (int p = 0; p < P; p++)
    {
        int pstart, pend;

        get_range(V, P, p, pstart, pend);

        recvcounts[p] = pend - pstart;
    }

    displacements[0] = 0;

    for (int p = 1; p < P; p++)
    {
        displacements[p] =
            displacements[p - 1] + recvcounts[p - 1];
    }


    // --------------------------------------------------------
    // Global component array
    // --------------------------------------------------------

    vector<int> global_component(V);


    // --------------------------------------------------------
    // Start timing
    // --------------------------------------------------------

    MPI_Barrier(MPI_COMM_WORLD);

    double start_time = MPI_Wtime();


    // --------------------------------------------------------
    // Label propagation
    // --------------------------------------------------------

    int iterations = 0;

    while (true)
    {
        iterations++;


        // ----------------------------------------------------
        // Share component IDs
        // ----------------------------------------------------

        MPI_Allgatherv(
            local_component.data(),
            local_n,
            MPI_INT,

            global_component.data(),
            recvcounts.data(),
            displacements.data(),
            MPI_INT,

            MPI_COMM_WORLD
        );


        // ----------------------------------------------------
        // Update local component IDs
        // ----------------------------------------------------

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

                best = min(
                    best,
                    global_component[neighbor]
                );
            }

            new_component[i] = best;

            if (new_component[i] != local_component[i])
            {
                local_changed = true;
            }
        }


        local_component.swap(new_component);


        // ----------------------------------------------------
        // Check whether ANY process changed
        // ----------------------------------------------------

        int local_flag = local_changed ? 1 : 0;

        int global_flag;

        MPI_Allreduce(
            &local_flag,
            &global_flag,
            1,
            MPI_INT,
            MPI_MAX,
            MPI_COMM_WORLD
        );


        if (global_flag == 0)
            break;
    }


    // --------------------------------------------------------
    // Final component array
    // --------------------------------------------------------

    MPI_Allgatherv(
        local_component.data(),
        local_n,
        MPI_INT,

        global_component.data(),
        recvcounts.data(),
        displacements.data(),
        MPI_INT,

        MPI_COMM_WORLD
    );


    // --------------------------------------------------------
    // End timing
    // --------------------------------------------------------

    MPI_Barrier(MPI_COMM_WORLD);

    double end_time = MPI_Wtime();

    double local_time = end_time - start_time;

    double total_time;

    MPI_Reduce(
        &local_time,
        &total_time,
        1,
        MPI_DOUBLE,
        MPI_MAX,
        0,
        MPI_COMM_WORLD
    );


    // --------------------------------------------------------
    // Output
    // --------------------------------------------------------

    if (rank == 0)
    {
        cout << "Component IDs:\n";

        for (int v = 0; v < V; v++)
        {
            cout
                << v << " "
                << global_component[v]
                << "\n";
        }

        cerr << "\nIterations: "
             << iterations
             << "\n";

        cerr << "Time: "
             << total_time
             << " seconds\n";
    }


    MPI_Finalize();

    return 0;
}