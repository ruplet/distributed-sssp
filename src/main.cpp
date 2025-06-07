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
    
    // In your provided code, you have data methods `isOwned` and `updateDist`.
    // Let's assume they work as intended.
    if (data.isOwned(root_rt_global_id)) {
        size_t local_idx = dist.globalToLocal(root_rt_global_id).value();
        data.updateDist(local_idx, 0); // Assuming updateDist takes local index now
        buckets[0].push_back(root_rt_global_id);
    }

    DebugLogger::getInstance().log("Initialization complete.");
    
    // Main loop: every iteration is one epoch
    while (true) {
        long long localMinK = INF;
        if (!buckets.empty()) {
            localMinK = buckets.begin()->first;
        }
        long long globalMinK = INF;
        MPI_Allreduce(&localMinK, &globalMinK, 1, MPI_LONG_LONG, MPI_MIN, MPI_COMM_WORLD);

        std::stringstream ss_epoch;
        ss_epoch << "\n--- Epoch Start --- Global Min k: " << (globalMinK == INF ? "INF" : std::to_string(globalMinK));
        DebugLogger::getInstance().log(ss_epoch.str());

        if (globalMinK == INF) {
            DebugLogger::getInstance().log("Termination condition met. Exiting.");
            break;
        }

        long long currentK = globalMinK;
        
        // --- PHASES WITHIN AN EPOCH ---
        // *** THE CORRECTED DEADLOCK-FREE LOOP STRUCTURE ***
        while (true) {
            // STEP 1: All processes collectively decide if there is any work left for this 'k'.
            int local_has_work = (buckets.count(currentK) && !buckets[currentK].empty()) ? 1 : 0;
            int global_has_work = 0;
            MPI_Allreduce(&local_has_work, &global_has_work, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

            // If the global sum is 0, NO process has work for 'currentK'. ALL break the phase loop.
            if (global_has_work == 0) {
                DebugLogger::getInstance().log("  > No more work for k=" + std::to_string(currentK) + ". Ending epoch.");
                break; 
            }
            
            // We now know that at least one process has work, so ALL processes must participate in the phase.
            
            // Determine the active set S for this process (it might be empty, that's OK)
            std::vector<int> S;
            if (local_has_work) {
                S = buckets[currentK];
                buckets.erase(currentK);
            }

            std::vector<long long> old_dist_copy(data.getNResponsible());
            char* dirty_flags = data.getDirtyFlagsBuffer();
            for(size_t i = 0; i < data.getNResponsible(); ++i) {
                // Assuming data.getDist(local_idx) exists. Your main() uses data.data()[i]
                old_dist_copy[i] = data.getDist(i); 
                dirty_flags[i] = 0;
            }

            std::stringstream ss_phase;
            ss_phase << "  > Phase for k=" << currentK << " | My |S|=" << S.size() << " | Global work exists.";
            DebugLogger::getInstance().log(ss_phase.str());

            // --- FENCE 1: DEADLOCK-FREE ---
            DebugLogger::getInstance().log("FENCE SYNC pre 1");
            MPI_Win_fence(0, dist_window);
            MPI_Win_fence(0, dirty_window);
            DebugLogger::getInstance().log("FENCE SYNC 1");

            // --- Relaxation Step ---
            for (int u_global_id : S) {
                // Assuming data.getDist(global_id) also exists as per your code
                long long u_dist = data.getDist(u_global_id); 

                data.forEachNeighbor(u_global_id, [&](size_t vGlobalIdx, long long w) {
                    long long potential_new_dist = u_dist + w;
                    int v_owner = dist.getResponsibleProcessor(vGlobalIdx).value();
                    MPI_Aint v_disp = dist.globalToLocal(vGlobalIdx).value();
    
                    MPI_Accumulate(&potential_new_dist, 1, MPI_LONG_LONG, v_owner,
                                   v_disp, 1, MPI_LONG_LONG, MPI_MIN, dist_window);
                    
                    char dirty_val = 1;
                    MPI_Put(&dirty_val, 1, MPI_CHAR, v_owner, v_disp, 1, MPI_CHAR, dirty_window);
                });
            }

            // --- FENCE 2 ---
            DebugLogger::getInstance().log("FENCE SYNC pre 2");
            MPI_Win_fence(0, dist_window);
            MPI_Win_fence(0, dirty_window);
            DebugLogger::getInstance().log("FENCE SYNC 2");

            // --- Local Update ---
            std::vector<int> R;
            for (size_t v_local_idx = 0; v_local_idx < data.getNResponsible(); ++v_local_idx) {
                if (dirty_flags[v_local_idx] == 1) {
                    int v_global_id = data.getFirstResponsibleGlobalIdx() + v_local_idx;
                    long long new_dist = data.getDist(v_global_id);
                    long long old_dist = old_dist_copy[v_local_idx];
                    
                    // ... your correct re-bucketing logic from before ...
                    if (old_dist != INF) {
                        long long old_bucket_idx = old_dist / delta_val;
                        if (buckets.count(old_bucket_idx)) {
                            auto& vec = buckets[old_bucket_idx];
                            vec.erase(std::remove(vec.begin(), vec.end(), v_global_id), vec.end());
                            if (vec.empty()) buckets.erase(old_bucket_idx);
                        }
                    }
                    long long new_bucket_idx = new_dist / delta_val;
                    if (new_bucket_idx == currentK) {
                        R.push_back(v_global_id);
                    } else {
                        buckets[new_bucket_idx].push_back(v_global_id);
                    }
                }
            }
            
            // Put light-edge vertices back into the current bucket for the next phase.
            if (!R.empty()) {
                buckets[currentK] = R;
            }
        } // end of while(true) phase loop
    } // end of while(true) epoch loop
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