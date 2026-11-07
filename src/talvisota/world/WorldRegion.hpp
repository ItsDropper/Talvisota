#pragma once

#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/WorldDefinition.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace talvisota::world
{
enum class PlacementCategory { Terrain, Vegetation, Rock, Structure, Prop };

struct WorldPlacement final
{
    std::string id;
    PlacementCategory category = PlacementCategory::Prop;
    std::string modelAssetId;
    std::string materialAssetId;
    std::array<float, 3> position{};
    float yawDegrees = 0.0F;
    float uniformScale = 1.0F;
};

struct CollisionBox final
{
    std::string id;
    std::array<float, 3> center{};
    std::array<float, 3> halfExtents{};
};

struct NavigationNode final
{
    std::string id;
    std::array<float, 3> position{};
};

struct NavigationEdge final
{
    std::string from;
    std::string to;
};

struct WorldRegion final
{
    std::string id;
    std::string worldId;
    std::array<float, 3> boundsMin{};
    std::array<float, 3> boundsMax{};
    std::vector<WorldPlacement> placements;
    std::vector<CollisionBox> collision;
    std::vector<NavigationNode> navigationNodes;
    std::vector<NavigationEdge> navigationEdges;
};

struct WorldRegionDiagnostic final
{
    std::filesystem::path file;
    std::size_t line = 0;
    std::string field;
    std::string message;
};

[[nodiscard]] bool LoadWorldRegion(const std::filesystem::path& path,
                                   const content::ContentRegistry& registry,
                                   const content::WorldDefinition& world,
                                   WorldRegion& output,
                                   std::vector<WorldRegionDiagnostic>& diagnostics);

// A region is published atomically: failed loads never replace the active region.
// No renderer/GPU resources are owned here; those belong to the presentation adapter.
class WorldRegionSession final
{
public:
    [[nodiscard]] bool Load(const std::filesystem::path& path,
                            const content::ContentRegistry& registry,
                            const content::WorldDefinition& world,
                            std::vector<WorldRegionDiagnostic>& diagnostics);
    [[nodiscard]] bool Reload(const std::filesystem::path& path,
                              const content::ContentRegistry& registry,
                              const content::WorldDefinition& world,
                              std::vector<WorldRegionDiagnostic>& diagnostics);
    void Unload() noexcept;
    [[nodiscard]] bool IsLoaded() const noexcept;
    [[nodiscard]] const WorldRegion* Active() const noexcept;

private:
    WorldRegion active_;
    bool loaded_ = false;
};
} // namespace talvisota::world
