#pragma once

#include <cstddef>
#include <vector>
#include <string>
#include <optional>
#include <mpi.h>
#include <sstream>
#include <stdexcept>
#include <cstring>    // std::memcpy
#include <functional> // std::function
#include <limits>
#include <iostream>

#include "logger.hpp"


const long long INF = std::numeric_limits<long long>::max();

class InvalidData : public std::runtime_error
{
public:
    InvalidData(const std::string &what) : std::runtime_error(what) {}
};

class Data
{
    size_t firstResponsibleGlobalIdx;
    size_t nLocalResponsible;
    size_t nVerticesGlobal;

    /// @brief adj[local_idx] -> {global_neighbor_idx, weight}
    std::vector<std::vector<std::pair<size_t, long long>>> neighOfLocal;

    /// @brief Convert a global id of a vertex to index of the corresponding field in the local `neighOfLocal` vector
    /// @throws VertexOwnershipException if vertex is not owned
    std::optional<size_t> globalToLocalIdx(size_t vGlobalIdx) const
    {
        if (!isOwned(vGlobalIdx))
        {
            return {};
        }

        return vGlobalIdx - firstResponsibleGlobalIdx;
    }

    // distToRoot[local_idx] for MPI_Win communication
    std::vector<long long> distToRoot;

    void *winMemory;
    MPI_Win window;
    int winDisp;
    MPI_Aint winSize;

public:
    struct Update
    {
        size_t vGlobalIdx;
        long long prevDist;
        long long newDist;
    };
    std::vector<Update> selfUpdates;

    Data(size_t firstResponsibleGlobalIdx_, size_t nLocalResponsible_, size_t nVerticesGlobal_)
        : firstResponsibleGlobalIdx(firstResponsibleGlobalIdx_),
          nLocalResponsible(nLocalResponsible_),
          nVerticesGlobal(nVerticesGlobal_),
          neighOfLocal(nLocalResponsible_, std::vector<std::pair<size_t, long long>>()),
          distToRoot(nLocalResponsible_, INF),
          winMemory(nullptr),
          window(MPI_WIN_NULL),
          winDisp(sizeof(long long)),
          winSize(nLocalResponsible_ * sizeof(long long)),
          selfUpdates()
    {
        if (nVerticesGlobal == 0 || lastResponsibleGlobalIdx() < firstResponsibleGlobalIdx || lastResponsibleGlobalIdx() >= nVerticesGlobal || distToRoot.size() != neighOfLocal.size() || distToRoot[0] != INF)
        {
            throw InvalidData(
                std::string("Input data invalid! ") + std::to_string(firstResponsibleGlobalIdx) + " " + std::to_string(nLocalResponsible) + " " + std::to_string(nVerticesGlobal) + " " + std::to_string(lastResponsibleGlobalIdx()) + " " + std::to_string(distToRoot.size()) + " " + std::to_string(neighOfLocal.size()) + " " + std::to_string(distToRoot[0]));
        }

        int mpi_err = MPI_Win_allocate(
            winSize, winDisp,
            MPI_INFO_NULL, MPI_COMM_WORLD, &winMemory, &window);

        if (mpi_err != MPI_SUCCESS || window == MPI_WIN_NULL)
        {
            throw InvalidData("MPI_Win_allocate failed!");
        }
    }

    void freeWindow()
    {
        MPI_Win_free(&window);
    }

    // delete copy constructor and assignment
    Data(const Data &) = delete;
    Data &operator=(const Data &) = delete;
    Data &operator=(Data &&) = delete;

    // allow move constructor
    Data(Data &&other) noexcept
        : firstResponsibleGlobalIdx(other.firstResponsibleGlobalIdx),
          nLocalResponsible(other.nLocalResponsible),
          nVerticesGlobal(other.nVerticesGlobal),
          neighOfLocal(std::move(other.neighOfLocal)),
          distToRoot(std::move(other.distToRoot)),
          winMemory(other.winMemory),
          window(other.window),
          winDisp(other.winDisp),
          winSize(other.winSize),
          selfUpdates(std::move(selfUpdates))
    {
        other.window = MPI_WIN_NULL;
        other.winMemory = nullptr;
    }

    const std::vector<std::vector<std::pair<size_t, long long>>> &getNeigh() const
    {
        return neighOfLocal;
    }

    void syncWindowToActual()
    {
        std::memcpy(winMemory, distToRoot.data(), winSize);
    }

    void fence_start()
    {
        // MPI_Win_flush_all(window);
        MPI_CALL(MPI_Win_fence(0, window));
    }

    void fence()
    {
        // MPI_Win_flush_all(window);
        MPI_CALL(MPI_Win_fence(0, window));
    }

    void communicateRelax(long long newDistance, int ownerProcess, int ownerIndex)
    {
        MPI_CALL(MPI_Accumulate(
            &newDistance, 1, MPI_LONG_LONG,
            ownerProcess, ownerIndex, 1, MPI_LONG_LONG,
            MPI_MIN, window));
    }

    void selfRelax(long long potential_new_dist, size_t vGlobalIdx) {
        Update upd;
        upd.vGlobalIdx = vGlobalIdx;
        upd.newDist = potential_new_dist;
        upd.prevDist = getDist(vGlobalIdx);
        selfUpdates.emplace_back(upd);
    }

    std::vector<Update> getUpdatesAndSyncDataToWin()
    {
        std::vector<Update> updates = selfUpdates;
        selfUpdates.clear();
        for (size_t i = 0; i < nLocalResponsible; ++i)
        {
            auto new_dist = static_cast<long long *>(winMemory)[i];
            if (new_dist > distToRoot[i])
            {
                // throw InvalidData("MPI distance relax caused dist to increase!");
                continue;
            }
            else if (new_dist < distToRoot[i])
            {
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

    long long *data()
    {
        return distToRoot.data();
    }

    std::vector<long long> getCopyOfDistances() const
    {
        return distToRoot;
    }

    long long getDist(size_t vGlobalIdx) const
    {
        auto locOpt = globalToLocalIdx(vGlobalIdx);
        if (!locOpt.has_value())
        {
            throw InvalidData("Vertex not owned!");
        }
        return distToRoot[*locOpt];
    }

    void forEachNeighbor(size_t vGlobalIdx, const std::function<void(size_t, long long)> &visitor) const
    {
        auto locOpt = globalToLocalIdx(vGlobalIdx);
        if (!locOpt.has_value())
        {
            throw InvalidData("Vertex not owned!");
        }
        for (const auto &edge : neighOfLocal[*locOpt])
        {
            visitor(edge.first, edge.second);
        }
    }

    size_t getNResponsible() const
    {
        return nLocalResponsible;
    }

    size_t getNVerticesGlobal() const
    {
        return nVerticesGlobal;
    }

    size_t getFirstResponsibleGlobalIdx() const
    {
        return firstResponsibleGlobalIdx;
    }

    size_t lastResponsibleGlobalIdx() const
    {
        if (firstResponsibleGlobalIdx + nLocalResponsible == 0)
        {
            throw InvalidData("This should never be zero!");
        }
        return firstResponsibleGlobalIdx + nLocalResponsible - 1;
    }

    void updateDist(size_t vGlobalIdx, long long dist)
    {
        auto locOpt = globalToLocalIdx(vGlobalIdx);
        if (!locOpt.has_value())
        {
            throw InvalidData("Vertex not owned!");
        }
        distToRoot[*locOpt] = dist;
    }

    /// @brief Add new edge to stored data if responsible for any of the end vertices. Ignore if not owned!
    /// @throws InvalidData
    void addEdgeFast(size_t u, size_t v, size_t weight)
    {
        if (u == v)
        {
            return;
        }
        if (u >= nVerticesGlobal || v >= nVerticesGlobal)
        {
            throw InvalidData(
                std::string("Invalid edge data!") + std::to_string(u) + " " + std::to_string(v) + " " + std::to_string(weight));
        }

        if (!isOwned(u) && !isOwned(v))
        {
            throw InvalidData("Neither of edge ends owned!");
        }
        if (isOwned(u))
        {
            auto &neighbors = neighOfLocal[*globalToLocalIdx(u)];
            // auto it = std::find_if(neighbors.begin(), neighbors.end(),
            //                        [v](const std::pair<uint64_t, uint8_t> &p)
            //                        { return p.first == v; });

            // if (it == neighbors.end())
            // {
            neighbors.push_back({v, weight});
            // }
            // else if (weight < it->second)
            // {
            //     it->second = weight;
            // }
        }
        if (isOwned(v))
        {
            auto &neighbors = neighOfLocal[*globalToLocalIdx(v)];
            // auto it = std::find_if(neighbors.begin(), neighbors.end(),
            //                        [u](const std::pair<uint64_t, uint8_t> &p)
            //                        { return p.first == u; });

            // if (it == neighbors.end())
            // {
            neighbors.push_back({u, weight});
            // }
            // else if (weight < it->second)
            // {
            // it->second = weight;
            // }
        }
    }

    void trimMultiEdges()
    {
        for (auto &neighbors : neighOfLocal)
        {
            std::unordered_map<size_t, long long> deduped;

            for (const auto &[target, weight] : neighbors)
            {
                auto it = deduped.find(target);
                if (it == deduped.end() || weight < it->second)
                {
                    deduped[target] = weight;
                }
            }

            neighbors.clear();
            neighbors.reserve(deduped.size());

            for (const auto &[target, weight] : deduped)
            {
                neighbors.emplace_back(target, weight);
            }
        }
    }

    bool isOwned(size_t vGlobalIdx) const
    {
        return vGlobalIdx >= firstResponsibleGlobalIdx && vGlobalIdx <= lastResponsibleGlobalIdx();
    }
};

std::optional<Data> process_input_and_load_graph_from_stream(
    int myRank,
    const std::string &input_filename,
    bool assume_nomultiedge);