#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>
#include <mpi.h>
#include <map>
#include <fstream>
#include <stdexcept>
#include <string>

#include "block_dist.hpp"
#include "parse_data.hpp"
#include "logger.hpp"

const long long DEFAULT_DELTA = 10;

// --- Configuration Flags for Heuristics (all disabled by default) ---
// const bool ENABLE_IOS_HEURISTIC = false;
// const bool ENABLE_PRUNING_HEURISTIC = false;
// const bool ENABLE_HYBRIDIZATION = false; // Not detailed enough in prompt to stub
// const bool ENABLE_LOAD_BALANCING = false; // Not detailed enough in prompt to stub

// const bool SKIP_COMPUTATIONS_FOR_NOW = false; // Set to false to run algorithm

int myRank, nProcessorsGlobal;


class VertexOwnershipException : public std::runtime_error {
public:
    VertexOwnershipException(int vertexId, int processRank)
        : std::runtime_error("Process " + std::to_string(processRank) +
                             " does not own vertex " + std::to_string(vertexId)),
          vertexId_(vertexId), processRank_(processRank) {}

    int vertexId() const { return vertexId_; }
    int processRank() const { return processRank_; }

private:
    int vertexId_;
    int processRank_;
};

class Fatal : public std::runtime_error {
public:
    Fatal(std::string what) : std::runtime_error(what) {}
};

struct RelaxRequest {
    int target_vertex_global_id;
    long long new_distance;
};


void delta_stepping_algorithm(
    Data& data,
    const BlockDistribution::Distribution& dist,
    MPI_Win& dist_window,
    MPI_Win& dirty_window,
    size_t root_rt_global_id,
    long long delta_val
) {
    std::map<long long, std::vector<int>> buckets;

    std::cerr << "Melduję się! proces: " << myRank << " posiadam wierzchołków: " << data.getNResponsible() << std::endl;
    
    if (data.isOwned(root_rt_global_id)) {
        data.updateDist(root_rt_global_id, 0);
        buckets[0].push_back(root_rt_global_id);
    }

    DebugLogger::getInstance().log("Initialization complete.");
    
    // Main loop: every iteration is one epoch
    while (true) {
        // Find the next globally non-empty bucket
        long long localMinK = INF;
        if (!buckets.empty()) {
            localMinK = buckets.begin()->first;
        }
        long long globalMinK = INF;
        MPI_Allreduce(&localMinK, &globalMinK, 1, MPI_LONG_LONG, MPI_MIN, MPI_COMM_WORLD);

        std::stringstream ss_epoch;
        ss_epoch << "\n--- Epoch Start --- Global Min k: " << (globalMinK == INF ? "INF" : std::to_string(globalMinK));
        DebugLogger::getInstance().log(ss_epoch.str());

        // No more work to be done! (assumption: input graph is connected!)
        if (globalMinK == INF) {
            DebugLogger::getInstance().log("Termination condition met. Exiting."); // <<< DEBUG
            break;
        }

        long long currentK = globalMinK;
        DebugLogger::getInstance().log_buckets("Before Phases", currentK, buckets); // <<< DEBUG

        // 3. --- PHASES WITHIN AN EPOCH ---
        // buckets.count() is 0 or 1
        // while (buckets.count(currentK) && !buckets[currentK].empty()) {
        int still_work_for_k = 1; // Assume there is work to loop at least once
        while (still_work_for_k > 0) {
            
            // --- Determine the active set S for this process ---
            std::vector<int> S;
            if (buckets.count(currentK) && !buckets[currentK].empty()) {
                S = buckets[currentK];
                buckets.erase(currentK);
            }
            
            // This is the old, deadlock-prone local check:
            // while (buckets.count(currentK) && !buckets[currentK].empty()) {
                // std::vector<int> S = buckets[currentK];
                // buckets.erase(currentK);

            std::vector<long long> old_dist_copy(data.getNResponsible());
            char* dirty_flags = data.getDirtyFlagsBuffer(); // Get buffer pointer
            // Reset local dirty flags and make a copy of distances
            for(int i = 0; i < data.getNResponsible(); ++i) {
                old_dist_copy[i] = data.getDist(i);
                dirty_flags[i] = 0; // Clean all flags before the phase
            }

            std::stringstream ss_phase;
            ss_phase << "  > Phase for k=" << currentK << " | Processing |S|=" << S.size() << " vertices.";
            DebugLogger::getInstance().log(ss_phase.str());

            // --- FENCE 1: Start one-sided communication epoch ---
            DebugLogger::getInstance().log("FENCE SYNC pre 1");
            MPI_Win_fence(0, dist_window);
            MPI_Win_fence(0, dirty_window);
            DebugLogger::getInstance().log("FENCE SYNC 1");

            // --- Relaxation Step using MPI_Accumulate ---
            for (int u_global_id : S) {
                // int u_local_idx = dist.globalToLocal(u_global_id).value();
                long long u_dist = data.getDist(u_global_id);

                data.forEachNeighbor(u_global_id, [&](size_t vGlobalIdx, long long w) {
                    long long potential_new_dist = u_dist + w;
                    int v_owner = dist.getResponsibleProcessor(vGlobalIdx).value();
                    MPI_Aint v_disp = dist.globalToLocal(vGlobalIdx).value(); // Displacement in target's window
    
                    // Atomically update remote distance: dist[v] = min(dist[v], new_dist)
                    MPI_Accumulate(&potential_new_dist, 1, MPI_LONG_LONG, v_owner,
                                   v_disp, 1, MPI_LONG_LONG, MPI_MIN, dist_window);
                    
                    char dirty_val = 1;
                    MPI_Put(&dirty_val, 1, MPI_CHAR, v_owner, v_disp, 1, MPI_CHAR, dirty_window);
                });
            }

            // --- FENCE 2: Complete all accumulate operations ---
            DebugLogger::getInstance().log("FENCE SYNC pre 2");
            MPI_Win_fence(0, dist_window);
            MPI_Win_fence(0, dirty_window);
            DebugLogger::getInstance().log("FENCE SYNC 2");

            // --- Local Update: Check for changes and re-bucket ---
            // char* dirty_flags = data.getDirtyFlagsBuffer();
            std::vector<int> R; // For vertices that were relaxed but remain in bucket k
            for (int v_local_idx = 0; v_local_idx < data.getNResponsible(); ++v_local_idx) {
                if (dirty_flags[v_local_idx] == 1) {
                    int v_global_id = data.getFirstResponsibleGlobalIdx() + v_local_idx;
                    long long new_dist = data.getDist(v_global_id);
                    
                    // Since it's dirty, its old distance MUST be what we copied.
                    long long old_dist = old_dist_copy[v_local_idx];

                    // <<< DEBUG: Log the relaxation
                    std::stringstream ss_relax;
                    ss_relax << "    Relaxed v" << v_global_id << " -> " << new_dist;
                    DebugLogger::getInstance().log(ss_relax.str());
                    
                    // Expensive search for the vertex in all buckets to find its old location
                    if (old_dist != INF) {
                        long long old_bucket_idx = old_dist / delta_val;

                        // We only need to check the specific bucket where it used to be.
                        if (buckets.count(old_bucket_idx)) {
                            auto& old_bucket_vector = buckets[old_bucket_idx];
                            
                            // Use the standard "remove-erase idiom" to efficiently delete the element.
                            // std::remove moves all elements *not* equal to v_global_id to the front,
                            // and returns an iterator to the new logical end of the vector.
                            // .erase() then cleans up from that point to the physical end.
                            old_bucket_vector.erase(
                                std::remove(old_bucket_vector.begin(), old_bucket_vector.end(), v_global_id),
                                old_bucket_vector.end()
                            );

                            // If removing that vertex made the bucket empty, erase the bucket key itself.
                            if (old_bucket_vector.empty()) {
                                buckets.erase(old_bucket_idx);
                            }
                        }
                    }

                    // Now add to the new bucket.
                    long long new_bucket_idx = new_dist / delta_val;
                    if (new_bucket_idx == currentK) {
                        R.push_back(v_global_id);
                    } else {
                        buckets[new_bucket_idx].push_back(v_global_id);
                    }
                }
            }
            DebugLogger::getInstance().log_buckets("After Phase Update", currentK, buckets); // <<< DEBUG
            
            // Put vertices from R back into the current bucket for the next phase.
            if (!R.empty()) {
                buckets[currentK].insert(buckets[currentK].end(), R.begin(), R.end());
            }

            // <<< THE FIX: After local updates, globally check if we need another phase for this k. >>>
            int local_has_work_for_k = buckets.count(currentK) && !buckets[currentK].empty() ? 1 : 0;
            MPI_Allreduce(&local_has_work_for_k, &still_work_for_k, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
            // After re-bucketing, the active set for the next phase would be what is now in B_k
        }
    }
    return;
}


int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);

    // MPI_Win dist_window = MPI_WIN_NULL; // MPI Window for one-sided access to distances
    MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
    MPI_Comm_size(MPI_COMM_WORLD, &nProcessorsGlobal);

    DebugLogger::getInstance().init(myRank);

    // assumption: argc is the same among processors
    if (argc < 3) {
        if (myRank == 0) std::cerr << "Usage: " << argv[0] << " <input_file> <output_file> [delta > 0]" << std::endl;
        MPI_Finalize();
        return 1;
    }
    std::string input_filename = argv[1];
    std::string output_filename = argv[2];
    long long delta_param = (argc > 3) ? std::stoll(argv[3]) : DEFAULT_DELTA;
    // assumption: delta CLI arg is hardcoded in mpirun script, so it will be the same everywhere
    if (delta_param <= 0) {
        if (myRank == 0) std::cerr << "Error: " << "delta param must be > 0" << std::endl;
        MPI_Finalize();
        return 1;
    }

    auto dataOpt = process_input_and_load_graph_from_stream(myRank, input_filename);
    if (!dataOpt.has_value()) {
        logError("Unable to parse data!");
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    auto data = *dataOpt;

    BlockDistribution::Distribution dist(nProcessorsGlobal, data.getNVerticesGlobal());
    auto distNRespOpt = dist.getNResponsibleVertices(myRank);
    if (!distNRespOpt.has_value()) {
        logError("Unable to take nResp from distribution");
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    auto distNResp = *distNRespOpt;
    if (distNResp != data.getNResponsible()) {
        std::cerr
            << "Rank "
            << myRank
            << ": mismatch in number of vertices owned by process: "
            << distNResp
            << " != "
            << data.getNResponsible()
            << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    auto respProcFstOpt = dist.getResponsibleProcessor(data.getFirstResponsibleGlobalIdx());
    auto respProcLstOpt = dist.getResponsibleProcessor(data.lastResponsibleGlobalIdx());
    if (!respProcFstOpt.has_value() || !respProcLstOpt.has_value()) {
        logError("Unable to obtain processor of first/last from distribution");
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    auto respProcFst = *respProcFstOpt;
    auto respProcLst = *respProcLstOpt;
    if (respProcFst != myRank || respProcLst != myRank) {
        std::cerr
            << "Rank "
            << myRank
            << ": mismatch in owner of vertices: "
            << respProcFst << " or " << respProcLst << " != " << myRank
            << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    std::ofstream outfile_stream(output_filename);
    if (!outfile_stream.is_open()) {
        std::cerr << "Rank " << myRank << ": Cannot open " << output_filename << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    // --- MPI WINDOW CREATION ---
    MPI_Win dist_window = MPI_WIN_NULL;
    MPI_Win dirty_window = MPI_WIN_NULL;
    MPI_Aint size = data.getNResponsible() * sizeof(long long);
    MPI_Win_create(
        data.data(), size, sizeof(long long),
        MPI_INFO_NULL, MPI_COMM_WORLD, &dist_window
    );

    MPI_Win_create(
        data.getDirtyFlagsBuffer(), data.getNResponsible() * sizeof(char), sizeof(char),
        MPI_INFO_NULL, MPI_COMM_WORLD, &dirty_window
    );

    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();
    try {
        delta_stepping_algorithm(data, dist, dist_window, dirty_window, 0, delta_param);
    } catch (Fatal& ex) {
        std::cerr
            << "Fatal error while Delta-stepping: "
            << ex.what()
            << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    MPI_Barrier(MPI_COMM_WORLD); // Ensure all processes done before anyone exits/prints final time
    double end_time = MPI_Wtime();
    if (myRank == 0) {
        std::cout << "Delta-stepping (one-sided) finished. Time: " << (end_time - start_time) << "s." << std::endl;
    }

    MPI_Win_free(&dist_window);
    MPI_Win_free(&dirty_window);

    for (size_t i = 0; i < data.getNResponsible(); ++i) {
        outfile_stream << (data.data()[i] == INF ? -1 : data.data()[i]) << std::endl;
    }
    outfile_stream.close();

    MPI_Finalize();
    return 0;
}