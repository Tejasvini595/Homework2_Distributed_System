// q6_sequential.cpp
// Sequential reference solution: Union-Find (disjoint set union).
// Used to verify correctness of the MPI implementation.
//
// Output format matches q6_mpi.cpp exactly: V lines of
// "vertex_id component_id", sorted by vertex id ascending,
// component id = minimum vertex id in that component.

#include <climits>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

using namespace std;

struct DSU
{
    vector<int> parent, rank_;

    DSU(int n) : parent(n), rank_(n, 0)
    {
        iota(parent.begin(), parent.end(), 0);
    }

    int find(int x)
    {
        while (parent[x] != x)
        {
            parent[x] = parent[parent[x]]; // path halving
            x = parent[x];
        }
        return x;
    }

    void unite(int a, int b)
    {
        a = find(a);
        b = find(b);
        if (a == b) return;
        if (rank_[a] < rank_[b]) swap(a, b);
        parent[b] = a;
        if (rank_[a] == rank_[b]) rank_[a]++;
    }
};

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        cerr << "Usage: ./q6_sequential input.txt\n";
        return 1;
    }

    ifstream fin(argv[1]);
    if (!fin)
    {
        cerr << "Cannot open input file\n";
        return 1;
    }

    int V;
    fin >> V;

    DSU dsu(V);

    for (int i = 0; i < V; i++)
    {
        int k;
        fin >> k;
        for (int j = 0; j < k; j++)
        {
            int u;
            fin >> u;
            dsu.unite(i, u);
        }
    }
    fin.close();

    // Component id = minimum vertex id in that component.
    vector<int> comp_min(V, INT_MAX);
    for (int v = 0; v < V; v++)
    {
        int r = dsu.find(v);
        comp_min[r] = min(comp_min[r], v);
    }

    ios_base::sync_with_stdio(false);
    ostringstream oss;
    for (int v = 0; v < V; v++)
        oss << v << ' ' << comp_min[dsu.find(v)] << '\n';
    cout << oss.str();

    return 0;
}
