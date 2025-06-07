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

const long long DEFAULT_DELTA = 10;

// --- Configuration Flags for Heuristics (all disabled by default) ---
const bool ENABLE_IOS_HEURISTIC = false;
const bool ENABLE_PRUNING_HEURISTIC = false;
// const bool ENABLE_HYBRIDIZATION = false; // Not detailed enough in prompt to stub
// const bool ENABLE_LOAD_BALANCING = false; // Not detailed enough in prompt to stub

const bool SKIP_COMPUTATIONS_FOR_NOW = false; // Set to false to run algorithm

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


// dist[local_idx] for MPI_Win communication
std::vector<long long> dist_local_responsible;

const long long INF = std::numeric_limits<long long>::max();

void delta_stepping_algorithm(const Data& data, size_t root_rt_global_id, long long delta_val) {
    std::cerr << "Melduję się! proces: " << myRank << " posiadam wierzchołków: " << data.getNResponsible() << std::endl;
    return;
}


int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);

    // MPI_Win dist_window = MPI_WIN_NULL; // MPI Window for one-sided access to distances
    MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
    MPI_Comm_size(MPI_COMM_WORLD, &nProcessorsGlobal);

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

    // Ensure sized for MPI_Win_create and algo
    dist_local_responsible.resize(data.getNResponsible());

    std::ofstream outfile_stream(output_filename);
    if (!outfile_stream.is_open()) {
        std::cerr << "Rank " << myRank << ": Cannot open " << output_filename << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();
    delta_stepping_algorithm(data, 0, delta_param);
    MPI_Barrier(MPI_COMM_WORLD); // Ensure all processes done before anyone exits/prints final time
    double end_time = MPI_Wtime();
    if (myRank == 0) {
        std::cout << "Delta-stepping (one-sided) finished. Time: " << (end_time - start_time) << "s." << std::endl;
    }

    for (size_t i = 0; i < data.getNResponsible(); ++i) {
        outfile_stream << (dist_local_responsible[i] == INF ? -1 : dist_local_responsible[i]) << std::endl;
    }
    outfile_stream.close();

    MPI_Finalize();
    return 0;
}