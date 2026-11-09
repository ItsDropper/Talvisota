#pragma once

#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/WorldDefinition.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace talvisota::world
{
enum class PlacementCategory { Terrain, Vegetation, Rock, Structure, Prop };
enum class TerrainSurfaceProfile { None, SnowDriftV1 };

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

struct TerrainHeightfield final
{
    std::size_t rows = 0;
    std::size_t columns = 0;
    float originX = 0.0F;
    float originZ = 0.0F;
    float spacingX = 1.0F;
    float spacingZ = 1.0F;
    TerrainSurfaceProfile surfaceProfile = TerrainSurfaceProfile::None;
    std::vector<float> elevations;
};

struct WaterSurface final
{
    std::string id;
    std::string placementId;
    float minX = 0.0F;
    float maxX = 0.0F;
    float minZ = 0.0F;
    float maxZ = 0.0F;
    float elevation = 0.0F;
};

struct WorldRegion final
{
    std::string id;
    std::string worldId;
    std::array<float, 3> boundsMin{};
    std::array<float, 3> boundsMax{};
    std::vector<WorldPlacement> placements;
    TerrainHeightfield terrain;
    std::vector<WaterSurface> waterSurfaces;
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

[[nodiscard]] std::array<float, 16> BuildPlacementTransform(const WorldPlacement& placement) noexcept;
[[nodiscard]] float SampleTerrainDisplacement(const WorldRegion& region, float x, float z) noexcept;
[[nodiscard]] bool SampleTerrainGroundHeight(const WorldRegion& region, float x, float z,
                                           float& height, std::array<float, 3>& normal,
                                           std::string& error);
[[nodiscard]] bool SampleTerrainHeight(const WorldRegion& region, float x, float z,
                                      float& height, std::array<float, 3>& normal,
                                      std::string& error);
[[nodiscard]] bool ResolveGroundedPlacementHeight(const WorldRegion& region,
    float x, float z, float authoredY, float& resolvedY, std::string& error);
[[nodiscard]] bool QueryCollision(const WorldRegion& region,
                                  const std::array<float, 3>& queryMin,
                                  const std::array<float, 3>& queryMax,
                                  bool& intersects,
                                  std::string& error);
[[nodiscard]] bool FindNavigationPath(const WorldRegion& region,
                                      std::string_view startNode,
                                      std::string_view goalNode,
                                      std::vector<std::string>& output,
                                      std::string& error);

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
