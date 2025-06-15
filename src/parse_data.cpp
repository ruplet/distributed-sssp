#include <fstream>
#include <sstream>
#include <optional>
#include "parse_data.hpp"

std::optional<Data> process_input_and_load_graph_from_stream(
    int myRank,
    const std::string& input_filename
) {
    std::ifstream instream(input_filename);
    if (!instream.is_open()) {
        std::cerr << "Rank " << myRank << ": Cannot open " << input_filename << std::endl;
        return {};
    }

    size_t nVerticesGlobal;
    size_t firstResponsibleGlobalIdx;
    size_t lastResponsibleGlobalIdx;

    std::string line;
    if (!std::getline(instream, line)) {
        std::cerr << "Rank " << myRank << ": Fail read L1" << std::endl;
        return {};
    }

    std::istringstream iss_first_line(line);
    if (!(iss_first_line
            >> nVerticesGlobal
            >> firstResponsibleGlobalIdx
            >> lastResponsibleGlobalIdx
        )
    ) {
        std::cerr << "Rank " << myRank << ": Fail parse L1" << std::endl;
        return {};
    }

    if (lastResponsibleGlobalIdx  < firstResponsibleGlobalIdx) {
        std::cerr << "Rank " << myRank << ": lastResponsible < firstResponsible" << std::endl;
        return {};
    }
    size_t nLocalResponsible = lastResponsibleGlobalIdx - firstResponsibleGlobalIdx + 1;

    try {
        Data data(firstResponsibleGlobalIdx, nLocalResponsible, nVerticesGlobal);

        // the graph considered is assumed to be undirected!
        long long u, v, weight;
        while (std::getline(instream, line)) {
            if (line.empty()) continue;
            std::istringstream iss_edge(line);
            if (!(iss_edge >> u >> v >> weight)) {
                std::cerr << "Rank " << myRank << ": Fail parse edge" << std::endl;
                return {};
            }
            if (u < 0 || v < 0 || weight < 0) {
                std::cerr << "Rank " << myRank << ": Fail parse edge" << std::endl;
                return {};
            }
            data.addEdge(u, v, weight);
        }

        return std::move(data);
    } catch (InvalidData& ex) {
        std::cerr << "Failed to parse infile: " << ex.what() << std::endl;
        return {};
    }
}