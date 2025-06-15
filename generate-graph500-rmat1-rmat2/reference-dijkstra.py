import sys
import networkx as nx
from pathlib import Path

def read_graph_and_ownerships(directory: Path):
    G = nx.Graph()
    ownership = {}  # Maps vertex -> process
    # seen_edges = {}

    # First pass: determine ownership and edges
    for i, path in enumerate(sorted(directory.glob("*.in"))):
        with open(path) as f:
            header = f.readline()
            if not header:
                continue
            num_vertices, first, last = map(int, header.strip().split())
            for v in range(first, last + 1):
                ownership[v] = i  # This process owns this vertex

            for line in f:
                u, v, w = map(int, line.strip().split())
                if u == v:
                    continue  # Ignore self-loops

                # edge = tuple(sorted((u, v)))
                # if edge not in seen_edges or w < seen_edges[edge]:
                #     seen_edges[edge] = w
                G.add_edge(u, v, weight=w)

    return G, ownership

def compute_and_write_distances(G, ownership, out_dir: Path, num_procs: int):
    distances = nx.single_source_dijkstra_path_length(G, source=0, weight='weight')

    # Invert ownership: process_id -> list of vertices
    proc_vertices = {i: [] for i in range(num_procs)}
    for v, proc in ownership.items():
        proc_vertices[proc].append(v)

    for proc, vertices in proc_vertices.items():
        out_path = out_dir / f"{proc}.out"
        with open(out_path, "w") as f:
            for v in sorted(vertices):
                dist = distances.get(v, -1)
                f.write(f"{dist}\n")

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <directory_with_proc_input_files>")
        sys.exit(1)

    directory = Path(sys.argv[1])
    G, ownership = read_graph_and_ownerships(directory)
    num_procs = len(set(ownership.values()))
    compute_and_write_distances(G, ownership, directory, num_procs)

if __name__ == "__main__":
    main()
