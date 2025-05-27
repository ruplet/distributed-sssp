// main.cpp
#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <limits>
#include <mpi.h>
#include <cmath> // For floor
#include <map>   // Potentially for sparse buckets

// --- Configuration Constants (Tunable / Flags) ---
// Set DELTA via command line or define a default here
// const long long DEFAULT_DELTA = 10; // Example, will be overriden by command line
const bool ENABLE_DEBUG_PRINT = false; // For verbose output
const bool USE_MAP_FOR_BUCKETS = false; // Alternative bucket implementation

// --- MPI Information ---
int world_rank, world_size;

// --- Graph Data (Local to each process) ---
long long N_global; // Total number of vertices in the graph
int N_local;    // Number of vertices owned by this process
int start_node; // Global ID of the first vertex owned by this process
int end_node;   // Global ID of the last vertex owned by this process (exclusive)

std::vector<std::vector<std::pair<int, int>>> adj; // Adjacency list: adj[local_u] -> {global_v, weight}
std::vector<long long> dist;                     // Distances to owned vertices

// Bucket structure:
// Option 1: Vector of vectors (if max bucket index is manageable)
std::vector<std::vector<int>> local_buckets_vec; // Stores local indices
// Option 2: Map (if bucket indices are sparse or max is too large)
std::map<int, std::vector<int>> local_buckets_map; // bucket_idx -> vector of local_indices

const long long INF = std::numeric_limits<long long>::max();
const int INF_BUCKET_INDEX = std::numeric_limits<int>::max();


// --- Helper Functions ---
int get_owner(int global_v_idx) {
    if (N_global == 0) return 0; // Avoid division by zero if N_global not set
    // Simple block distribution
    long long vertices_per_process = (N_global + world_size - 1) / world_size;
    return std::min((long long)world_size - 1, global_v_idx / vertices_per_process);
}

int global_to_local(int global_v_idx) {
    return global_v_idx - start_node;
}

bool is_owned(int global_v_idx) {
    return global_v_idx >= start_node && global_v_idx < end_node;
}

// Bucket management wrappers
void add_to_bucket(int local_v_idx, int bucket_idx) {
    if (bucket_idx == INF_BUCKET_INDEX) return; // Don't add to infinity bucket explicitly used for logic
    if (USE_MAP_FOR_BUCKETS) {
        local_buckets_map[bucket_idx].push_back(local_v_idx);
    } else {
        if (bucket_idx >= local_buckets_vec.size()) {
            local_buckets_vec.resize(bucket_idx + 1); // Grow if needed
        }
        local_buckets_vec[bucket_idx].push_back(local_v_idx);
    }
}

void remove_from_bucket(int local_v_idx, int bucket_idx) {
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
        // Efficient removal if order doesn't matter: swap with last and pop
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
        return local_buckets_map[bucket_idx]; // Creates if not exists, good for adding
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

// Message structure for relax operations
struct RelaxMessage {
    int v_global;
    long long new_dist;
};


// --- Delta-Stepping Core Logic ---
void delta_stepping(int source_global_idx, long long delta_val) {
    // Initialization
    dist.assign(N_local, INF);
    if (USE_MAP_FOR_BUCKETS) {
        local_buckets_map.clear();
    } else {
        for(auto& bucket : local_buckets_vec) bucket.clear();
        // Heuristic for initial size: Max possible SP could be N * w_max.
        // Buckets = N * w_max / delta. This can be huge.
        // Max shorted distance is more like D_graph * w_max.
        // RMAT graphs often have small diameter.
        // Max reasonable value around 2 * W_MAX / DELTA * C (C~10-100) (from some papers for small deltas)
        // For now, let it grow dynamically or rely on map.
        // Let's give a moderate initial size for vector approach if chosen
        if(!USE_MAP_FOR_BUCKETS && local_buckets_vec.empty()) local_buckets_vec.resize(1024);
    }


    if (is_owned(source_global_idx)) {
        int source_local_idx = global_to_local(source_global_idx);
        dist[source_local_idx] = 0;
        add_to_bucket(source_local_idx, get_bucket_index(0, delta_val));
    }

    int k = 0; // Current bucket index being processed

    std::vector<RelaxMessage> outgoing_relax_requests_flat;
    std::vector<int> send_counts(world_size);
    std::vector<int> sdispls(world_size);
    std::vector<RelaxMessage> incoming_relax_requests_flat;
    std::vector<int> recv_counts(world_size);
    std::vector<int> rdispls(world_size);
    
    std::vector<std::vector<RelaxMessage>> outgoing_per_proc_buffer(world_size);
    
    MPI_Datatype relax_message_type;

    while (true) {
        // Find the smallest k such that B_k is non-empty globally
        // Each process finds its local minimal k' >= k
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

        if (k == INF_BUCKET_INDEX) {
            break; // Termination condition
        }

        if (ENABLE_DEBUG_PRINT && world_rank == 0) {
            std::cout << "Epoch for k = " << k << std::endl;
        }
        
        std::vector<int> S_k; // Active vertices for current bucket k in current phase. Store local indices.
                              // Initial S_k for the epoch = B_k
        if (!is_bucket_empty_locally(k)) {
             S_k = get_bucket_list_ref(k); // Get a copy
            }
            
            
            // ProcessBucket(k)
        while (true) { // Phases loop
            long long S_k_local_size = S_k.size();
            long long S_k_global_size;
            MPI_Allreduce(&S_k_local_size, &S_k_global_size, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);

            if (S_k_global_size == 0) {
                break; // No more active vertices in B_k globally for this epoch
            }
            
            if (ENABLE_DEBUG_PRINT && world_rank == 0) {
                 std::cout << "  Phase for k = " << k << " |S_k| global = " << S_k_global_size << std::endl;
            }


            for (int i = 0; i < world_size; ++i) outgoing_per_proc_buffer[i].clear();
            outgoing_relax_requests_flat.clear(); // Clear buffer for new messages

            // 1. Generate relax requests
            for (int u_local : S_k) {
                long long dist_u = dist[u_local];
                if (dist_u == INF) continue; 

                // int u_global = start_node + u_local; // This was unused.
                // The actual u_global is not directly needed here if adj stores global_v
                // and we're just sending d(u) for relaxation of its neighbors.
                // However, if logging or other logic needed u_global, it would be used.
                // For the current logic of Relax(u,v) as d(v) <- min(d(v), d(u)+w(u,v)),
                // the actual u_global is not required in the RelaxMessage itself,
                // only d(u) (which is dist_u) and w(u,v) (from adj) and v_global.
                // So we can safely remove it here.
                // If any future optimization requires sending u_global, it can be re-added.

                for (const auto& edge : adj[u_local]) {
                    int v_global = edge.first;
                    int weight = edge.second;
                    long long potential_new_dist_v = dist_u + weight;
                    
                    int v_owner = get_owner(v_global);
                    outgoing_per_proc_buffer[v_owner].push_back({v_global, potential_new_dist_v});
                }
            }
            S_k.clear(); // S_k for next phase will be repopulated

            // Prepare for Alltoallv: flatten outgoing_per_proc_buffer and set send_counts/sdispls
            int current_sdispl = 0;
            for(int p = 0; p < world_size; ++p) {
                send_counts[p] = outgoing_per_proc_buffer[p].size();
                sdispls[p] = current_sdispl;
                for(const auto& msg : outgoing_per_proc_buffer[p]) {
                    outgoing_relax_requests_flat.push_back(msg);
                }
                current_sdispl += send_counts[p];
            }

            // 2. Exchange relax requests (MPI_Alltoall to share counts, then MPI_Alltoallv for data)
            MPI_Alltoall(send_counts.data(), 1, MPI_INT, recv_counts.data(), 1, MPI_INT, MPI_COMM_WORLD);
            
            int total_incoming_messages = 0;
            int current_rdispl = 0;
            for(int p=0; p<world_size; ++p) {
                rdispls[p] = current_rdispl;
                total_incoming_messages += recv_counts[p];
                current_rdispl += recv_counts[p];
            }
            incoming_relax_requests_flat.resize(total_incoming_messages);

            MPI_Alltoallv(outgoing_relax_requests_flat.data(), send_counts.data(), sdispls.data(), MPI_BYTE, // Sending as bytes
                          incoming_relax_requests_flat.data(), recv_counts.data(), rdispls.data(), MPI_BYTE, // Receiving as bytes
                          MPI_COMM_WORLD); // Using MPI_BYTE with RelaxMessage struct, ensure packing/MPI_Datatype for production

            // For MPI_Alltoallv with structs, it's safer to define an MPI_Datatype
            // or send raw bytes after ensuring struct is packable or using char arrays.
            // For simplicity here, assuming direct byte transfer works across heterogeneous systems if needed,
            // but MPI_Datatype is robust. Size of RelaxMessage:
            int lengths[2] = {1, 1};
            MPI_Aint displacements[2];
            RelaxMessage dummy_msg;
            MPI_Get_address(&dummy_msg.v_global, &displacements[0]);
            MPI_Get_address(&dummy_msg.new_dist, &displacements[1]);
            MPI_Aint base_address;
            MPI_Get_address(&dummy_msg, &base_address);
            displacements[0] -= base_address;
            displacements[1] -= base_address;
            MPI_Datatype types[2] = {MPI_INT, MPI_LONG_LONG};
            MPI_Type_create_struct(2, lengths, displacements, types, &relax_message_type);
            MPI_Type_commit(&relax_message_type);

            // Re-do Alltoallv with MPI_Datatype
            MPI_Alltoallv(outgoing_relax_requests_flat.data(), send_counts.data(), sdispls.data(), relax_message_type,
                          incoming_relax_requests_flat.data(), recv_counts.data(), rdispls.data(), relax_message_type,
                          MPI_COMM_WORLD);


            // 3. Process received requests and update distances/buckets
            std::vector<int> changed_vertices_local_indices; // Store local indices of vertices whose d(v) changed

            for (const auto& msg : incoming_relax_requests_flat) {
                int v_global = msg.v_global;
                long long new_dist_v = msg.new_dist;

                if (is_owned(v_global)) {
                    int v_local = global_to_local(v_global);
                    long long old_dist_v = dist[v_local];

                    if (new_dist_v < old_dist_v) {
                        int old_bucket_idx = get_bucket_index(old_dist_v, delta_val);
                        
                        if (old_dist_v != INF) { // Only remove if it was in a valid bucket
                           remove_from_bucket(v_local, old_bucket_idx);
                        }
                        
                        dist[v_local] = new_dist_v;
                        int new_bucket_idx = get_bucket_index(new_dist_v, delta_val);
                        add_to_bucket(v_local, new_bucket_idx);
                        
                        changed_vertices_local_indices.push_back(v_local);
                    }
                }
            }
            
            // 4. Rebuild S_k for the next phase: A <- B_k intersect A' (A' is changed_vertices_local_indices)
            //    S_k should contain local indices of vertices that changed AND are currently in bucket k
            for (int v_local : changed_vertices_local_indices) {
                 if (get_bucket_index(dist[v_local], delta_val) == k) {
                    // Check for duplicates if S_k could have them (e.g. if add_to_bucket doesn't check)
                    // std::vector used for S_k, so duplicates might occur if not careful.
                    // However, each v_local from changed_vertices_local_indices is unique.
                    S_k.push_back(v_local);
                 }
            }
            // Make S_k unique if necessary (e.g. if sources of changes could re-add)
            std::sort(S_k.begin(), S_k.end());
            S_k.erase(std::unique(S_k.begin(), S_k.end()), S_k.end());

        } // End phases loop for ProcessBucket(k)
        
        // Epoch for B_k is done. Remove B_k itself as its vertices are now "settled" for this k.
        // This is implicitly handled as S_k will be empty and we look for k' > k.
        // If vertices moved to k from k_higher, they will be caught when B_k is re-evaluated if k is visited again.
        // However, delta-stepping usually processes k in increasing order and doesn't revisit.
        // The paper states "k <- min{i > k : Bi != empty}". This ensures forward progress.
        // If a Relax operation moves v to B_j where j < k (current epoch), that vertex v
        // is now in an "earlier" bucket. The current distributed implementation might not catch this immediately
        // unless k is reset or those earlier buckets are re-processed.
        // The Meyer/Sanders original paper clarifies that vertices relaxed into B_j (j < k) are
        // added to B_j and processed when B_j is handled. Since we find min k globally,
        // if some process puts a vertex into B_j (j < current k), the next global min k
        // could become j. So, k must be reset to 0 at the start of finding the next global min k for correctness,
        // or simply k_search_start_boundary = 0.
        // The pseudocode: "k <- min{i > k : Bi != empty}". This suggests we always move forward.
        // This means Relax(u,v) where v goes to B_j (j<k) will make B_j non-empty, but the algorithm
        // as written would only process it if k loops back, which it doesn't.
        // This implies the "light" edges (w <= Delta) are the ones expected to keep vertices in B_k or move to B_{k+1} etc.
        // And "heavy" edges (w > Delta) are those that can put things into B_{k+m}.
        // The critical part from paper: "If j < i, move v from Bi to Bj".
        // This means if a vertex is currently in B_k, and gets relaxed to B_{k-1}, it moves there.
        // The next chosen global minimum k should find it. My k finding logic needs to scan from 0.

        // Corrected k finding logic:
        // Scan all buckets globally to find the absolute minimum non-empty bucket index.
        k = 0; // Reset search base for k for next epoch for full correctness,
               // ensuring any vertex moved to an earlier bucket is found.

    } // End while(true) epochs loop
    
    MPI_Type_free(&relax_message_type);
}


void generate_rmat_graph_locally(long long num_vertices, long long num_edges_per_proc_approx, int max_w, unsigned int seed) {
    N_global = num_vertices;
    long long vertices_per_process_ideal = (N_global + world_size - 1) / world_size;
    start_node = world_rank * vertices_per_process_ideal;
    end_node = std::min(N_global, (long long)(world_rank + 1) * vertices_per_process_ideal);
    N_local = end_node - start_node;

    adj.assign(N_local, std::vector<std::pair<int, int>>());
    dist.resize(N_local);

    if (N_local == 0 && N_global > 0) return;

    std::mt19937 gen(seed + world_rank);
    std::uniform_real_distribution<> dis(0.0, 1.0);
    double a = 0.57, b = 0.19, c = 0.19; // d = 1.0 - a - b - c = 0.05

    for (long long i = 0; i < num_edges_per_proc_approx; ++i) {
        if (N_local == 0) break;
        int u_local = gen() % N_local;
        int u_global = start_node + u_local; // u_global is used here

        long long v_target_global = -1;
        
        // RMAT-like target selection
        if (N_global > 0) { // Ensure N_global is positive
            long long current_N_partition = N_global;
            long long current_v_offset = 0;
            int scale = 0;
            if (N_global > 1) scale = static_cast<int>(floor(log2(N_global -1))) +1; // Number of bits for N_global vertices
            if (N_global == 1) scale = 1;


            for (int s = 0; s < scale && current_N_partition > 1; ++s) {
                double r_val = dis(gen);
                long long half_partition = (current_N_partition + 1) / 2; // More robust for odd N

                if (r_val < a) { // Quadrant 00 (top-left)
                    current_N_partition = half_partition;
                } else if (r_val < a + b) { // Quadrant 01 (top-right)
                    current_v_offset += half_partition;
                    current_N_partition -= half_partition; // Size of the remaining part
                } else if (r_val < a + b + c) { // Quadrant 10 (bottom-left)
                    current_N_partition = half_partition;
                } else { // Quadrant 11 (bottom-right)
                    current_v_offset += half_partition;
                    current_N_partition -= half_partition;
                }
            }
            v_target_global = current_v_offset + (gen() % std::max(1LL, current_N_partition));
            v_target_global = std::min(N_global - 1, std::max(0LL, v_target_global)); // Clamp
        } else {
             v_target_global = 0; // Only one choice if N_global is 0 or 1 (though N_global should be >0 from input)
        }


        if (v_target_global == u_global && N_global > 1) v_target_global = (v_target_global + 1) % N_global;
        else if (N_global == 1) v_target_global = u_global; // Only self-loops if N=1

        int weight = 1 + (gen() % max_w);
        adj[u_local].push_back({(int)v_target_global, weight});
    }
}

// --- Main ---
int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (argc < 5) {
        if (world_rank == 0) {
            std::cerr << "Usage: " << argv[0] << " <num_vertices> <avg_degree_approx> <max_edge_weight> <source_vertex_id> [delta]" << std::endl;
            std::cerr << "Example for 1M vertices, avg degree 16, max_w 100, src 0, delta 10: "
                      << argv[0] << " 1000000 16 100 0 10" << std::endl;
        }
        MPI_Finalize();
        return 1;
    }

    long long N_arg = std::stoll(argv[1]);
    long long avg_degree_arg = std::stoll(argv[2]);
    int max_w_arg = std::stoi(argv[3]);
    int source_idx_arg = std::stoi(argv[4]);
    long long delta_param = (argc > 5) ? std::stoll(argv[5]) : N_arg / (100 * world_size) ; // Default delta heuristic
    if (delta_param <= 0) delta_param = 1;


    if (world_rank == 0) {
        std::cout << "Running Delta-Stepping SSSP" << std::endl;
        std::cout << "Processes: " << world_size << std::endl;
        std::cout << "Global Vertices (N): " << N_arg << std::endl;
        std::cout << "Avg Degree Approx: " << avg_degree_arg << std::endl;
        std::cout << "Max Edge Weight (w_max): " << max_w_arg << std::endl;
        std::cout << "Source Vertex: " << source_idx_arg << std::endl;
        std::cout << "Delta (Δ): " << delta_param << std::endl;
        std::cout << "Using " << (USE_MAP_FOR_BUCKETS ? "std::map" : "std::vector") << " for buckets." << std::endl;
    }
    
    long long num_edges_to_generate_total = N_arg * avg_degree_arg / 2; // /2 because (u,v) and (v,u) would make degree 2 from one conceptual edge
    if (N_arg * avg_degree_arg < 0) num_edges_to_generate_total = N_arg /2; // overflow check hack
    
    long long edges_per_proc_to_generate = (num_edges_to_generate_total + world_size -1) / world_size;

    unsigned int seed = 12345;
    generate_rmat_graph_locally(N_arg, edges_per_proc_to_generate, max_w_arg, seed);

    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();

    delta_stepping(source_idx_arg, delta_param);

    MPI_Barrier(MPI_COMM_WORLD);
    double end_time = MPI_Wtime();

    if (world_rank == 0) {
        std::cout << "Delta-stepping finished." << std::endl;
        std::cout << "Time taken: " << (end_time - start_time) << " seconds." << std::endl;
    }

    // Verification (Optional: gather all distances and print some)
    if (ENABLE_DEBUG_PRINT && N_global <= 20) { // Print for small graphs
        std::vector<long long> all_distances(N_global);
        std::vector<long long> local_dist_buffer;
        if (is_owned(start_node)) { // To prevent access to dist[-1] or similar if N_local is 0
             local_dist_buffer.assign(dist.begin(), dist.begin() + N_local);
        }


        // Need to gather distances correctly based on N_local for each process
        std::vector<int> recvcounts_gather(world_size);
        std::vector<int> displs_gather(world_size);
        int local_N_val = N_local; // N_local might be 0 for some ranks if N_global < world_size

        MPI_Gather(&local_N_val, 1, MPI_INT, recvcounts_gather.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

        if (world_rank == 0) {
            displs_gather[0] = 0;
            for (int i = 1; i < world_size; ++i) {
                displs_gather[i] = displs_gather[i-1] + recvcounts_gather[i-1];
            }
        }
        
        MPI_Gatherv(local_dist_buffer.data(), local_N_val, MPI_LONG_LONG,
                    all_distances.data(), recvcounts_gather.data(), displs_gather.data(),
                    MPI_LONG_LONG, 0, MPI_COMM_WORLD);

        if (world_rank == 0) {
            std::cout << "Distances from source " << source_idx_arg << ":" << std::endl;
            for (long long i = 0; i < N_global; ++i) {
                if (all_distances[i] == INF) {
                    std::cout << "Vertex " << i << ": INF" << std::endl;
                } else {
                    std::cout << "Vertex " << i << ": " << all_distances[i] << std::endl;
                }
            }
        }
    }

    MPI_Finalize();
    return 0;
}