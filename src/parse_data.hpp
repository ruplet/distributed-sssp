#pragma once

#include <cstddef>
#include <vector>
#include <string>
#include <optional>
#include <mpi.h>
#include <stdexcept>
#include <cstring> // std::memcpy
#include <functional> // std::function
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

    void *winMemory;
    MPI_Win window;
    int winDisp;
    MPI_Aint winSize;

public:
    Data(size_t firstResponsibleGlobalIdx, size_t nLocalResponsible, size_t nVerticesGlobal) 
        :
            firstResponsibleGlobalIdx(firstResponsibleGlobalIdx),
            nLocalResponsible(nLocalResponsible),
            nVerticesGlobal(nVerticesGlobal),
            neighOfLocal(nLocalResponsible, std::vector<std::pair<size_t, long long>>()),
            distToRoot(nLocalResponsible, INF),
            dirtyFlags(nLocalResponsible, 0),
            winMemory(nullptr),
            window(MPI_WIN_NULL),
            winDisp(sizeof(long long)),
            winSize(nLocalResponsible * sizeof(long long))
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

        int mpi_err = MPI_Win_allocate(
            winSize, winDisp,
            MPI_INFO_NULL, MPI_COMM_WORLD, &winMemory, &window
        );

        if (mpi_err != MPI_SUCCESS || window == MPI_WIN_NULL) {
            throw InvalidData("MPI_Win_allocate failed!");
        }
    }

    ~Data() {
        // winMemory will be freed automatically with MPI_Win_free
        if (window != MPI_WIN_NULL) {
            MPI_Win_free(&window);
        }
    }

    // delete copy constructor and assignment
    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;
    Data& operator=(Data&&) = delete;

    // allow move constructor
    Data(Data&& other) noexcept
        :
            firstResponsibleGlobalIdx(other.firstResponsibleGlobalIdx),
            nLocalResponsible(other.nLocalResponsible),
            nVerticesGlobal(other.nVerticesGlobal),
            neighOfLocal(std::move(other.neighOfLocal)),
            distToRoot(std::move(other.distToRoot)),
            dirtyFlags(std::move(other.dirtyFlags)),
            winMemory(other.winMemory),
            window(other.window),
            winDisp(other.winDisp),
            winSize(other.winSize)
    {
        other.window = MPI_WIN_NULL;
        other.winMemory = nullptr;
    }

    void syncWindowToActual() {
        std::memcpy(winMemory, distToRoot.data(), winSize);
    }

    void fence() {
        MPI_Win_fence(0, window);
    }

    void communicateRelax(size_t vGlobalIdx, long long newDistance, int ownerProcess, MPI_Aint ownerDisp) {
        MPI_Accumulate(&newDistance, 1, MPI_LONG_LONG, ownerProcess,
                        ownerDisp, 1, MPI_LONG_LONG, MPI_MIN, window);
    }

    struct Update {
        size_t vGlobalIdx;
        long long prevDist;
        long long newDist;
    };

    std::vector<Update> getUpdatesAndSyncDataToWin() {
        std::vector<Update> updates;
        for (size_t i = 0; i < nLocalResponsible; ++i) {
            auto new_dist = static_cast<long long *>(winMemory)[i];
            if (new_dist > distToRoot[i]) {
                throw InvalidData("MPI distance relax caused dist to increase!");
            } else if (new_dist < distToRoot[i]) {
                Update update;
                update.vGlobalIdx = getFirstResponsibleGlobalIdx() + i;
                update.prevDist = distToRoot[i];
                update.newDist = new_dist;
                updates.push_back(update);
                distToRoot[i] = new_dist;
            }
        }
        // std::memcpy(distToRoot.data(), winMemory, winSize);
        return updates;
    }

    long long* data() {
        return distToRoot.data();
    }

    char* getDirtyFlagsBuffer() {
        return dirtyFlags.data();
    }

    std::vector<long long> getCopyOfDistances() const {
        return distToRoot;
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