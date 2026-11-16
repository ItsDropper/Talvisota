#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace talvisota::presentation
{

struct TriangleSubdivisionEstimate final
{
    std::size_t sourceVertices = 0U;
    std::size_t sourceIndices = 0U;
    std::size_t uniqueEdges = 0U;
    std::size_t resultVertices = 0U;
    std::size_t resultIndices = 0U;
};

// Mirrors a four-way midpoint subdivision that caches one midpoint per undirected
// index pair. Degenerate self-edges are intentionally counted: the runtime midpoint
// cache creates a midpoint for (index, index) as well, so excluding it would undercount.
// On failure, output is preserved and error identifies the invalid input or overflow.
[[nodiscard]] inline bool EstimateFourWayTriangleSubdivision(
    const std::size_t vertexCount,
    const std::span<const std::uint32_t> indices,
    TriangleSubdivisionEstimate& output,
    std::string& error)
{
    if (vertexCount == 0U ||
        vertexCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        error = "Triangle subdivision requires a non-zero vertex count representable by 32-bit indices.";
        return false;
    }
    if (indices.empty() || indices.size() % 3U != 0U)
    {
        error = "Triangle subdivision requires a non-empty index list containing complete triangles.";
        return false;
    }
    if (indices.size() > std::numeric_limits<std::size_t>::max() / 4U)
    {
        error = "Triangle subdivision index count would overflow size_t.";
        return false;
    }
    if (indices.size() > std::numeric_limits<std::size_t>::max() / sizeof(std::uint64_t))
    {
        error = "Triangle subdivision edge-key storage would overflow size_t.";
        return false;
    }

    std::vector<std::uint64_t> edgeKeys;
    edgeKeys.reserve(indices.size());
    const auto appendEdge = [&edgeKeys](const std::uint32_t first,
        const std::uint32_t second) {
        const std::uint32_t minimum = std::min(first, second);
        const std::uint32_t maximum = std::max(first, second);
        edgeKeys.push_back((static_cast<std::uint64_t>(minimum) << 32U) | maximum);
    };

    for (std::size_t index = 0U; index < indices.size(); index += 3U)
    {
        const std::uint32_t first = indices[index];
        const std::uint32_t second = indices[index + 1U];
        const std::uint32_t third = indices[index + 2U];
        if (static_cast<std::size_t>(first) >= vertexCount ||
            static_cast<std::size_t>(second) >= vertexCount ||
            static_cast<std::size_t>(third) >= vertexCount)
        {
            error = "Triangle subdivision found an index outside the source vertex array at triangle " +
                std::to_string(index / 3U) + ".";
            return false;
        }

        appendEdge(first, second);
        appendEdge(second, third);
        appendEdge(third, first);
    }

    std::sort(edgeKeys.begin(), edgeKeys.end());
    edgeKeys.erase(std::unique(edgeKeys.begin(), edgeKeys.end()), edgeKeys.end());
    const std::size_t uniqueEdgeCount = edgeKeys.size();
    if (uniqueEdgeCount > std::numeric_limits<std::size_t>::max() - vertexCount)
    {
        error = "Triangle subdivision vertex count would overflow size_t.";
        return false;
    }

    const std::size_t resultVertexCount = vertexCount + uniqueEdgeCount;
    if (resultVertexCount > static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max()))
    {
        error = "Triangle subdivision would exceed the 32-bit vertex-index capacity.";
        return false;
    }

    TriangleSubdivisionEstimate candidate;
    candidate.sourceVertices = vertexCount;
    candidate.sourceIndices = indices.size();
    candidate.uniqueEdges = uniqueEdgeCount;
    candidate.resultVertices = resultVertexCount;
    candidate.resultIndices = indices.size() * 4U;
    output = candidate;
    error.clear();
    return true;
}

[[nodiscard]] inline bool FitsVertexBudget(
    const TriangleSubdivisionEstimate& estimate,
    const std::size_t maximumVertices,
    std::string& error)
{
    if (maximumVertices == 0U)
    {
        error = "Triangle subdivision vertex budget must be non-zero.";
        return false;
    }
    if (estimate.resultVertices > maximumVertices)
    {
        error = "Triangle subdivision would produce " + std::to_string(estimate.resultVertices) +
            " vertices from " + std::to_string(estimate.sourceVertices) + " source vertices and " +
            std::to_string(estimate.uniqueEdges) + " unique midpoint edges, exceeding the " +
            std::to_string(maximumVertices) + "-vertex limit (" +
            std::to_string(estimate.resultIndices) + " result indices).";
        return false;
    }
    error.clear();
    return true;
}

} // namespace talvisota::presentation
