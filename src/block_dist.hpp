#pragma once

#include <optional>
#include <cstddef>
#include "common.hpp"

namespace BlockDistribution {

/// @brief Reason about block distribution of the data. Processors of rank `0` to `extra() - 1` get `baseLoad() + 1` vertices.
/// Processors of rank `extra()` to `nProcessorsGlobal - 1` get `baseLoad()` vertices.
class Distribution {
    size_t nProcessorsGlobal;
    size_t nVerticesGlobal;

public:
    /// @throws `InvalidDistribution` if distributing nonzero work among zero processors
    Distribution(size_t nProcessorsGlobal, size_t nVerticesGlobal) :
        nProcessorsGlobal(nProcessorsGlobal),
        nVerticesGlobal(nVerticesGlobal)
        {
            if (nVerticesGlobal > 0 && nProcessorsGlobal == 0) {
                throw InvalidDistribution();
            }

            if (baseLoad() * nProcessorsGlobal + extra() != nVerticesGlobal) {
                throw InvalidDistribution();
            }
        }
    
    class InvalidDistribution : public std::runtime_error {
    public:
        InvalidDistribution() : std::runtime_error("Cannot distribute nonzero work among zero processors") {}
    };

    size_t baseLoad() const {
        if (nProcessorsGlobal == 0) { return 0; }
        return nVerticesGlobal / nProcessorsGlobal;
    }
    size_t extra() const {
        if (nProcessorsGlobal == 0) { return 0; }
        return nVerticesGlobal % nProcessorsGlobal;
    }
    
    std::optional<size_t> getNResponsibleVertices(size_t processorIdx) const {
        if (processorIdx >= nProcessorsGlobal) { return {}; }

        if (processorIdx < extra()) {
            return baseLoad() + 1;
        } else {
            return baseLoad();
        }
    }

    std::optional<size_t> getResponsibleProcessor(size_t vGlobalIdx) const {
        if (nVerticesGlobal == 0 || nProcessorsGlobal == 0 || vGlobalIdx >= nVerticesGlobal) return {};

        size_t threshold = (baseLoad() + 1) * extra();
        if (vGlobalIdx < threshold) {
            return vGlobalIdx / (baseLoad() + 1);
        } else {
            return extra() + (vGlobalIdx - threshold) / baseLoad();
        }
    }

    std::optional<size_t> getFirstGlobalIdxOf(size_t processorIdx) const {
        if (processorIdx >= nProcessorsGlobal) { return {}; }
        if (processorIdx < extra()) {
            return processorIdx * (baseLoad() + 1);
        } else {
            size_t baseOffset = extra() * (baseLoad() + 1);
            size_t normalOffset = (processorIdx - extra()) * baseLoad();
            return baseOffset + normalOffset;
        }
    }

    std::optional<size_t> globalToLocal(size_t vGlobalIdx) const {
        // First, find which processor owns this vertex
        auto ownerOpt = getResponsibleProcessor(vGlobalIdx);
        if (!ownerOpt) {
            return {}; // Vertex is out of bounds
        }
        
        // Next, find the first global index that processor owns
        auto firstIdxOpt = getFirstGlobalIdxOf(*ownerOpt);
        if (!firstIdxOpt) {
            return {}; // Should not happen if ownerOpt is valid, but good for safety
        }
        
        // The local index is the offset from that first index
        return vGlobalIdx - *firstIdxOpt;
    }
};

} // namespace BlockDistribution