// main.cpp
#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>
#include <mpi.h>
#include <cmath>   // For floor
#include <map>     // Potentially for sparse buckets
#include <random>  // For std::mt19937, std::uniform_real_distribution (though RMAT generator removed)
#include <fstream> // For file output
#include <sstream> // For parsing lines

// --- Configuration Constants (Tunable / Flags) ---
const bool ENABLE_DEBUG_PRINT = false;    // For verbose output from rank 0
const bool USE_MAP_FOR_BUCKETS = false;  // Alternative bucket implementation
const bool SKIP_COMPUTATIONS_FOR_NOW = true; // <<< NEW FLAG TO SKIP DELTA_STEPPING

// --- MPI Information ---
int world_rank, world_size;

// --- Graph Data (Local to each process) ---
long long N_global_total_vertices; // Total number of vertices in the graph (from input)
int N_local_responsible_count; // Number of vertices this process is responsible for (calculated from first_responsible_idx, last_responsible_idx)
int first_responsible_idx; // Global ID of the first vertex this process is responsible for (from input)
int last_responsible_idx;  // Global ID of the last vertex this process is responsible for (inclusive, from input)

// Adjacency list: adj[local_u_idx] -> {global_v_idx, weight}
// The local_u_idx will range from 0 to (N_local_responsible_count - 1).
// It maps to global_u_idx = first_responsible_idx + local_u_idx.
std::vector<std::vector<std::pair<int, int>>> adj_local_responsible;
std::vector<long long> dist_local_responsible; // Distances to locally responsible vertices

const long long INF = std::numeric_limits<long long>::max();
const int INF_BUCKET_INDEX = std::numeric_limits<int>::max();


// --- Helper Functions (Ownership might change based on new input) ---
// The concept of "owning" a vertex for computation in Delta-Stepping might be different
// from "being responsible for" a vertex for input/output.
// For Delta-Stepping, we need a consistent global ownership model.
// The problem description implies block distribution is GIVEN.
// "The vertices will be equally distributed among the processes (with up to 1 vertex difference),
// and the vertex indexes will be increasing with respect to the rank number."
// This implies that the first_responsible_idx and last_responsible_idx define the
// vertices *owned* by this process for the algorithm as well.

int get_owner_of_global_vertex(int global_v_idx) {
    if (N_global_total_vertices == 0) return 0;
    // Based on the problem's distribution guarantee:
    // We need to find which rank's [first_responsible_idx, last_responsible_idx] range contains global_v_idx.
    // This requires knowing all other processes' ranges, or re-calculating them.
    // Let's assume we can recalculate block distribution for any vertex.
    long long vertices_per_process_ideal = (N_global_total_vertices + world_size - 1) / world_size;
    int owner = global_v_idx / vertices_per_process_ideal;
    // Clamp to ensure it's a valid rank, especially if N_global_total_vertices is small
    return std::min(owner, world_size - 1);
}

int global_to_local_idx(int global_v_idx) {
    // This maps a global vertex ID to its local index IF this process is responsible for it.
    if (global_v_idx >= first_responsible_idx && global_v_idx <= last_responsible_idx) {
        return global_v_idx - first_responsible_idx;
    }
    return -1; // Indicates not responsible for this vertex / not a direct local index
}

bool is_responsible_for_global_vertex(int global_v_idx) {
    return global_v_idx >= first_responsible_idx && global_v_idx <= last_responsible_idx;
}


// Bucket management wrappers (largely unchanged, but operate on dist_local_responsible indices)
std::vector<std::vector<int>> local_buckets_vec;
std::map<int, std::vector<int>> local_buckets_map;

void add_to_bucket(int local_v_idx, int bucket_idx) { // local_v_idx is for dist_local_responsible
    if (bucket_idx == INF_BUCKET_INDEX) return;
    if (USE_MAP_FOR_BUCKETS) {
        local_buckets_map[bucket_idx].push_back(local_v_idx);
    } else {
        if (bucket_idx >= local_buckets_vec.size()) {
            local_buckets_vec.resize(bucket_idx + 1);
        }
        local_buckets_vec[bucket_idx].push_back(local_v_idx);
    }
}

void remove_from_bucket(int local_v_idx, int bucket_idx) { // local_v_idx is for dist_local_responsible
    if (bucket_idx == INF_BUCKET_INDEX) return;
    std::vector<int>* bucket_ptr;
    if (USE_MAP_FOR_BUCKETS) {
        auto it = local_buckets_map.find(bucket_idx);
        if (it == local_buckets_map.end()) return;
        bucket_ptr = &(it->second);
    } else {
        if (bucket_idx >= local_buckets_vec.size()) return;
        bucket_ptr = &local_buckets_vec[bucket_idx];
    }

    auto& bucket = *bucket_ptr;
    auto item_it = std::find(bucket.begin(), bucket.end(), local_v_idx);
    if (item_it != bucket.end()) {
        *item_it = bucket.back();
        bucket.pop_back();
    }
    if (USE_MAP_FOR_BUCKETS && bucket.empty()) {
         local_buckets_map.erase(bucket_idx);
    }
}

int get_bucket_index(long long current_dist, long long delta_val) {
    if (current_dist == INF) return INF_BUCKET_INDEX;
    return static_cast<int>(current_dist / delta_val);
}

std::vector<int>& get_bucket_list_ref(int bucket_idx) {
    if (USE_MAP_FOR_BUCKETS) {
        return local_buckets_map[bucket_idx];
    } else {
        if (bucket_idx >= local_buckets_vec.size()) {
            local_buckets_vec.resize(bucket_idx + 1);
        }
        return local_buckets_vec[bucket_idx];
    }
}

bool is_bucket_empty_locally(int bucket_idx) {
    if (USE_MAP_FOR_BUCKETS) {
        auto it = local_buckets_map.find(bucket_idx);
        return (it == local_buckets_map.end() || it->second.empty());
    } else {
        return (bucket_idx >= local_buckets_vec.size() || local_buckets_vec[bucket_idx].empty());
    }
}

struct RelaxMessage {
    int v_global;
    long long new_dist;
};


// --- Delta-Stepping Core Logic (to be skipped for now but kept) ---
void delta_stepping_algorithm(int source_global_idx, long long delta_val) {
    if (SKIP_COMPUTATIONS_FOR_NOW) {
        if (world_rank == 0 && ENABLE_DEBUG_PRINT) {
            std::cout << "DEBUG: Skipping Delta-Stepping computations." << std::endl;
        }
        // Initialize dist_local_responsible with dummy values if needed for output structure
        dist_local_responsible.assign(N_local_responsible_count, 0); // DUMMY OUTPUT
        if (is_responsible_for_global_vertex(source_global_idx)) {
            dist_local_responsible[global_to_local_idx(source_global_idx)] = 0;
        } else {
            // If source 0 is not owned, its dist won't be 0 here by default,
            // but for the dummy output this might be okay as we only output responsible vertices.
        }
        return;
    }

    // Actual Initialization for Delta-Stepping
    dist_local_responsible.assign(N_local_responsible_count, INF);
    if (USE_MAP_FOR_BUCKETS) {
        local_buckets_map.clear();
    } else {
        for(auto& bucket : local_buckets_vec) bucket.clear();
        if(!USE_MAP_FOR_BUCKETS && local_buckets_vec.empty()) local_buckets_vec.resize(1024);
    }

    MPI_Datatype relax_message_type;
    int lengths[2] = {1, 1};
    MPI_Aint displacements[2];
    RelaxMessage dummy_msg_mpi_type;
    MPI_Get_address(&dummy_msg_mpi_type.v_global, &displacements[0]);
    MPI_Get_address(&dummy_msg_mpi_type.new_dist, &displacements[1]);
    MPI_Aint base_address;
    MPI_Get_address(&dummy_msg_mpi_type, &base_address);
    displacements[0] -= base_address;
    displacements[1] -= base_address;
    MPI_Datatype types[2] = {MPI_INT, MPI_LONG_LONG};
    MPI_Type_create_struct(2, lengths, displacements, types, &relax_message_type);
    MPI_Type_commit(&relax_message_type);

    if (is_responsible_for_global_vertex(source_global_idx)) {
        int source_local_idx = global_to_local_idx(source_global_idx);
        dist_local_responsible[source_local_idx] = 0;
        add_to_bucket(source_local_idx, get_bucket_index(0, delta_val));
    }

    int k = 0; 

    std::vector<RelaxMessage> outgoing_relax_requests_flat;
    std::vector<int> send_counts(world_size);
    std::vector<int> sdispls(world_size);
    std::vector<RelaxMessage> incoming_relax_requests_flat;
    std::vector<int> recv_counts(world_size);
    std::vector<int> rdispls(world_size);
    std::vector<std::vector<RelaxMessage>> outgoing_per_proc_buffer(world_size);

    while (true) { 
        int local_min_k_overall = INF_BUCKET_INDEX;
        if (USE_MAP_FOR_BUCKETS) {
            for (const auto& pair : local_buckets_map) {
                if (pair.first >= k && !pair.second.empty()) {
                    local_min_k_overall = std::min(local_min_k_overall, pair.first);
                }
            }
        } else {
            for (int b_idx = k; b_idx < local_buckets_vec.size(); ++b_idx) {
                if (!local_buckets_vec[b_idx].empty()) {
                    local_min_k_overall = b_idx;
                    break;
                }
            }
        }
        
        int global_min_k_overall;
        MPI_Allreduce(&local_min_k_overall, &global_min_k_overall, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
        k = global_min_k_overall;

        if (k == INF_BUCKET_INDEX) break; 

        if (ENABLE_DEBUG_PRINT && world_rank == 0) {
            std::cout << "Epoch for k = " << k << std::endl;
        }
        
        std::vector<int> S_k_active_in_bucket_k; 
        if (!is_bucket_empty_locally(k)) {
             S_k_active_in_bucket_k = get_bucket_list_ref(k); 
        }

        while (true) { 
            long long S_k_local_size = S_k_active_in_bucket_k.size();
            long long S_k_global_size;
            MPI_Allreduce(&S_k_local_size, &S_k_global_size, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);

            if (S_k_global_size == 0) break; 
            
            if (ENABLE_DEBUG_PRINT && world_rank == 0) {
                 std::cout << "  Phase for k = " << k << " |S_k| global = " << S_k_global_size << std::endl;
            }

            for (int i = 0; i < world_size; ++i) outgoing_per_proc_buffer[i].clear();
            outgoing_relax_requests_flat.clear(); 

            for (int u_responsible_local_idx : S_k_active_in_bucket_k) {
                long long dist_u = dist_local_responsible[u_responsible_local_idx];
                if (dist_u == INF) continue; 

                // int u_global = first_responsible_idx + u_responsible_local_idx; // This u_global is not sent.
                for (const auto& edge : adj_local_responsible[u_responsible_local_idx]) {
                    int v_global = edge.first;
                    int weight = edge.second;
                    long long potential_new_dist_v = dist_u + weight;
                    
                    int v_owner = get_owner_of_global_vertex(v_global);
                    outgoing_per_proc_buffer[v_owner].push_back({v_global, potential_new_dist_v});
                }
            }
            S_k_active_in_bucket_k.clear(); 

            int current_sdispl = 0;
            for(int p = 0; p < world_size; ++p) {
                send_counts[p] = outgoing_per_proc_buffer[p].size();
                sdispls[p] = current_sdispl;
                for(const auto& msg : outgoing_per_proc_buffer[p]) {
                    outgoing_relax_requests_flat.push_back(msg);
                }
                current_sdispl += send_counts[p];
            }

            MPI_Alltoall(send_counts.data(), 1, MPI_INT, recv_counts.data(), 1, MPI_INT, MPI_COMM_WORLD);
            
            int total_incoming_messages = 0;
            int current_rdispl = 0;
            for(int p=0; p<world_size; ++p) {
                rdispls[p] = current_rdispl;
                total_incoming_messages += recv_counts[p];
                current_rdispl += recv_counts[p];
            }
            incoming_relax_requests_flat.resize(total_incoming_messages);

            MPI_Alltoallv(outgoing_relax_requests_flat.data(), send_counts.data(), sdispls.data(), relax_message_type,
                          incoming_relax_requests_flat.data(), recv_counts.data(), rdispls.data(), relax_message_type,
                          MPI_COMM_WORLD);

            std::vector<int> changed_vertices_local_indices; 
            for (const auto& msg : incoming_relax_requests_flat) {
                int v_global = msg.v_global;
                long long new_dist_v = msg.new_dist;

                if (is_responsible_for_global_vertex(v_global)) {
                    int v_responsible_local_idx = global_to_local_idx(v_global);
                    long long old_dist_v = dist_local_responsible[v_responsible_local_idx];

                    if (new_dist_v < old_dist_v) {
                        int old_bucket_idx = get_bucket_index(old_dist_v, delta_val);
                        
                        if (old_dist_v != INF) { 
                           remove_from_bucket(v_responsible_local_idx, old_bucket_idx);
                        }
                        
                        dist_local_responsible[v_responsible_local_idx] = new_dist_v;
                        int new_bucket_idx = get_bucket_index(new_dist_v, delta_val);
                        add_to_bucket(v_responsible_local_idx, new_bucket_idx);
                        
                        changed_vertices_local_indices.push_back(v_responsible_local_idx);
                    }
                }
            }
            
            for (int v_responsible_local_idx : changed_vertices_local_indices) {
                 if (get_bucket_index(dist_local_responsible[v_responsible_local_idx], delta_val) == k) {
                    S_k_active_in_bucket_k.push_back(v_responsible_local_idx);
                 }
            }
            std::sort(S_k_active_in_bucket_k.begin(), S_k_active_in_bucket_k.end());
            S_k_active_in_bucket_k.erase(std::unique(S_k_active_in_bucket_k.begin(), S_k_active_in_bucket_k.end()), S_k_active_in_bucket_k.end());

        } 
        k = 0; // Reset search for next k from beginning. (Paper says k_next = min{i > k}, let's stick to that logic implemented via the allreduce and loop)
               // The `k = global_min_k_overall` at the start of the outer loop correctly finds the smallest active k.
    } 
    
    MPI_Type_free(&relax_message_type);
}


// --- Graph Loading from stdin ---
void load_graph_from_stdin() {
    std::string line;

    // Read the first line: N_total, first_idx, last_idx
    if (!std::getline(std::cin, line)) {
        std::cerr << "Rank " << world_rank << ": Error reading first line from stdin." << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    std::istringstream iss_first_line(line);
    if (!(iss_first_line >> N_global_total_vertices >> first_responsible_idx >> last_responsible_idx)) {
        std::cerr << "Rank " << world_rank << ": Error parsing first line: " << line << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (first_responsible_idx < 0 || last_responsible_idx < first_responsible_idx || last_responsible_idx >= N_global_total_vertices) {
         std::cerr << "Rank " << world_rank << ": Invalid responsible indices: "
                   << first_responsible_idx << " to " << last_responsible_idx
                   << " for N_global=" << N_global_total_vertices << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    N_local_responsible_count = (last_responsible_idx - first_responsible_idx + 1);
    if (N_local_responsible_count < 0) N_local_responsible_count = 0; // Should not happen with checks above

    adj_local_responsible.assign(N_local_responsible_count, std::vector<std::pair<int, int>>());
    dist_local_responsible.resize(N_local_responsible_count); // Will be filled by algorithm or dummy

    // Read edges
    int u, v, w;
    while (std::getline(std::cin, line)) {
        std::istringstream iss_edge(line);
        if (!(iss_edge >> u >> v >> w)) {
            std::cerr << "Rank " << world_rank << ": Error parsing edge line: " << line << std::endl;
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        // The input edges are global. Each process gets ALL edges.
        // It should only store edges where 'u' is one of its responsible vertices.
        // The problem says "The remaining lines contain the descriptions of the edges" implying ALL edges are given to each process.
        // However, the SSSP algorithms are usually more efficient if each process only stores outgoing edges for vertices it *owns*.
        // Let's assume the input format means: "these are edges involving MY responsible vertices".
        // "The input file consists of ... first and last vertex THIS PROCESS IS RESPONSIBLE FOR."
        // "The remaining lines contain ... edges" - this strongly suggests these edges are relevant to this process.
        // If an edge (u,v,w) is listed, and THIS process is responsible for 'u', add it.
        // What if 'v' is responsible by this process? And 'u' by another? Then this process doesn't list it.
        // The problem statement needs clarification on whether edges are globally listed or only for one endpoint.
        // Given typical distributed graph formats, let's assume an edge (u,v,w) means that if THIS process is responsible for 'u',
        // it should add an outgoing edge u->v.
        // And if it's responsible for 'v', it should add an outgoing edge v->u (because graph is undirected).

        // If this process is responsible for u, add edge (u,v,w)
        if (is_responsible_for_global_vertex(u)) {
            adj_local_responsible[global_to_local_idx(u)].push_back({v, w});
        }
        // If graph is undirected and this process is responsible for v, add edge (v,u,w)
        // This handles the case where u is owned by another process.
        if (is_responsible_for_global_vertex(v)) {
             adj_local_responsible[global_to_local_idx(v)].push_back({u, w});
        }
    }
    // Post-processing: remove duplicate edges if the input could cause them due to the symmetric addition.
    // (e.g., if an edge (A,B,W) is in the input, and A and B are both owned by this process, it gets added twice)
    // Or, a convention could be: input contains each undirected edge only ONCE, process adds it if it owns either endpoint.
    // Sticking to "if I own u, add (u,v). If I own v, add (v,u)" could create duplicates IF an edge (u,v) has both u and v owned by me.
    // Let's refine edge adding:
    // Each edge (u, v, w) from input file.
    // Add u -> v with weight w IF current process is responsible for u.
    // Add v -> u with weight w IF current process is responsible for v.
    // This still can lead to u -> v and v -> u being in adj_local_responsible[u_local] if v is also local.
    // This is okay for SSSP, as Relax(u,v) happens from u.
    // Let's re-read the input description logic carefully. "The remaining lines contain the descriptions of the edges"
    // This implies this process receives a *list of edges*. It doesn't say these edges *originate* from its responsible vertices.
    // It could be that each process receives the *entire global edgelist*.
    // "The input PART will fit into a process memory" suggests it's not the *entire* graph necessarily.
    // "The vertices will be equally distributed...indexes increasing with rank number" this defines partitions.
    // "The first line ... first and last vertex THIS PROCESS IS RESPONSIBLE FOR"
    // "The remaining lines ... DESCRIPTIONS OF THE EDGES" - whose edges?
    // Assumption: Each process's stdin receives:
    // 1. N_global, my_first_resp_idx, my_last_resp_idx
    // 2. A list of edges (u,v,w) that are RELEVANT to THIS process, typically meaning u is one of my_first_resp_idx to my_last_resp_idx.
    // Let's assume input edges (u,v,w) are such that u is one of the process's responsible vertices.

    // Re-doing edge reading with clearer assumption: Each line (u,v,w) in *my* stdin
    // means an edge from a 'u' that I am responsible for, to some 'v'.
    // Clear previous adj_local_responsible.
    adj_local_responsible.assign(N_local_responsible_count, std::vector<std::pair<int, int>>());
    
    // Re-open stdin or reset stream (not standard for stdin, better to read once)
    // For file input, we would rewind. For stdin, we assume all lines are now processed.
    // The initial read loop was okay. The interpretation of which edges to add is key.

    // If the input for each process is indeed:
    // N_global my_first_idx my_last_idx
    // u1 v1 w1  (where u1 is within my_first_idx..my_last_idx)
    // u2 v2 w2  (where u2 is within my_first_idx..my_last_idx)
    // ...
    // Then the first loading attempt was closer:
    // (Need to reset std::cin or read into a buffer first)

    // Let's read all edge lines into a temporary buffer first, then process.
    std::vector<std::tuple<int, int, int>> edge_buffer;
    // The loop structure with std::getline(std::cin, line) and then iss_edge(line) processes stdin line by line.
    // That was correct. The logic *inside* the loop for adj_local_responsible.push_back needs to be certain.

    // Final refined logic for load_graph_from_stdin:
    // Assume stdin for rank P is:
    //   N_global P_first_idx P_last_idx
    //   u1 v1 w1  (where P is responsible for u1. Edge u1 -> v1)
    //   u2 v2 w2  (where P is responsible for u2. Edge u2 -> v2)
    //   ...

    // This means the first edge parsing loop structure was essentially correct, but we need to be sure about u,v,w values.
    // `adj_local_responsible.assign(N_local_responsible_count, ...)` should be done AFTER reading N_local_responsible_count.

    // Corrected load_graph_from_stdin again, this time assuming input edges are ONLY outgoing from my responsible vertices.
    // (The previous version reading all lines then re-processing `adj` was messy. Direct is better)

    // The first approach for load_graph_from_stdin:
    // (Resetting to that and simplifying)
    std::cin.clear(); // Clear EOF flags if any were set by mistake before.
    std::cin.seekg(0); // This does NOT work for stdin usually. Input data must be re-provided or piped correctly.
                      // The program is run once per process by mpirun, each gets its own stdin stream if redirected.
                      // E.g. mpirun -np 2 ./exec < process0.txt : process1.txt (this syntax is MPI specific, often via hostfile)
                      // Or more commonly: `mpirun ... ./exec` and each process reads from its standard input,
                      // which is often a copy of the terminal input unless redirected for each process by a script.
                      // For your setup, each process will likely be fed its specific input file via redirection in a job script.
                      // So, reading from std::cin directly by each process should be fine.
}

void process_input_and_load_graph() {
    std::string line;

    // Read the first line: N_total, first_idx, last_idx
    if (!std::getline(std::cin, line)) {
        std::cerr << "Rank " << world_rank << ": Error reading first line from stdin." << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return; // Should not reach here after Abort
    }
    std::istringstream iss_first_line(line);
    if (!(iss_first_line >> N_global_total_vertices >> first_responsible_idx >> last_responsible_idx)) {
        std::cerr << "Rank " << world_rank << ": Error parsing first line: " << line << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return;
    }

    if (first_responsible_idx < 0 || last_responsible_idx < first_responsible_idx || 
        (N_global_total_vertices > 0 && last_responsible_idx >= N_global_total_vertices) ||
        (N_global_total_vertices == 0 && (first_responsible_idx != 0 || last_responsible_idx != -1))) { // N=0 case for empty graph
         std::cerr << "Rank " << world_rank << ": Invalid responsible indices: "
                   << first_responsible_idx << " to " << last_responsible_idx
                   << " for N_global=" << N_global_total_vertices << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return;
    }
    
    if (N_global_total_vertices == 0) {
        N_local_responsible_count = 0;
        first_responsible_idx = 0; // canonical empty range
        last_responsible_idx = -1;
    } else {
        N_local_responsible_count = (last_responsible_idx - first_responsible_idx + 1);
    }
    
    adj_local_responsible.assign(N_local_responsible_count, std::vector<std::pair<int, int>>());
    // dist_local_responsible will be resized/assigned in delta_stepping or main

    // Read edges. Assume each (u,v,w) line means u is one of *my* responsible vertices.
    int u_edge_src, v_edge_dest, w_edge_weight;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue; // Skip empty lines if any
        std::istringstream iss_edge(line);
        if (!(iss_edge >> u_edge_src >> v_edge_dest >> w_edge_weight)) {
            std::cerr << "Rank " << world_rank << ": Error parsing edge line: '" << line << "'" << std::endl;
            MPI_Abort(MPI_COMM_WORLD, 1);
            return;
        }

        if (!is_responsible_for_global_vertex(u_edge_src)) {
            std::cerr << "Rank " << world_rank << ": Edge source " << u_edge_src 
                      << " is not in responsible range [" << first_responsible_idx << "," << last_responsible_idx << "]. Edge: " << line << std::endl;
            // According to problem: "The remaining lines contain the descriptions of the edges" - this implies edges *for this process's part*.
            // It's common in some formats that the edgelist provided to a process P contains *only* edges (u,v,w) where u is owned by P.
            // If this is not the case, and each process gets a global edgelist, the filtering would be:
            // if (is_responsible_for_global_vertex(u_edge_src)) { adj_local_responsible[global_to_local_idx(u_edge_src)].push_back({v_edge_dest, w_edge_weight}); }
            // if (is_responsible_for_global_vertex(v_edge_dest)) { adj_local_responsible[global_to_local_idx(v_edge_dest)].push_back({u_edge_src, w_edge_weight}); } // For undirected
            // But the problem statement's flow seems to imply the input edges are already filtered by source for this process.
            // If this check fires, it means my assumption about input structure is wrong OR input data is malformed for this rank.
            // For now, let's be strict based on the "this process is responsible for" part.
             MPI_Abort(MPI_COMM_WORLD, 1); // Strict check
             return;
        }
        if (u_edge_src < 0 || u_edge_src >= N_global_total_vertices || v_edge_dest < 0 || v_edge_dest >= N_global_total_vertices || w_edge_weight < 0) {
             std::cerr << "Rank " << world_rank << ": Invalid edge data: " << u_edge_src << " " << v_edge_dest << " " << w_edge_weight << std::endl;
             MPI_Abort(MPI_COMM_WORLD, 1);
             return;
        }


        adj_local_responsible[global_to_local_idx(u_edge_src)].push_back({v_edge_dest, w_edge_weight});
    }
}

// Change the signature and use the stream parameter
// void process_input_and_load_graph() becomes:
void process_input_and_load_graph_from_stream(std::istream& instream) {
    std::string line;

    // Read the first line: N_total, first_idx, last_idx
    if (!std::getline(instream, line)) {
        std::cerr << "Rank " << world_rank << ": Error reading first line from input stream." << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return; 
    }
    std::istringstream iss_first_line(line);
    if (!(iss_first_line >> N_global_total_vertices >> first_responsible_idx >> last_responsible_idx)) {
        std::cerr << "Rank " << world_rank << ": Error parsing first line: " << line << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return;
    }

    // (Validation of N_global_total_vertices, first_responsible_idx, last_responsible_idx as before)
    if (first_responsible_idx < 0 || 
        (N_global_total_vertices > 0 && (last_responsible_idx < first_responsible_idx || last_responsible_idx >= N_global_total_vertices)) ||
        (N_global_total_vertices == 0) ) {
         std::cerr << "Rank " << world_rank << ": Invalid responsible indices: "
                   << first_responsible_idx << " to " << last_responsible_idx
                   << " for N_global=" << N_global_total_vertices << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return;
    }
    
    N_local_responsible_count = (last_responsible_idx - first_responsible_idx + 1);
    
    adj_local_responsible.assign(N_local_responsible_count, std::vector<std::pair<int, int>>());

    int u_edge, v_edge, w_edge;
    while (std::getline(instream, line)) {
        if (line.empty()) continue; // Skip empty lines
        std::istringstream iss_edge(line);
        if (!(iss_edge >> u_edge >> v_edge >> w_edge)) {
            std::cerr << "Rank " << world_rank << ": Error parsing edge line: '" << line << "'" << std::endl;
            MPI_Abort(MPI_COMM_WORLD, 1);
            return;
        }

        if (u_edge < 0 || u_edge >= N_global_total_vertices || v_edge < 0 || v_edge >= N_global_total_vertices || w_edge < 0) {
             std::cerr << "Rank " << world_rank << ": Invalid edge data from input: " 
                       << u_edge << " " << v_edge << " " << w_edge 
                       << " (N_global=" << N_global_total_vertices << ")" << std::endl;
             MPI_Abort(MPI_COMM_WORLD, 1);
             return;
        }
        // Graph is undirected. Each edge {u,v} with weight w means (u,v,w) and (v,u,w) exist.
        // Add (u,v,w) if this process is responsible for u.
        if (is_responsible_for_global_vertex(u_edge)) {
            adj_local_responsible[global_to_local_idx(u_edge)].push_back({v_edge, w_edge});
        }
        // Add (v,u,w) if this process is responsible for v.
        // This ensures that if an edge {u,v} is listed, and this process owns v but not u, it still gets v->u.
        if (is_responsible_for_global_vertex(v_edge)) {
            adj_local_responsible[global_to_local_idx(v_edge)].push_back({u_edge, w_edge});
        }
    }
    // The problem says "no repeated edges" in the input. This refers to the {u,v} pair being unique.
    // My logic above correctly adds the directed components. If an edge (A,B) is in the input
    // and process P owns both A and B, then:
    // - adj_local_responsible[local_A] will get (B, weight)
    // - adj_local_responsible[local_B] will get (A, weight)
    // This is the correct representation for SSSP on an undirected graph.
}

// --- Main ---
int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (argc < 3) { // Now expecting executable, input_file, output_file
        if (world_rank == 0) { 
            std::cerr << "Usage: " << argv[0] << " <input_file> <output_file> [delta_param_optional]" << std::endl;
            std::cerr << "Each process reads its specific input_file." << std::endl;
            std::cerr << "Each process writes to its specific output_file." << std::endl;
        }
        MPI_Finalize();
        return 1;
    }

    std::string input_filename = argv[1];
    std::string output_filename = argv[2];
    // Delta: Default to 10. If N_global_total_vertices becomes available early, a heuristic could be used.
    // For now, a fixed default or command-line arg.
    long long delta_param = (argc > 3) ? std::stoll(argv[3]) : 10; 
    if (delta_param <= 0) delta_param = 1;

    if (world_rank == 0 && ENABLE_DEBUG_PRINT) {
        std::cout << "DEBUG: MPI Initialized. World size: " << world_size << std::endl;
        // Note: Input/Output filenames are per-process. Rank 0 printing argv[1] and argv[2]
        // will show what Rank 0 received. Each rank gets its own argv from mpirun.
        std::cout << "DEBUG: Rank 0 using input file: " << input_filename << std::endl;
        std::cout << "DEBUG: Rank 0 using output file: " << output_filename << std::endl;
        std::cout << "DEBUG: Delta parameter: " << delta_param << std::endl;
    }
    
    // --- Graph Loading from specified input_filename ---
    std::ifstream infile(input_filename);
    if (!infile.is_open()) {
        std::cerr << "Rank " << world_rank << ": Error opening input file " << input_filename << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Pass the ifstream to the loading function
    process_input_and_load_graph_from_stream(infile);
    infile.close(); // Close the file after reading

    if (world_rank == 0 && ENABLE_DEBUG_PRINT) {
        std::cout << "DEBUG: Graph loading complete for rank 0." << std::endl;
        std::cout << "DEBUG: N_global_total_vertices=" << N_global_total_vertices 
                  << ", Rank 0 responsible for " << first_responsible_idx << "-" << last_responsible_idx
                  << " (count " << N_local_responsible_count << ")" << std::endl;
    }
    
    MPI_Barrier(MPI_COMM_WORLD); 
    double start_time = MPI_Wtime();

    const int SOURCE_VERTEX_GLOBAL_ID = 0;
    delta_stepping_algorithm(SOURCE_VERTEX_GLOBAL_ID, delta_param);

    MPI_Barrier(MPI_COMM_WORLD);
    double end_time = MPI_Wtime();

    if (world_rank == 0 && !SKIP_COMPUTATIONS_FOR_NOW) {
        std::cout << "Delta-stepping finished (actual computation)." << std::endl;
        std::cout << "Time taken: " << (end_time - start_time) << " seconds." << std::endl;
    } else if (world_rank == 0 && SKIP_COMPUTATIONS_FOR_NOW) {
        std::cout << "Delta-stepping computations SKIPPED. Outputting dummy distances." << std::endl;
        std::cout << "Skipped computation time: " << (end_time - start_time) << " seconds (mostly barrier sync)." << std::endl;
    }

    std::ofstream outfile_stream(output_filename);
    if (!outfile_stream.is_open()) {
        std::cerr << "Rank " << world_rank << ": Error opening output file " << output_filename << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Ensure dist_local_responsible is correctly sized, especially for dummy output
    if (SKIP_COMPUTATIONS_FOR_NOW || dist_local_responsible.size() != N_local_responsible_count) {
        dist_local_responsible.assign(N_local_responsible_count, 0); // Default dummy is 0
        if (is_responsible_for_global_vertex(SOURCE_VERTEX_GLOBAL_ID)) {
            int local_src_idx = global_to_local_idx(SOURCE_VERTEX_GLOBAL_ID);
            if (local_src_idx >= 0 && local_src_idx < dist_local_responsible.size()) {
                dist_local_responsible[local_src_idx] = 0; // Source distance is 0
            }
        }
        // For any other non-source dummy output, it remains 0. If problem requires INF or -1 for unreachable dummy:
        // for (int i=0; i < N_local_responsible_count; ++i) {
        //    if ( (first_responsible_idx + i) != SOURCE_VERTEX_GLOBAL_ID) dist_local_responsible[i] = SOME_DUMMY_INF_VALUE_LIKE_MINUS_1_OR_ACTUAL_INF;
        // }
        // The current dummy output gives 0 for all, which includes source if owned. This might be okay for skeleton.
    }


    for (int i = 0; i < N_local_responsible_count; ++i) {
        if (dist_local_responsible[i] == INF) {
            // Consider problem spec for INF output: often -1 or a very large number
            outfile_stream << -1 << std::endl; // Assuming -1 for INF
        } else {
            outfile_stream << dist_local_responsible[i] << std::endl;
        }
    }
    outfile_stream.close();

    MPI_Finalize();
    return 0;
}