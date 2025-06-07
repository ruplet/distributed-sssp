#pragma once

#include <cassert>
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

            assert (baseLoad() * nProcessorsGlobal + extra() == nVerticesGlobal);
        }
    
    class InvalidDistribution : public std::runtime_error {
    public:
        InvalidDistribution() : std::runtime_error("Cannot distribute nonzero work among zero processors") {}
    };

    size_t baseLoad() {
        if (nProcessorsGlobal == 0) { return 0; }
        return nVerticesGlobal / nProcessorsGlobal;
    }
    size_t extra() {
        if (nProcessorsGlobal == 0) { return 0; }
        return nVerticesGlobal % nProcessorsGlobal;
    }
    
    std::optional<size_t> getNResponsibleVertices(size_t processorIdx) {
        if (processorIdx >= nProcessorsGlobal) { return {}; }

        if (processorIdx < extra()) {
            return baseLoad() + 1;
        } else {
            return baseLoad();
        }
    }

    std::optional<size_t> getResponsibleProcessor(size_t vGlobalIdx) {
        if (nVerticesGlobal == 0 || nProcessorsGlobal == 0 || vGlobalIdx >= nVerticesGlobal) return {};

        size_t threshold = (baseLoad() + 1) * extra();
        if (vGlobalIdx < threshold) {
            return vGlobalIdx / (baseLoad() + 1);
        } else {
            return extra() + (vGlobalIdx - threshold) / baseLoad();
        }
    }
};

} // namespace BlockDistribution