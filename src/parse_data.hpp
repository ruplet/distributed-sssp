#pragma once

#include <cstddef>
#include <vector>
#include <string>
#include <optional>
#include <stdexcept>
#include <functional>
#include <limits>
#include <iostream>

const long long INF = std::numeric_limits<long long>::max();

class InvalidData : public std::runtime_error {
public:
    InvalidData(const std::string& what) : std::runtime_error(what) {}
};

class Data {
    size_t firstResponsibleGlobalIdx;
    size_t nLocalResponsible;
    size_t nVerticesGlobal;

    /// @brief adj[local_idx] -> {global_neighbor_idx, weight}
    std::vector<std::vector<std::pair<size_t, long long>>> neighOfLocal;

    /// @brief Convert a global id of a vertex to index of the corresponding field in the local `neighOfLocal` vector
    /// @throws VertexOwnershipException if vertex is not owned
    std::optional<size_t> globalToLocalIdx(size_t vGlobalIdx) const {
        if (!isOwned(vGlobalIdx)) {
            return {};
        }

        return vGlobalIdx - firstResponsibleGlobalIdx;
    }

    // distToRoot[local_idx] for MPI_Win communication
    std::vector<long long> distToRoot;
    std::vector<char> dirtyFlags;

public:
    Data(size_t firstResponsibleGlobalIdx, size_t nLocalResponsible, size_t nVerticesGlobal) 
        :
            firstResponsibleGlobalIdx(firstResponsibleGlobalIdx),
            nLocalResponsible(nLocalResponsible),
            nVerticesGlobal(nVerticesGlobal),
            neighOfLocal(nLocalResponsible, std::vector<std::pair<size_t, long long>>()),
            distToRoot(nLocalResponsible, INF),
            dirtyFlags(nLocalResponsible, 0)
    {
        if (
            nVerticesGlobal <= 0
            || firstResponsibleGlobalIdx < 0
            || lastResponsibleGlobalIdx() < 0
            || lastResponsibleGlobalIdx() < firstResponsibleGlobalIdx
            || lastResponsibleGlobalIdx() >= nVerticesGlobal
            || distToRoot.size() != neighOfLocal.size()
            || distToRoot[0] != INF
        ) {
            throw InvalidData(
                std::string("Input data invalid! ")
                + std::to_string(firstResponsibleGlobalIdx) + " "
                + std::to_string(nLocalResponsible) + " "
                + std::to_string(nVerticesGlobal) + " "
                + std::to_string(lastResponsibleGlobalIdx()) + " "
                + std::to_string(distToRoot.size()) + " "
                + std::to_string(neighOfLocal.size()) + " "
                + std::to_string(distToRoot[0])
            );
        }
    }

    long long* data() {
        return distToRoot.data();
    }

    char* getDirtyFlagsBuffer() {
        return dirtyFlags.data();
    }

    long long getDist(size_t vGlobalIdx) const {
        if (!isOwned(vGlobalIdx)) {
            throw InvalidData("Vertex not owned!");
        }
        return distToRoot[*globalToLocalIdx(vGlobalIdx)];
    }

    void forEachNeighbor(size_t vGlobalIdx, const std::function<void(size_t, long long)>& visitor) const {
        if (!isOwned(vGlobalIdx)) {
            throw InvalidData("Vertex not owned!");
        }
        for (const auto& edge : neighOfLocal[*globalToLocalIdx(vGlobalIdx)]) {
            visitor(edge.first, edge.second);
        }
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

    void updateDist(size_t vGlobalIdx, long long dist) {
        if (!isOwned(vGlobalIdx)) {
            throw InvalidData("Vertex not owned!");
        }
        distToRoot[*globalToLocalIdx(vGlobalIdx)] = dist;
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
            neighOfLocal[*globalToLocalIdx(u)].push_back({v, weight});
        } else if (isOwned(v)) {
            neighOfLocal[*globalToLocalIdx(v)].push_back({u, weight});
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