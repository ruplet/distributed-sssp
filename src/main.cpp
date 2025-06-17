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

enum class LoggingLevel
{
    None,
    Progress,
    Debug
};

const long long DEFAULT_DELTA = 10;
const int DEFAULT_PROGESS_FREQ = 10;
// const float HYBRIDIZATION_THRESHOLD = 0.4;
LoggingLevel logging_level = LoggingLevel::Progress;
int myRank, nProcessorsGlobal;
unsigned long long int totalPhases = 0;
unsigned long long int relaxationsBypassed = 0;
unsigned long long int relaxationsShort = 0;
unsigned long long int relaxationsLong = 0;
double timeAtBarrier = 0;

class VertexOwnershipException : public std::runtime_error
{
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

class Fatal : public std::runtime_error
{
public:
    Fatal(std::string what) : std::runtime_error(what) {}
};

bool anyoneHasWork(const std::vector<size_t> &activeSet)
{
    int local_has_work = 0;
    if (!activeSet.empty())
    {
        local_has_work = 1;
        {
            DEBUGN("I have some work to do!");
        }
    }
    else
    {
        DEBUGN("I don't raport anything to do!");
    }

    int global_has_work = 0;
    MPI_CALL(MPI_Allreduce(&local_has_work, &global_has_work, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD));
    if (global_has_work)
    {
        return true;
    }
    else
    {
        return false;
    }
}

std::vector<size_t> getActiveSet(const std::map<long long, std::vector<size_t>> &buckets, size_t bucketIdx)
{
    std::vector<size_t> active;
    auto it = buckets.find(bucketIdx);
    if (it != buckets.end())
    {
        active = it->second;
    }
    return active;
}

void updateBucketInfo(
    std::map<long long, std::vector<size_t>> &buckets,
    size_t vGlobalIdx,
    long long oldBucket,
    long long newBucket)
{
    if (oldBucket == newBucket)
    {
        return;
    }

    auto newIt = buckets.find(newBucket);
    if (newIt != buckets.end())
    {
        const auto &newVec = newIt->second;
        if (std::find(newVec.begin(), newVec.end(), vGlobalIdx) != newVec.end())
        {
            throw Fatal("Vertex already present in new bucket!");
        }
    }

    if (oldBucket != INF)
    {
        auto oldIt = buckets.find(oldBucket);
        if (oldIt == buckets.end())
        {
            throw Fatal("Old bucket not found!");
        }
        auto &oldVec = oldIt->second;

        auto pos = std::find(oldVec.begin(), oldVec.end(), vGlobalIdx);
        if (pos == oldVec.end())
        {
            throw Fatal("Vertex not found in old bucket!");
        }
        oldVec.erase(pos);
    }

    buckets[newBucket].push_back(vGlobalIdx);
}

void setActiveSet(
    std::map<long long, std::vector<size_t>> &buckets,
    size_t bucketIdx,
    const std::vector<size_t> &activeSet)
{
    if (activeSet.empty())
    {
        buckets.erase(bucketIdx);
    }
    else
    {
        buckets[bucketIdx] = activeSet;
    }
}

void relaxAllEdgesLocalBypass(
    std::vector<size_t> activeSet, // by copy!
    const std::function<bool(size_t, size_t, long long)> &edgeConsidered,
    Data &data,
    const BlockDistribution::Distribution &dist,
    // std::map<long long, std::vector<size_t>> &buckets,
    long long delta_val)
{
    // --- Relaxation Step ---

    std::vector<size_t> newActive;
    // originally it was not a loop, but a single execution.
    // my optimization: if a process owns newly activated vertices, proceed
    while (!activeSet.empty())
    {
        auto currentBucket = activeSet[0] / delta_val;
        newActive.clear();

        for (auto u_global_id : activeSet)
        {
            auto u_dist = data.getDist(u_global_id);
            DEBUGN("Relaxing neighs of vertex:", u_global_id, ". Dist of it:", u_dist);

            if (u_dist == INF)
            {
                ERROR("FATAL");
                throw Fatal("We should have never entered the INF bucket!");
            }

            data.forEachNeighbor(u_global_id, [&](size_t vGlobalIdx, long long w)
            {
                auto potential_new_dist = u_dist + w;

                if (!edgeConsidered(u_global_id, vGlobalIdx, w)) {
                    DEBUGN("Skipping relaxation of", u_global_id, vGlobalIdx, "as is not relevant");
                    return;
                }

                auto ownerProcessOpt = dist.getResponsibleProcessor(vGlobalIdx);
                if (!ownerProcessOpt.has_value()) { throw Fatal("Owner doesn't exist!"); }
                size_t ownerProcess = *ownerProcessOpt;

                auto indexAtOwnerOpt = dist.globalToLocal(vGlobalIdx);
                if (!indexAtOwnerOpt.has_value()) { throw Fatal("Owner doesn't exist!"); }
                MPI_Aint indexAtOwner = static_cast<MPI_Aint>(*indexAtOwnerOpt);

                DEBUGN("Sending update to process: ", ownerProcess, "(displacement:",
                    indexAtOwner, "). New dist of", vGlobalIdx, "=", potential_new_dist);

                // NOTE: this will bypass syncing window to dist afterwards!
                if (ownerProcess == myRank) {
                    auto prevDist = data.getDist(vGlobalIdx);
                    auto oldBucket = prevDist == INF ? INF : prevDist / delta_val;
                    auto newBucket = potential_new_dist / delta_val;
                    DEBUGN("Try short:", vGlobalIdx, prevDist, oldBucket, newBucket, currentBucket, delta_val);
                    if (oldBucket > currentBucket && newBucket == currentBucket) {
                        DEBUGN("Shortcut!", vGlobalIdx);
                        relaxationsBypassed++;
                        newActive.push_back(vGlobalIdx);
                    }
                    
                    // data.selfRelax(potential_new_dist, vGlobalIdx);
                    data.communicateRelax(potential_new_dist, ownerProcess, indexAtOwner);
                } else {
                    data.communicateRelax(potential_new_dist, ownerProcess, indexAtOwner);
                } 
            });
        }
        activeSet = newActive;
    }
}

void relaxAllEdges(
    const std::vector<size_t> &activeSet,
    const std::function<bool(size_t, size_t, long long)> &edgeConsidered,
    Data &data,
    const BlockDistribution::Distribution &dist)
{
    for (auto u_global_id : activeSet)
    {
        auto u_dist = data.getDist(u_global_id);
        DEBUGN("Relaxing neighs of vertex:", u_global_id, ". Dist of it:", u_dist);

        if (u_dist == INF)
        {
            ERROR("FATAL");
            throw Fatal("We should have never entered the INF bucket!");
        }

        data.forEachNeighbor(u_global_id, [&](size_t vGlobalIdx, long long w)
        {
            auto potential_new_dist = u_dist + w;

            if (!edgeConsidered(u_global_id, vGlobalIdx, w)) {
                DEBUGN("Skipping relaxation of", u_global_id, vGlobalIdx, "as is not relevant");
                return;
            }

            auto ownerProcessOpt = dist.getResponsibleProcessor(vGlobalIdx);
            if (!ownerProcessOpt.has_value()) { throw Fatal("Owner doesn't exist!"); }
            size_t ownerProcess = *ownerProcessOpt;

            auto indexAtOwnerOpt = dist.globalToLocal(vGlobalIdx);
            if (!indexAtOwnerOpt.has_value()) { throw Fatal("Owner doesn't exist!"); }
            MPI_Aint indexAtOwner = static_cast<MPI_Aint>(*indexAtOwnerOpt);

            DEBUGN("Sending update to process: ", ownerProcess, "(displacement:",
                indexAtOwner, "). New dist of", vGlobalIdx, "=", potential_new_dist);

            data.communicateRelax(potential_new_dist, ownerProcess, indexAtOwner);
        });
    }
}

void processBucket(
    std::map<long long, std::vector<size_t>> &buckets,
    size_t currentK,
    Data &data,
    const BlockDistribution::Distribution &dist,
    long long delta_val,
    const std::function<bool(size_t, size_t, long long)> &edgeConsidered,
    bool enable_local_bypass)
{
    size_t phaseNo = 0;

    // Determine the active set S for this process (it might be empty, that's OK)
    std::vector<size_t> activeSet = getActiveSet(buckets, currentK);

    while (true)
    {
        // STEP 1: All processes collectively decide if there is any work left for this 'k'.
        // If the global sum is 0, NO process has work for 'currentK'. ALL break the phase loop.
        if (!anyoneHasWork(activeSet))
        {
            DEBUGN("Process", myRank, "no more work for k=", currentK);
            break;
        }
        // We now know that at least one process has work, so ALL processes must participate in the phase.
        totalPhases++;
        phaseNo++;

        {
            DEBUG("Process", myRank, "starting phase", phaseNo, "for k=", currentK);
            DEBUG(". Active vertices: [");
            if (!activeSet.empty() && activeSet.size() < 1000)
            {
                DEBUG(activeSet[0]);
                for (auto it = activeSet.begin() + 1; it != activeSet.end(); ++it)
                {
                    DEBUG(",", *it);
                }
            }
            DEBUGN("]");
        }

        // FENCE 1
        {
            PROGRESSN("FENCE SYNC 1: waiting...");
            data.syncWindowToActual();
            double start = MPI_Wtime();
            data.fence();
            double end = MPI_Wtime();
            timeAtBarrier += end - start;
            DEBUGN("FENCE SYNC 1: done! Performing relaxations...");
        }

        if (enable_local_bypass)
        {
            // relaxAllEdgesLocalBypass(activeSet, edgeConsidered, data, dist, buckets, delta_val);
            relaxAllEdgesLocalBypass(activeSet, edgeConsidered, data, dist, delta_val);
        }
        else
        {
            relaxAllEdges(activeSet, edgeConsidered, data, dist);
        }

        // --- FENCE 2 ---
        {
            // data.communicateRelax(INF, myRank, 0);
            PROGRESSN("FENCE SYNC 2: waiting... epoch:", totalPhases);
            double start = MPI_Wtime();
            data.fence();
            double end = MPI_Wtime();
            timeAtBarrier += end - start;
            DEBUGN("FENCE SYNC 2: done!");
        }

        // we will only preserve updates vertices
        activeSet.clear();
        DEBUGN("activeSet.clear(): done!");
        for (auto update : data.getUpdatesAndSyncDataToWin())
        {
            DEBUGN("updating!");
            auto vGlobalIdx = update.vGlobalIdx;
            auto prevDist = update.prevDist;
            auto newDist = update.newDist;
            DEBUGN("Update registered:", vGlobalIdx, "changed from", prevDist, "to", newDist);

            auto oldBucket = prevDist == INF ? INF : prevDist / delta_val;
            auto newBucket = newDist / delta_val;

            updateBucketInfo(buckets, vGlobalIdx, oldBucket, newBucket);

            if (newBucket == currentK)
            {
                DEBUGN("New active node:", vGlobalIdx);
                activeSet.push_back(vGlobalIdx);
            }
        }
        DEBUGN("updates: done!");
        DEBUG("Finishing phase. Updates processed.");
        DEBUG(" Active vertices: [");
        if (!activeSet.empty())
        {
            DEBUG(activeSet[0]);
            for (auto it = activeSet.begin() + 1; it != activeSet.end(); ++it)
            {
                DEBUG(",", *it);
            }
        }
        DEBUGN("]");
    } // end of while(true) phase loop
}

void delta_stepping_algorithm(
    Data &data,
    const BlockDistribution::Distribution &dist,
    size_t root_rt_global_id,
    long long delta_val,
    int progress_freq,
    bool enable_ios,
    bool enable_pruning,
    bool enable_local_bypass,
    bool enable_hybridization)
{
    (void)enable_pruning;
    (void)enable_hybridization;
    std::map<long long, std::vector<size_t>> buckets;

    DEBUGN("Process", myRank, "processing", data.getNResponsible(), "vertices!");
    if (data.getNResponsible() < 1000)
    {
        for (size_t localVertexId = 0; localVertexId < data.getNResponsible(); ++localVertexId)
        {
            auto owned = data.getFirstResponsibleGlobalIdx() + localVertexId;
            DEBUG("\nVertex:", owned, "neighbours: [");
            auto curNeighs = data.getNeigh()[localVertexId];
            for (size_t i = 0; i < curNeighs.size(); ++i)
            {
                DEBUG(curNeighs[i].first, "(@", curNeighs[i].second, "), ");
            }
            DEBUG("]");
        }
    }
    DEBUGN("");

    if (data.isOwned(root_rt_global_id))
    {
        data.updateDist(root_rt_global_id, 0);
        updateBucketInfo(buckets, root_rt_global_id, INF, 0);
    }

    // Main loop: every iteration is one epoch
    size_t epochNo = 0;
    while (true)
    {
        long long localMinK = INF;
        for (auto it = buckets.begin(); it != buckets.end() && it->second.empty(); it = buckets.begin())
        {
            buckets.erase(it);
        }
        if (!buckets.empty())
        {
            localMinK = buckets.begin()->first;
        }
        long long currentK = INF;
        MPI_CALL(MPI_Allreduce(&localMinK, &currentK, 1, MPI_LONG_LONG, MPI_MIN, MPI_COMM_WORLD));

        if (epochNo % progress_freq == 0)
        {
            PROGRESSN("Process", myRank, "is starting epoch", epochNo);
            if (!buckets.empty())
            {
                if (currentK == INF)
                    PROGRESSN("Bucket considered:", "INF");
                else
                    PROGRESSN(
                        "Bucket considered:", currentK, "(raported my best bucket:",
                        localMinK, "of", buckets.begin()->second.size(), "nodes");
            }
            else
            {
                if (currentK == INF)
                    PROGRESSN("Bucket considered:", "INF");
                else
                    PROGRESSN("Bucket considered:", currentK, "(raported no bucket)");
            }
        }
        epochNo++;

        if (currentK == INF)
        {
            DEBUGN("Termination condition met. Exiting.");
            break;
        }

        auto isInnerShort = [&data, delta_val, currentK](size_t uGlobalIdx, [[maybe_unused]] size_t vGlobalIdx, long long weight) -> bool
        {
            auto u_dist = data.getDist(uGlobalIdx);
            auto potential_new_dist = u_dist + weight;
            return weight < delta_val && potential_new_dist <= (currentK + 1) * delta_val - 1;
        };

        if (!enable_ios)
        {
            processBucket(buckets, currentK, data, dist, delta_val, [&isInnerShort](size_t uGlobalIdx, size_t vGlobalIdx, long long weight) -> bool
                          {
                            // here we assume the relaxation will always be made
                            if (isInnerShort(uGlobalIdx, vGlobalIdx, weight)) {
                                relaxationsShort++;
                            } else {
                                relaxationsLong++;
                            }
                            return true; }, enable_local_bypass);
        }
        else
        {
            // SHORT PHASE; this will execute many iterations of the internal loop
            processBucket(buckets, currentK, data, dist, delta_val, [&isInnerShort](size_t uGlobalIdx, size_t vGlobalIdx, long long weight) -> bool
                          {
                            // here we assume the relaxation will always be made
                            if (isInnerShort(uGlobalIdx, vGlobalIdx, weight)) {
                                relaxationsShort++;
                                return true;
                            }
                            return false; }, enable_local_bypass);
            // LONG PHASE; this will be just a single iteration
            processBucket(buckets, currentK, data, dist, delta_val, [&isInnerShort](size_t uGlobalIdx, size_t vGlobalIdx, long long weight) -> bool
                          {
                            // here we assume the relaxation will always be made
                            if (isInnerShort(uGlobalIdx, vGlobalIdx, weight)) {
                                return false;
                            }
                            relaxationsLong++;
                            return true; }, enable_local_bypass);
        }
        setActiveSet(buckets, currentK, {});
    } // end of while(true) epoch loop
    return;
}

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);
    MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN);

    // MPI_Win dist_window = MPI_WIN_NULL; // MPI Window for one-sided access to distances
    MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
    MPI_Comm_size(MPI_COMM_WORLD, &nProcessorsGlobal);

    DebugLogger::getInstance().init("debug_log_" + std::to_string(myRank) + ".txt");

    // assumption: argc is the same among processors
    if (argc < 3)
    {
        if (myRank == 0)
        {
            std::cerr << "\nUsage:\n";
            std::cerr << "  " << argv[0] << " <input_file> <output_file> [delta > 0] [options]\n\n";
            std::cerr << "Required arguments:\n";
            std::cerr << "  <input_file>             Path to input graph data\n";
            std::cerr << "  <output_file>            Path where results will be written\n";
            std::cerr << "  [delta > 0]              (Optional) Delta-stepping bucket width (default: " << DEFAULT_DELTA << ")\n\n";

            std::cerr << "Optional flags:\n";
            std::cerr << "  --ios / --noios          Enable or disable IOS optimizations (default: enabled)\n";
            std::cerr << "  --pruning / --nopruning  Enable or disable pruning optimization (default: enabled)\n";
            std::cerr << "  --local-bypass / --nolocal-bypass  Enable or disable dynamically adding just relaxed nodes to active set inside one processor (default: enabled)\n";
            std::cerr << "  --hybrid / --nohybrid    Enable or disable hybridization optimization (default: enabled)\n";
            std::cerr << "  --assume-nomultiedge     Skip removing multi-edges from the input graph (default: disabled)\n";
            std::cerr << "  --logging <level>        Set logging level: none | progress | debug (default: progress)\n";
            std::cerr << "  --progress-freq <int>    Report progress once every N epochs (default: 10)\n";
            std::cerr << std::endl;
        }
        MPI_Finalize();
        return 1;
    }
    std::string input_filename = argv[1];
    std::string output_filename = argv[2];
    long long delta_param = (argc > 3) ? std::stoll(argv[3]) : DEFAULT_DELTA;

    // assumption: delta CLI arg is hardcoded in mpirun script, so it will be the same everywhere
    if (delta_param <= 0)
    {
        if (myRank == 0)
            ERROR("Delta param must be > 0");
        MPI_Finalize();
        return 1;
    }

    // === New flags ===
    bool enable_ios_optimizations = true;
    bool enable_pruning = true;
    bool enable_local_bypass = true;
    bool enable_hybridization = true;
    bool assume_nomultiedge = false;

    int progress_freq = DEFAULT_PROGESS_FREQ;

    for (int i = 4; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--ios")
        {
            enable_ios_optimizations = true;
        }
        else if (arg == "--noios")
        {
            enable_ios_optimizations = false;
        }
        else if (arg == "--pruning")
        {
            enable_pruning = true;
        }
        else if (arg == "--nopruning")
        {
            enable_pruning = false;
        }
        else if (arg == "--local-bypass")
        {
            enable_local_bypass = true;
        }
        else if (arg == "--nolocal-bypass")
        {
            enable_local_bypass = false;
        }
        else if (arg == "--hybrid")
        {
            enable_hybridization = true;
        }
        else if (arg == "--nohybrid")
        {
            enable_hybridization = false;
        }
        else if (arg == "--assume-nomultiedge")
        {
            assume_nomultiedge = true;
        }
        else if (arg == "--logging")
        {
            if (i + 1 >= argc)
            {
                if (myRank == 0)
                    std::cerr << "--logging requires an argument: none, progress, or debug" << std::endl;
                MPI_Finalize();
                return 1;
            }
            std::string level = argv[++i];
            if (level == "none")
                logging_level = LoggingLevel::None;
            else if (level == "progress")
                logging_level = LoggingLevel::Progress;
            else if (level == "debug")
                logging_level = LoggingLevel::Debug;
            else
            {
                if (myRank == 0)
                    std::cerr << "Invalid value for --logging: " << level << std::endl;
                MPI_Finalize();
                return 1;
            }
        }
        else if (arg == "--progress-freq")
        {
            if (i + 1 >= argc)
            {
                if (myRank == 0)
                    std::cerr << "--progress-freq requires an integer argument" << std::endl;
                MPI_Finalize();
                return 1;
            }
            try
            {
                progress_freq = std::stoi(argv[++i]);
                if (progress_freq <= 0)
                    throw std::invalid_argument("must be > 0");
            }
            catch (const std::exception &e)
            {
                if (myRank == 0)
                    std::cerr << "Invalid value for --progress-freq: " << e.what() << std::endl;
                MPI_Finalize();
                return 1;
            }
        }
        else
        {
            if (myRank == 0)
                std::cerr << "Unknown argument: " << arg << std::endl;
            MPI_Finalize();
            return 1;
        }
    }

    PROGRESSN("Starting to parse data!");
    PROGRESSN("Log level: >= progress");
    DEBUGN("Log level: >= debug");
    std::cout.setf(std::ios::unitbuf); // auto-flush std::cout
    std::cerr.setf(std::ios::unitbuf); // auto-flush std::cerr
    if (myRank == 0) std::cerr << "std::cerr test";
    if (myRank == 0) ERROR("(this is a test of error log displaying)");

    double start_time1 = MPI_Wtime();
    auto dataOpt = process_input_and_load_graph_from_stream(myRank, input_filename, assume_nomultiedge);
    double end_time1 = MPI_Wtime();
    if (myRank == 0)
        std::cout << "Parsing data took: " << end_time1 - start_time1 << "s\n";

    if (!dataOpt.has_value())
    {
        ERROR("Unable to parse data!");
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    auto &data = *dataOpt;

    BlockDistribution::Distribution dist(nProcessorsGlobal, data.getNVerticesGlobal());
    auto distNRespOpt = dist.getNResponsibleVertices(myRank);
    if (!distNRespOpt.has_value())
    {
        ERROR("Unable to take nResp from distribution");
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    auto distNResp = *distNRespOpt;
    if (distNResp != data.getNResponsible())
    {
        ERROR("Rank", myRank,
              ": mismatch in number of vertices owned by process: ",
              distNResp, "!=", data.getNResponsible());
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    auto respProcFstOpt = dist.getResponsibleProcessor(data.getFirstResponsibleGlobalIdx());
    auto respProcLstOpt = dist.getResponsibleProcessor(data.lastResponsibleGlobalIdx());
    if (!respProcFstOpt.has_value() || !respProcLstOpt.has_value())
    {
        ERROR("Unable to obtain processor of first/last from distribution");
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    auto respProcFst = *respProcFstOpt;
    auto respProcLst = *respProcLstOpt;
    if (respProcFst != myRank || respProcLst != myRank)
    {
        ERROR("Rank", myRank, ": mismatch in owner of vertices: ",
              respProcFst, "or", respProcLst, "!=", myRank);
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    std::ofstream outfile_stream(output_filename);
    if (!outfile_stream.is_open())
    {
        std::cerr << "Rank " << myRank << ": Cannot open " << output_filename << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    DEBUGN("Starting delta stepping!");
    double start_time = MPI_Wtime();
    try
    {
        delta_stepping_algorithm(data, dist, 0, delta_param, progress_freq,
                                 enable_ios_optimizations, enable_pruning, enable_local_bypass,
                                 enable_hybridization);
    }
    catch (Fatal &ex)
    {
        ERROR("Fatal error while Delta-stepping: ", ex.what());
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    MPI_Barrier(MPI_COMM_WORLD); // Ensure all processes done before anyone exits/prints final time
    double end_time = MPI_Wtime();

    long long globalRelaxationsShort = 0;
    long long globalRelaxationsLong = 0;
    long long globalRelaxationsBypassed = 0;
    long long globalTotalPhases = 0;

    // Reduce (sum) the counters across all processes
    MPI_CALL(MPI_Reduce(&relaxationsShort, &globalRelaxationsShort, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD));
    MPI_CALL(MPI_Reduce(&relaxationsLong, &globalRelaxationsLong, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD));
    MPI_CALL(MPI_Reduce(&relaxationsBypassed, &globalRelaxationsBypassed, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD));
    MPI_CALL(MPI_Reduce(&totalPhases, &globalTotalPhases, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD));

    if (myRank == 0)
    {
        std::cout << "Delta-stepping (one-sided) finished.\n";
        std::cout << "Time: " << (end_time - start_time) << "s." << std::endl;
        std::cout << "Short relaxations: " << globalRelaxationsShort << std::endl;
        std::cout << "  from which bypassed: " << globalRelaxationsBypassed << std::endl;
        std::cout << "Long relaxations: " << globalRelaxationsLong << std::endl;
        std::cout << "Total phases: " << globalTotalPhases << std::endl;
    }

    for (size_t i = 0; i < data.getNResponsible(); ++i)
    {
        outfile_stream << (data.data()[i] == INF ? -1 : data.data()[i]) << std::endl;
    }
    outfile_stream.close();

    data.freeWindow();
    MPI_Finalize();
    return 0;
}