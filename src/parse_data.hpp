#pragma once

#include <cstddef>
#include <vector>
#include <string>
#include <optional>
#include <stdexcept>
#include <iostream>

class InvalidData : public std::runtime_error {
public:
    InvalidData(const std::string& what) : std::runtime_error(what) {}
};

class Data {
    size_t firstResponsibleGlobalIdx;
    size_t nLocalResponsible;
    size_t nVerticesGlobal;

    /// @brief adj[local_idx] -> {global_neighbor_idx, weight}
    std::vector<std::vector<std::pair<int, int>>> adj_local_responsible;

    /// @brief Convert a global id of a vertex to index of the corresponding field in the local `adj_local_responsible` vector
    /// @throws VertexOwnershipException if vertex is not owned
    std::optional<size_t> globalToLocalIdx(size_t vGlobalIdx) const {
        if (!isOwned(vGlobalIdx)) {
            return {};
        }

        return vGlobalIdx - firstResponsibleGlobalIdx;
    }

public:
    Data(size_t firstResponsibleGlobalIdx, size_t nLocalResponsible, size_t nVerticesGlobal) 
        :
            firstResponsibleGlobalIdx(firstResponsibleGlobalIdx),
            nLocalResponsible(nLocalResponsible),
            nVerticesGlobal(nVerticesGlobal)
    {
        if (
            nVerticesGlobal <= 0
            || firstResponsibleGlobalIdx < 0
            || lastResponsibleGlobalIdx() < 0
            || lastResponsibleGlobalIdx() < firstResponsibleGlobalIdx
            || lastResponsibleGlobalIdx() >= nVerticesGlobal
        ) {
            throw InvalidData(
                std::string("Input data invalid! ")
                + std::to_string(firstResponsibleGlobalIdx) + " "
                + std::to_string(nLocalResponsible) + " "
                + std::to_string(nVerticesGlobal) + " "
                + std::to_string(lastResponsibleGlobalIdx())
            );
        }

        adj_local_responsible.assign(nLocalResponsible, std::vector<std::pair<int, int>>());
    }

    size_t getNResponsible() const {
        return nLocalResponsible;
    }

    size_t getNVerticesGlobal() const {
        return nVerticesGlobal;
    }

    size_t getFirstResponsibleGlobalIdx() const {
        return firstResponsibleGlobalIdx;
    }

    size_t lastResponsibleGlobalIdx() const {
        return firstResponsibleGlobalIdx + nLocalResponsible - 1;
    }

    /// @brief Add new edge to stored data if responsible for any of the end vertices. Ignore if not owned!
    /// @throws InvalidData
    void addEdge(size_t u, size_t v, size_t weight){
        if (
            u < 0
            || u >= nVerticesGlobal
            || v < 0
            || v >= nVerticesGlobal
            || weight < 0
        ) {
            throw InvalidData(
                std::string("Invalid edge data!") + std::to_string(u) + " " 
                + std::to_string(v) + " " + std::to_string(weight)
            );
        }
        
        if (isOwned(u)) {
            adj_local_responsible[*globalToLocalIdx(u)].push_back({v, weight});
        } else if (isOwned(v)) {
            adj_local_responsible[*globalToLocalIdx(v)].push_back({u, weight});
        } else {
            std::cerr << "WARNING: " << ": Ignoring not owned edge: " << u << " " << v << " " << weight << std::endl;
        }
    }

    bool isOwned(size_t vGlobalIdx) const {
        return vGlobalIdx >= firstResponsibleGlobalIdx && vGlobalIdx <= lastResponsibleGlobalIdx();
    }
};

std::optional<Data> process_input_and_load_graph_from_stream(
    int myRank,
    const std::string& input_filename
);