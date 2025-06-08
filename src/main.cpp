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


bool anyoneHasWork(const std::map<long long, std::vector<size_t>>& buckets, size_t bucketIdx) {
    int local_has_work = 0;
    auto it = buckets.find(bucketIdx);
    if (it != buckets.end() && !it->second.empty()) {
        local_has_work = 1;
    }

    int global_has_work = 0;
    MPI_Allreduce(&local_has_work, &global_has_work, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    if (global_has_work) { return true; }
    else { return false; }
}

std::vector<size_t> getActiveSet(const std::map<long long, std::vector<size_t>>& buckets, size_t bucketIdx) {
    std::vector<size_t> active;
    auto it = buckets.find(bucketIdx);
    if (it != buckets.end()) {
        active = it->second;
    }
    return active;
}

void updateBucketInfo(
    std::map<long long, std::vector<size_t>>& buckets,
    size_t vGlobalIdx,
    long long oldBucket,
    long long newBucket
) {
    auto newIt = buckets.find(newBucket);
    if (newIt != buckets.end()) {
        const auto& newVec = newIt->second;
        if (std::find(newVec.begin(), newVec.end(), vGlobalIdx) != newVec.end()) {
            throw Fatal("Vertex already present in new bucket!");
        }
    }

    if (oldBucket != INF) {
        auto oldIt = buckets.find(oldBucket);
        if (oldIt == buckets.end()) {
            throw Fatal("Old bucket not found!");
        }
        auto& oldVec = oldIt->second;
    
        auto pos = std::find(oldVec.begin(), oldVec.end(), vGlobalIdx);
        if (pos == oldVec.end()) {
            throw Fatal("Vertex not found in old bucket!");
        }
        oldVec.erase(pos);
    }

    buckets[newBucket].push_back(vGlobalIdx);
}

void setActiveSet(
    std::map<long long, std::vector<size_t>>& buckets,
    size_t bucketIdx,
    const std::vector<size_t>& activeSet
) {
    auto it = buckets.find(bucketIdx);
    if (!activeSet.empty()) {
        it->second = activeSet;
    } else {
        buckets.erase(it);
    }
}

void delta_stepping_algorithm(
    Data& data,
    const BlockDistribution::Distribution& dist,
    size_t root_rt_global_id,
    long long delta_val
) {
    std::map<long long, std::vector<size_t>> buckets;

    {
        std::stringstream ss;
        ss << "Process " << myRank << " processing " << data.getNResponsible() << " vertices!";
        DebugLogger::getInstance().log(ss.str());
    }
    
    if (data.isOwned(root_rt_global_id)) {
        data.updateDist(root_rt_global_id, 0);
        updateBucketInfo(buckets, root_rt_global_id, INF, 0);
    }
    
    // Main loop: every iteration is one epoch
    size_t epochNo = 0;
    while (true) {
        long long localMinK = INF;
        if (!buckets.empty()) {
            localMinK = buckets.begin()->first;
        }
        long long globalMinK = INF;
        MPI_Allreduce(&localMinK, &globalMinK, 1, MPI_LONG_LONG, MPI_MIN, MPI_COMM_WORLD);
        {
            std::stringstream ss;
            ss
                << "Process " << myRank << " starting epoch " << epochNo
                << ". Bucket considered: " << (globalMinK == INF ? "INF" : std::to_string(globalMinK))
                << "(raported my best bucket: " << localMinK << ", of " << buckets.begin()->second.size() << "nodes)";
            DebugLogger::getInstance().log(ss.str());
        }
        epochNo++;

        if (globalMinK == INF) {
            DebugLogger::getInstance().log("Termination condition met. Exiting.");
            break;
        }

        long long currentK = globalMinK;
        
        size_t phaseNo = 0;
        while (true) {
            // STEP 1: All processes collectively decide if there is any work left for this 'k'.
            // If the global sum is 0, NO process has work for 'currentK'. ALL break the phase loop.
            if (anyoneHasWork(buckets, currentK)) {
                std::stringstream ss;
                ss << "Process " << myRank << " no more work for k=" << currentK;
                DebugLogger::getInstance().log(ss.str());
                break; 
            }
            // We now know that at least one process has work, so ALL processes must participate in the phase.
            
            // Determine the active set S for this process (it might be empty, that's OK)
            std::vector<size_t> activeSet = getActiveSet(buckets, currentK);

            // std::vector<long long> distancesBeforeRelaxations = data.getCopyOfDistances();
            {
                std::stringstream ss;
                ss << "Process " << myRank << " starting phase " << phaseNo << " for k=" << currentK;
                ss << ". Active vertices: [";
                if (!activeSet.empty()) {
                    ss << activeSet[0];
                    for (auto it=activeSet.begin() + 1; it != activeSet.end(); ++it) {
                        ss << ", " << *it;
                    }
                }
                ss << "]";
                DebugLogger::getInstance().log(ss.str());
            }

            // FENCE 1
            DebugLogger::getInstance().log("FENCE SYNC 1: waiting...");
            data.syncWindowToActual();
            data.fence();
            DebugLogger::getInstance().log("FENCE SYNC 1: done! Performing relaxations...");

            // --- Relaxation Step ---
            for (auto u_global_id : activeSet) {
                auto u_dist = data.getDist(u_global_id); 

                data.forEachNeighbor(u_global_id, [&](size_t vGlobalIdx, long long w) {
                    auto potential_new_dist = u_dist + w;

                    auto ownerProcessOpt = dist.getResponsibleProcessor(vGlobalIdx);
                    if (!ownerProcessOpt.has_value()) { throw Fatal("Owner doesn't exist!"); }
                    size_t ownerProcess = *ownerProcessOpt;

                    auto dispAtownerOpt = dist.globalToLocal(vGlobalIdx);
                    if (!dispAtownerOpt.has_value()) { throw Fatal("Owner doesn't exist!"); }
                    MPI_Aint dispAtOwner = *dispAtownerOpt;

                    data.communicateRelax(vGlobalIdx, potential_new_dist, ownerProcess, dispAtOwner);
                });
            }

            // --- FENCE 2 ---
            DebugLogger::getInstance().log("FENCE SYNC 2: waiting...");
            data.fence();
            DebugLogger::getInstance().log("FENCE SYNC 2: done!");

            // Check which distances and buckets have been relaxed in our data
            std::vector<bool> wasUpdated(data.getNResponsible(), false);

            // we will only preserve updates vertices
            activeSet.clear();
            for (auto update : data.getUpdatesAndSyncDataToWin()) {
                auto vGlobalIdx = update.vGlobalIdx;
                auto prevDist = update.prevDist;
                auto newDist = update.newDist;

                auto oldBucket = prevDist == INF ? INF : prevDist / delta_val;
                auto newBucket = newDist / delta_val;

                updateBucketInfo(buckets, vGlobalIdx, oldBucket, newBucket);

                if (newBucket == currentK) {
                    activeSet.push_back(vGlobalIdx);
                }
            }
            setActiveSet(buckets, currentK, activeSet);

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
    auto& data = *dataOpt;

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

    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();
    try {
        delta_stepping_algorithm(data, dist, 0, delta_param);
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

    for (size_t i = 0; i < data.getNResponsible(); ++i) {
        outfile_stream << (data.data()[i] == INF ? -1 : data.data()[i]) << std::endl;
    }
    outfile_stream.close();

    MPI_Finalize();
    return 0;
}