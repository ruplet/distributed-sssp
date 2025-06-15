import sys
import networkx as nx
from pathlib import Path

# --- NEW: get_owner_process function for block distribution ---
def get_owner_process(vertex: int, num_vertices: int, num_procs: int) -> int:
    """
    Determines the owner process for a given vertex based on block distribution.
    This logic mirrors the C++ split script's ownership assignment.

    Args:
        vertex: The vertex ID.
        num_vertices: The total number of vertices in the graph.
        num_procs: The total number of processes.

    Returns:
        The index of the process that owns the given vertex.
    """
    base = num_vertices // num_procs
    extras = num_vertices % num_procs

    # Vertices [0, extras * (base + 1)) are assigned one extra load
    threshold = extras * (base + 1)
    if vertex < threshold:
        return vertex // (base + 1)
    else:
        return extras + (vertex - threshold) // base

def read_graph_and_ownerships(directory: Path):
    """
    Reads graph data from input files and establishes vertex ownership
    based on a strict block distribution.

    Args:
        directory: The Path object pointing to the directory containing .in files.

    Returns:
        A tuple: (networkx.Graph, ownership_dict, total_vertices, total_processes)
        - G: The constructed graph.
        - ownership_dict: A dictionary mapping vertex ID to its assigned process ID.
        - total_vertices: The total number of vertices in the graph.
        - total_processes: The total number of processes (equal to the number of .in files).

    Raises:
        ValueError: If no .in files are found or if the first header is invalid.
    """
    G = nx.Graph()
    input_files = sorted(directory.glob("*.in"))

    if not input_files:
        raise ValueError(f"No .in files found in directory: {directory}")

    # Determine num_procs from the number of input files found
    num_procs = len(input_files)

    # Read the header from the *first* file to get the consistent total num_vertices.
    # We assume this value is consistent across all files.
    num_vertices = 0
    with open(input_files[0]) as f:
        header = f.readline()
        if header:
            # We only need the first value (num_vertices) from the header for global context
            num_vertices_from_file, _, _ = map(int, header.strip().split())
            num_vertices = num_vertices_from_file

    if num_vertices == 0: # Check if num_vertices was successfully extracted and is non-zero
        raise ValueError(f"Could not read valid total number of vertices from header of {input_files[0]}")

    # --- MODIFIED: Rebuild the ownership map based on strict block distribution ---
    # This loop iterates through all expected vertices and assigns ownership
    # using the 'get_owner_process' function, ensuring perfect block distribution.
    ownership = {v: get_owner_process(v, num_vertices, num_procs) for v in range(num_vertices)}
    # --- END MODIFIED ---

    # Second pass: Now read the actual graph edges from all files
    for i, path in enumerate(input_files):
        with open(path) as f:
            f.readline() # Skip the header line as we've already processed its information
            for line in f:
                u, v, w = map(int, line.strip().split())
                if u == v:
                    continue  # Ignore self-loops (consistent with original script)
                
                # Add edge to the graph. networkx handles duplicate edges by keeping the first
                # or lowest weight if added multiple times, but your original implicitly handled it.
                # If a more explicit 'seen_edges' logic is needed, it would go here.
                G.add_edge(u, v, weight=w)

    return G, ownership, num_vertices, num_procs

def compute_and_write_distances(G: nx.Graph, ownership: dict, out_dir: Path, num_procs: int):
    """
    Computes single-source shortest path distances using Dijkstra's algorithm
    and writes results to output files based on vertex ownership.

    Args:
        G: The NetworkX graph.
        ownership: A dictionary mapping vertex ID to its process owner.
        out_dir: The output directory where .out files will be written.
        num_procs: The total number of processes.
    """
    print(f"Computing SSSP distances from source 0 for {G.number_of_nodes()} vertices...")
    distances = nx.single_source_dijkstra_path_length(G, source=0, weight='weight')
    print("Distance computation complete.")

    # Invert ownership: Group vertices by their owning process
    proc_vertices = {i: [] for i in range(num_procs)}
    for v, proc in ownership.items():
        if proc not in proc_vertices: # Ensure the process key exists
            proc_vertices[proc] = []
        proc_vertices[proc].append(v)

    # Write distances to process-specific output files
    print(f"Writing distances to {num_procs} output files...")
    for proc_id in sorted(proc_vertices.keys()): # Iterate by sorted process ID
        vertices_for_proc = proc_vertices[proc_id]
        out_path = out_dir / f"{proc_id}.out"
        with open(out_path, "w") as f:
            for v in sorted(vertices_for_proc): # Sort vertices for consistent output
                dist = distances.get(v, -1) # -1 if vertex not reachable
                f.write(f"{dist}\n")
    print("Distance writing complete.")

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <directory_with_proc_input_files>", file=sys.stderr)
        sys.exit(1)

    directory = Path(sys.argv[1])
    try:
        G, ownership, num_vertices, num_procs = read_graph_and_ownerships(directory)
    except ValueError as e:
        print(f"Error reading input files: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Graph loaded with {G.number_of_nodes()} vertices and {G.number_of_edges()} edges.")
    print(f"Calculated total vertices for distribution: {num_vertices}")
    print(f"Number of processes (based on input files): {num_procs}")
    
    # Example: Verify a few vertices' ownership
    # print("\nVerifying block distribution for a few vertices:")
    # for v_check in range(min(num_vertices, 20)): # Check first 20 or fewer if graph is small
    #     expected_owner = get_owner_process(v_check, num_vertices, num_procs)
    #     actual_owner = ownership[v_check]
    #     print(f"  Vertex {v_check}: Calculated owner {expected_owner}, Actual assigned {actual_owner}")
    # print("-" * 30)

    compute_and_write_distances(G, ownership, directory, num_procs)
    print("Script finished successfully.")

if __name__ == "__main__":
    main()