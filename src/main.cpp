#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>
#include <mpi.h>
#include <cmath>
#include <map>
#include <fstream>
#include <sstream>
#include <string>

// --- Configuration Flags for Heuristics (all disabled by default) ---
const bool ENABLE_IOS_HEURISTIC = false;
const bool ENABLE_PRUNING_HEURISTIC = false;
// const bool ENABLE_HYBRIDIZATION = false; // Not detailed enough in prompt to stub
// const bool ENABLE_LOAD_BALANCING = false; // Not detailed enough in prompt to stub

const bool SKIP_COMPUTATIONS_FOR_NOW = false; // Set to false to run algorithm

// MPI Information
int world_rank, world_size;
MPI_Win dist_window; // MPI Window for one-sided access to distances

// Graph Data & Buckets
long long N_global_total_vertices;
int N_local_responsible_count;    // Number of vertices this process *owns*
int first_responsible_idx;        // Global ID of the first owned vertex
int last_responsible_idx;         // Global ID of the last owned vertex (inclusive)

std::vector<std::vector<std::pair<int, int>>> adj_local_responsible; // adj[local_idx] -> {global_neighbor_idx, weight}
std::vector<long long> dist_local_responsible;                      // dist[local_idx]
std::map<int, std::vector<int>> B; // Buckets Bk: bucket_idx -> vector of LOCAL indices

const long long INF = std::numeric_limits<long long>::max();

// --- Helper Functions ---
int get_owner_of_global_vertex(int global_v_idx) {
    if (N_global_total_vertices == 0 || world_size == 0) return 0;
    long long vertices_per_process_ideal = (N_global_total_vertices + world_size - 1) / world_size;
    if (vertices_per_process_ideal == 0 && N_global_total_vertices > 0) vertices_per_process_ideal = 1; // Avoid div by zero for small N
    else if (vertices_per_process_ideal == 0 && N_global_total_vertices == 0) return 0;

    int owner = global_v_idx / vertices_per_process_ideal;
    return std::min(owner, world_size - 1);
}

int global_to_local_idx(int global_v_idx) {
    if (global_v_idx >= first_responsible_idx && global_v_idx <= last_responsible_idx) {
        return global_v_idx - first_responsible_idx;
    }
    return -1; // Not owned
}

bool is_owned_by_current_proc(int global_v_idx) {
    return global_v_idx >= first_responsible_idx && global_v_idx <= last_responsible_idx;
}

int get_bucket_idx_for_dist(long long current_dist, long long delta_val) {
    if (current_dist == INF) return -1; // Sentinel for B_infinity (not explicitly in map B)
    if (delta_val <= 0) return -1; // Avoid division by zero or negative delta
    return static_cast<int>(current_dist / delta_val);
}

void add_vertex_to_local_bucket(int local_v_idx, int bucket_idx_j) {
    if (bucket_idx_j != -1) { // Don't add to B_infinity map
        B[bucket_idx_j].push_back(local_v_idx);
    }
}

void remove_vertex_from_local_bucket(int local_v_idx, int bucket_idx_i) {
    if (bucket_idx_i == -1) return; // Not in map B
    auto map_iter = B.find(bucket_idx_i);
    if (map_iter != B.end()) {
        std::vector<int>& bucket_vec = map_iter->second;
        auto vec_iter = std::find(bucket_vec.begin(), bucket_vec.end(), local_v_idx);
        if (vec_iter != bucket_vec.end()) {
            *vec_iter = bucket_vec.back();
            bucket_vec.pop_back();
        }
        if (bucket_vec.empty()) {
            B.erase(map_iter);
        }
    }
}

// --- Relax Operation (One-Sided Push Model) ---
void perform_relax_push(int u_global, int v_global, int weight_uv, long long dist_u, long long delta_val,
                        std::vector<int>& local_A_prime_accumulator) {
    if (dist_u == INF) return;

    long long potential_new_dist_v = dist_u + weight_uv;
    int owner_of_v = get_owner_of_global_vertex(v_global);

    if (owner_of_v == world_rank) { // v is local
        int v_local_idx = global_to_local_idx(v_global);
        if (v_local_idx != -1) {
            if (potential_new_dist_v < dist_local_responsible[v_local_idx]) {
                long long old_dist_v = dist_local_responsible[v_local_idx];
                int old_bucket_idx_i = get_bucket_idx_for_dist(old_dist_v, delta_val);
                
                dist_local_responsible[v_local_idx] = potential_new_dist_v;
                local_A_prime_accumulator.push_back(v_local_idx);
                
                int new_bucket_idx_j = get_bucket_idx_for_dist(potential_new_dist_v, delta_val);
                if (new_bucket_idx_j != old_bucket_idx_i) {
                    remove_vertex_from_local_bucket(v_local_idx, old_bucket_idx_i);
                    add_vertex_to_local_bucket(v_local_idx, new_bucket_idx_j);
                }
            }
        }
    } else { // v is remote
        MPI_Aint target_disp_on_remote = global_to_local_idx(v_global); // This is what owner_of_v uses
        if (target_disp_on_remote != -1) { // This check should not be -1 if logic is correct
             // It should be: target_disp_on_remote = v_global - (owner_of_v * ideal_vpp); // More direct way
            long long vertices_per_process_ideal = (N_global_total_vertices + world_size - 1) / world_size;
            if (vertices_per_process_ideal == 0 && N_global_total_vertices > 0) vertices_per_process_ideal =1;

            MPI_Aint remote_start_node = owner_of_v * vertices_per_process_ideal;
            MPI_Aint target_actual_disp = v_global - remote_start_node;


            MPI_Accumulate(&potential_new_dist_v, 1, MPI_LONG_LONG,
                           owner_of_v, target_actual_disp, 1, MPI_LONG_LONG,
                           MPI_MIN, dist_window);
        }
    }
}

// --- Delta-Stepping Algorithm ---
void delta_stepping_algorithm(int root_rt_global_id, long long delta_val) {
    if (SKIP_COMPUTATIONS_FOR_NOW) {
        dist_local_responsible.assign(N_local_responsible_count, 0);
        if (is_owned_by_current_proc(root_rt_global_id)) {
            int local_rt_idx = global_to_local_idx(root_rt_global_id);
            if(local_rt_idx != -1 && local_rt_idx < dist_local_responsible.size())
                dist_local_responsible[local_rt_idx] = 0;
        }
        return;
    }

    dist_local_responsible.assign(N_local_responsible_count, INF);
    B.clear();
    if (is_owned_by_current_proc(root_rt_global_id)) {
        int rt_local_idx = global_to_local_idx(root_rt_global_id);
        if (rt_local_idx != -1) {
            dist_local_responsible[rt_local_idx] = 0;
            add_vertex_to_local_bucket(rt_local_idx, get_bucket_idx_for_dist(0, delta_val));
        }
    }

    MPI_Win_create(dist_local_responsible.data(), 
                   (N_local_responsible_count > 0 ? N_local_responsible_count : 0) * sizeof(long long), // Handle N_local_responsible_count = 0
                   sizeof(long long), 
                   MPI_INFO_NULL, MPI_COMM_WORLD, &dist_window);

    int k = 0;
    std::vector<long long> dist_snapshot_before_sync(N_local_responsible_count > 0 ? N_local_responsible_count : 0);
    
    // Loop // Epochs
    while (true) {
        std::vector<int> A_active_in_Bk_for_epoch;
        if (B.count(k)) {
            A_active_in_Bk_for_epoch = B[k];
            B.erase(k);
        }
        
        std::vector<int> A_current_phase_actives = A_active_in_Bk_for_epoch;

        // While A != empty (globally) // Phases
        while (true) {
            long long A_local_size = A_current_phase_actives.size();
            long long A_global_size;
            MPI_Allreduce(&A_local_size, &A_global_size, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);

            if (A_global_size == 0) break;

            if (N_local_responsible_count > 0) { // Only take snapshot if there are local vertices
                dist_snapshot_before_sync = dist_local_responsible;
            }
            std::vector<int> A_prime_locally_changed_this_phase;

            MPI_Win_fence(MPI_MODE_NOPRECEDE, dist_window);

            for (int u_local_idx : A_current_phase_actives) {
                if (u_local_idx < 0 || u_local_idx >= N_local_responsible_count) continue;
                long long current_dist_u = dist_local_responsible[u_local_idx];
                int u_global_idx = first_responsible_idx + u_local_idx;

                for (const auto& edge : adj_local_responsible[u_local_idx]) {
                    int v_global_idx = edge.first;
                    int weight = edge.second;
                    bool is_short_edge = (weight < delta_val);

                    if (ENABLE_IOS_HEURISTIC) {
                        long long potential_new_dist_v = (current_dist_u == INF) ? INF : current_dist_u + weight;
                        if (potential_new_dist_v != INF && get_bucket_idx_for_dist(potential_new_dist_v, delta_val) == k) {
                            perform_relax_push(u_global_idx, v_global_idx, weight, current_dist_u, delta_val, A_prime_locally_changed_this_phase);
                        }
                    } else {
                        if (!ENABLE_PRUNING_HEURISTIC || is_short_edge) {
                             perform_relax_push(u_global_idx, v_global_idx, weight, current_dist_u, delta_val, A_prime_locally_changed_this_phase);
                        }
                    }
                }
            }
            MPI_Win_fence(MPI_MODE_NOSUCCEED, dist_window); // Flushes ACCs
            // A stronger MPI_Win_fence(0, dist_window) might be needed if subsequent local reads depend on it,
            // but targets will see effects. The owner then processes these changes.

            std::vector<int> A_prime_all_changed_local_nodes;
            for(int local_idx : A_prime_locally_changed_this_phase) {
                A_prime_all_changed_local_nodes.push_back(local_idx);
            }
            
            if (N_local_responsible_count > 0) { // Process changes from remote only if local vertices exist
                for (int v_local_idx = 0; v_local_idx < N_local_responsible_count; ++v_local_idx) {
                    if (dist_local_responsible[v_local_idx] < dist_snapshot_before_sync[v_local_idx]) {
                        A_prime_all_changed_local_nodes.push_back(v_local_idx);
                        long long old_dist_snap = dist_snapshot_before_sync[v_local_idx];
                        int old_b_idx = get_bucket_idx_for_dist(old_dist_snap, delta_val);
                        int new_b_idx = get_bucket_idx_for_dist(dist_local_responsible[v_local_idx], delta_val);
                        if (new_b_idx != old_b_idx) {
                            remove_vertex_from_local_bucket(v_local_idx, old_b_idx);
                            add_vertex_to_local_bucket(v_local_idx, new_b_idx);
                        }
                    }
                }
            }
            std::sort(A_prime_all_changed_local_nodes.begin(), A_prime_all_changed_local_nodes.end());
            A_prime_all_changed_local_nodes.erase(
                std::unique(A_prime_all_changed_local_nodes.begin(), A_prime_all_changed_local_nodes.end()),
                A_prime_all_changed_local_nodes.end()
            );

            A_current_phase_actives.clear();
            for (int v_local_idx : A_prime_all_changed_local_nodes) {
                if (get_bucket_idx_for_dist(dist_local_responsible[v_local_idx], delta_val) == k) {
                    A_current_phase_actives.push_back(v_local_idx);
                }
            }
        }

        // --- Long Edge Phase / Outer-Short for IOS Stub ---
        if (N_local_responsible_count > 0) {
            dist_snapshot_before_sync = dist_local_responsible; // Snapshot before this phase
        }
        std::vector<int> A_prime_locally_changed_long_phase;

        MPI_Win_fence(MPI_MODE_NOPRECEDE, dist_window);
        for (int u_local_idx : A_active_in_Bk_for_epoch) {
            if (u_local_idx < 0 || u_local_idx >= N_local_responsible_count) continue;
            long long current_dist_u = dist_local_responsible[u_local_idx];
            if (get_bucket_idx_for_dist(current_dist_u, delta_val) != k && current_dist_u != INF) continue;
            int u_global_idx = first_responsible_idx + u_local_idx;

            for (const auto& edge : adj_local_responsible[u_local_idx]) {
                int v_global_idx = edge.first;
                int weight = edge.second;
                bool is_long_edge = (weight >= delta_val);

                if (ENABLE_IOS_HEURISTIC) {
                    long long potential_new_dist_v = (current_dist_u == INF) ? INF : current_dist_u + weight;
                    if ((potential_new_dist_v != INF && get_bucket_idx_for_dist(potential_new_dist_v, delta_val) != k) || is_long_edge) {
                         perform_relax_push(u_global_idx, v_global_idx, weight, current_dist_u, delta_val, A_prime_locally_changed_long_phase);
                    }
                } else if (is_long_edge) { // Basic Short/Long or Pruning context
                    if (ENABLE_PRUNING_HEURISTIC) {
                        // Future: if (use_pull_model_for_edge(...)) { /* initiate_pull */ } else { push }
                         perform_relax_push(u_global_idx, v_global_idx, weight, current_dist_u, delta_val, A_prime_locally_changed_long_phase);
                    } else { // Default: relax long edges in this phase
                         perform_relax_push(u_global_idx, v_global_idx, weight, current_dist_u, delta_val, A_prime_locally_changed_long_phase);
                    }
                }
            }
        }
        MPI_Win_fence(MPI_MODE_NOSUCCEED, dist_window);

        // Process changes from this long-edge/outer-short phase
        // for(int local_idx : A_prime_locally_changed_long_phase) { /* Similar to A_prime_all_changed update */ }
        if (N_local_responsible_count > 0) {
            for (int v_local_idx = 0; v_local_idx < N_local_responsible_count; ++v_local_idx) {
                if (dist_local_responsible[v_local_idx] < dist_snapshot_before_sync[v_local_idx]) {
                    long long old_dist_snap = dist_snapshot_before_sync[v_local_idx];
                    int old_b_idx = get_bucket_idx_for_dist(old_dist_snap, delta_val);
                    int new_b_idx = get_bucket_idx_for_dist(dist_local_responsible[v_local_idx], delta_val);
                    if (new_b_idx != old_b_idx) {
                        remove_vertex_from_local_bucket(v_local_idx, old_b_idx);
                        add_vertex_to_local_bucket(v_local_idx, new_b_idx);
                    }
                }
            }
        }
        // --- End of ProcessBucket(k) ---

        int local_min_next_k = std::numeric_limits<int>::max();
        for (const auto& pair : B) {
            if (pair.first > k && !pair.second.empty()) {
                local_min_next_k = std::min(local_min_next_k, pair.first);
            }
        }
        int global_min_next_k;
        MPI_Allreduce(&local_min_next_k, &global_min_next_k, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

        if (global_min_next_k == std::numeric_limits<int>::max()) break;
        k = global_min_next_k;
    }
    MPI_Barrier(MPI_COMM_WORLD); // Ensure all processes finish before freeing window
    if (dist_window != MPI_WIN_NULL) { // Check if window was created
      MPI_Win_free(&dist_window);
    }
}

void process_input_and_load_graph_from_stream(std::istream& instream) {
    std::string line;
    if (!std::getline(instream, line)) { std::cerr << "Rank " << world_rank << ": Fail read L1\n"; MPI_Abort(MPI_COMM_WORLD, 1); return; }
    std::istringstream iss_first_line(line);
    if (!(iss_first_line >> N_global_total_vertices >> first_responsible_idx >> last_responsible_idx)) { std::cerr << "Rank " << world_rank << ": Fail parse L1\n"; MPI_Abort(MPI_COMM_WORLD, 1); return; }

    if (first_responsible_idx < 0 || 
        (N_global_total_vertices > 0 && (last_responsible_idx < first_responsible_idx || last_responsible_idx >= N_global_total_vertices)) || 
        (N_global_total_vertices == 0 && (first_responsible_idx != 0 || last_responsible_idx != -1 ))) { // canonical empty: 0, -1
        std::cerr << "Rank " << world_rank << ": Invalid responsible idx N=" << N_global_total_vertices << " f=" << first_responsible_idx << " l=" << last_responsible_idx <<std::endl; 
        MPI_Abort(MPI_COMM_WORLD, 1); return;
    }
    
    N_local_responsible_count = (N_global_total_vertices == 0 || first_responsible_idx > last_responsible_idx) ? 0 : (last_responsible_idx - first_responsible_idx + 1);
    adj_local_responsible.assign(N_local_responsible_count, std::vector<std::pair<int, int>>());
    dist_local_responsible.resize(N_local_responsible_count); // Ensure sized for MPI_Win_create and algo

    int u_edge, v_edge, w_edge;
    while (std::getline(instream, line)) {
        if (line.empty()) continue;
        std::istringstream iss_edge(line);
        if (!(iss_edge >> u_edge >> v_edge >> w_edge)) { std::cerr << "Rank " << world_rank << ": Fail parse edge\n"; MPI_Abort(MPI_COMM_WORLD, 1); return; }
        
        if (N_global_total_vertices > 0 && (u_edge < 0 || u_edge >= N_global_total_vertices || v_edge < 0 || v_edge >= N_global_total_vertices || w_edge < 0)) {
             std::cerr << "Rank " << world_rank << ": Invalid edge data " << u_edge << " " << v_edge << " " << w_edge << "\n"; MPI_Abort(MPI_COMM_WORLD, 1); return;
        } else if (N_global_total_vertices == 0 && (u_edge !=0 || v_edge !=0)) { // Or simply no edges for N=0
             std::cerr << "Rank " << world_rank << ": Edge for N=0 graph\n"; MPI_Abort(MPI_COMM_WORLD, 1); return;
        }


        if (is_owned_by_current_proc(u_edge)) {
            if (N_local_responsible_count > 0) // Check to prevent segfault if N_local is 0 but u is in range (e.g. 0, -1 range)
              adj_local_responsible[global_to_local_idx(u_edge)].push_back({v_edge, w_edge});
        }
        if (is_owned_by_current_proc(v_edge)) {
             if (N_local_responsible_count > 0)
               adj_local_responsible[global_to_local_idx(v_edge)].push_back({u_edge, w_edge});
        }
    }
}

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    dist_window = MPI_WIN_NULL; // Initialize to null

    if (argc < 3) {
        if (world_rank == 0) std::cerr << "Usage: " << argv[0] << " <input_file> <output_file> [delta]" << std::endl;
        MPI_Finalize(); return 1;
    }
    std::string input_filename = argv[1];
    std::string output_filename = argv[2];
    long long delta_param = (argc > 3) ? std::stoll(argv[3]) : 10;
    if (delta_param <= 0) delta_param = 1;

    std::ifstream infile(input_filename);
    if (!infile.is_open()) { std::cerr << "Rank " << world_rank << ": Cannot open " << input_filename << std::endl; MPI_Abort(MPI_COMM_WORLD, 1); }
    process_input_and_load_graph_from_stream(infile);
    infile.close();
    
    // dist_local_responsible is resized in process_input_and_load_graph_from_stream

    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();

    delta_stepping_algorithm(0, delta_param);

    MPI_Barrier(MPI_COMM_WORLD); // Ensure all processes done before anyone exits/prints final time
    double end_time = MPI_Wtime();

    if (world_rank == 0 && !SKIP_COMPUTATIONS_FOR_NOW) {
        std::cout << "Delta-stepping (one-sided) finished. Time: " << (end_time - start_time) << "s." << std::endl;
    } else if (world_rank == 0 && SKIP_COMPUTATIONS_FOR_NOW) {
        std::cout << "Delta-stepping SKIPPED (dummy output)." << std::endl;
    }
    
    std::ofstream outfile_stream(output_filename);
    if (!outfile_stream.is_open()) { std::cerr << "Rank " << world_rank << ": Cannot open " << output_filename << std::endl; MPI_Abort(MPI_COMM_WORLD, 1); }
    
    // Ensure dist_local_responsible has right size even if skipped
    if (dist_local_responsible.size() != N_local_responsible_count) {
        dist_local_responsible.assign(N_local_responsible_count, 0); // Default dummy value
        if (SKIP_COMPUTATIONS_FOR_NOW && is_owned_by_current_proc(0) ) {
             int local_rt_idx = global_to_local_idx(0);
             if(local_rt_idx != -1 && local_rt_idx < dist_local_responsible.size()) dist_local_responsible[local_rt_idx] = 0;
        }
    }


    for (int i = 0; i < N_local_responsible_count; ++i) {
        outfile_stream << (dist_local_responsible[i] == INF ? -1 : dist_local_responsible[i]) << std::endl;
    }
    outfile_stream.close();

    MPI_Finalize();
    return 0;
}