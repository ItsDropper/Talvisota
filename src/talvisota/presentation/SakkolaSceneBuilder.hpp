#pragma once

#include "talvisota/presentation/SceneRenderer.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace talvisota::presentation
{
[[nodiscard]] bool BuildWorldScene(const std::filesystem::path& root,
    std::string_view worldId, std::string_view regionId, SceneSnapshot& scene,
    std::vector<float>& instanceMaxDrawDistances,
    std::size_t& placementCount, std::size_t& drawCount,
    talvisota::world::WorldRegion& outputRegion, std::string& error);
} // namespace talvisota::presentation
