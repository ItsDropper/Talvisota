#pragma once

#include "talvisota/vegetation/Skeleton.hpp"

#include <cstdint>
#include <vector>

namespace talvisota::vegetation
{
struct MeshVertex final
{
    Float3 position{};
    Float3 normal{0.0F, 1.0F, 0.0F};
    float u = 0.0F;
    float v = 0.0F;
};

struct MeshSection final
{
    std::int32_t materialGroup = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};

struct TreeMesh final
{
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshSection> sections;
};

// Converts tapered skeleton segments into indexed swept tubes. A stable local
// frame, deterministic bark relief and physical-length UVs avoid billboard-like
// branches and stretched bark textures.
[[nodiscard]] TreeMesh BuildTreeMesh(const TreeSkeleton& skeleton,
    std::uint64_t seed, int radialSegments = 8);
} // namespace talvisota::vegetation
