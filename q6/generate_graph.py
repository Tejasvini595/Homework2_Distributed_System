#!/usr/bin/env python3
"""
generate_graph.py - Generate test/benchmark graphs in the assignment's
adjacency-list input format:

    V
    k v1 v2 ... vk      (line for vertex 0)
    ...                 (V lines total)

Edges are written symmetrically (if u appears in v's list, v appears in
u's list), matching the convention the MPI/sequential programs expect.

Usage:
    python3 generate_graph.py --type random   --V 1000 --E 5000  --out g.txt
    python3 generate_graph.py --type chain    --V 1000           --out g.txt
    python3 generate_graph.py --type star     --V 1000           --out g.txt
    python3 generate_graph.py --type clustered --V 1000 --clusters 10 --out g.txt
    python3 generate_graph.py --type disconnected --V 1000 --out g.txt   # E=0
    python3 generate_graph.py --type dupes_selfloops --V 20 --out g.txt  # correctness edge case
"""
import argparse
import random
import sys


def write_graph(V, edges, out_path, seed_extra_selfloops=False, seed_dupes=False):
    adj = [[] for _ in range(V)]
    for u, v in edges:
        adj[u].append(v)
        adj[v].append(u)

    if seed_extra_selfloops:
        for v in range(0, V, max(1, V // 5)):
            adj[v].append(v)

    if seed_dupes:
        for v in range(V):
            if adj[v]:
                adj[v].append(adj[v][0])

    with open(out_path, "w") as f:
        f.write(f"{V}\n")
        for v in range(V):
            nbrs = adj[v]
            f.write(f"{len(nbrs)} " + " ".join(map(str, nbrs)) + "\n" if nbrs else "0\n")


def gen_random(V, E, seed):
    random.seed(seed)
    edges = set()
    max_possible = V * (V - 1) // 2
    E = min(E, max_possible)
    while len(edges) < E:
        u = random.randrange(V)
        v = random.randrange(V)
        if u == v:
            continue
        a, b = min(u, v), max(u, v)
        edges.add((a, b))
    return list(edges)


def gen_chain(V):
    return [(i, i + 1) for i in range(V - 1)]


def gen_star(V):
    return [(0, i) for i in range(1, V)]


def gen_clustered(V, clusters, intra_edge_factor, seed):
    """Several dense-ish clusters, no edges between clusters."""
    random.seed(seed)
    edges = []
    base = V // clusters
    start = 0
    for c in range(clusters):
        size = base + (1 if c < V % clusters else 0)
        verts = list(range(start, start + size))
        # random spanning tree within cluster (guarantees connectivity)
        for i in range(1, len(verts)):
            j = random.randrange(i)
            edges.append((verts[j], verts[i]))
        # extra random intra-cluster edges
        extra = int(len(verts) * intra_edge_factor)
        for _ in range(extra):
            if len(verts) < 2:
                break
            a, b = random.sample(verts, 2)
            edges.append((min(a, b), max(a, b)))
        start += size
    return edges


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--type", required=True,
                     choices=["random", "chain", "star", "clustered",
                              "disconnected", "dupes_selfloops"])
    ap.add_argument("--V", type=int, required=True)
    ap.add_argument("--E", type=int, default=0, help="used by --type random")
    ap.add_argument("--clusters", type=int, default=10)
    ap.add_argument("--intra-edge-factor", type=float, default=1.5)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    if args.type == "random":
        edges = gen_random(args.V, args.E, args.seed)
        write_graph(args.V, edges, args.out)
    elif args.type == "chain":
        write_graph(args.V, gen_chain(args.V), args.out)
    elif args.type == "star":
        write_graph(args.V, gen_star(args.V), args.out)
    elif args.type == "clustered":
        edges = gen_clustered(args.V, args.clusters, args.intra_edge_factor, args.seed)
        write_graph(args.V, edges, args.out)
    elif args.type == "disconnected":
        write_graph(args.V, [], args.out)
    elif args.type == "dupes_selfloops":
        # small connected-ish random graph, then poison it with
        # self-loops and duplicate edges to stress-test robustness
        edges = gen_random(args.V, max(args.V, args.E), args.seed)
        write_graph(args.V, edges, args.out,
                    seed_extra_selfloops=True, seed_dupes=True)

    print(f"Wrote {args.out}: V={args.V}", file=sys.stderr)


if __name__ == "__main__":
    main()
