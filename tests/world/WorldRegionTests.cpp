#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("talvisota-world-tests-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& Path() const noexcept { return path_; }
    void Write(const std::filesystem::path& relative, const std::string& data)
    {
        std::filesystem::create_directories((path_ / relative).parent_path());
        std::ofstream output(path_ / relative, std::ios::binary);
        output << data;
        Require(static_cast<bool>(output), "Could not write world fixture.");
    }
private:
    std::filesystem::path path_;
};

constexpr const char* WorldText =
    "talvisota-world\t1\n"
    "id = world.test\n"
    "name = Test World\n"
    "unit = meter\n"
    "up_axis = y\n"
    "north_axis = +z\n"
    "handedness = right\n"
    "bounds_min = -32, -5, -32\n"
    "bounds_max = 32, 20, 32\n";

constexpr const char* RegionText =
    "talvisota-region\t1\n"
    "id\tregion.test\n"
    "world\tworld.test\n"
    "bounds_min\t-16,-5,-16\n"
    "bounds_max\t16,10,16\n"
    "heightfield\t3\t3\t-16\t-16\t16\t16\n"
    "height_row\t0\t1,1,1\n"
    "height_row\t1\t1,1,1\n"
    "height_row\t2\t1,1,1\n"
    "placement\tterrain.main\tterrain\tmodel.terrain\tmaterial.snow\t0\t0\t0\t0\t1\n"
    "placement\ttree.01\tvegetation\tmodel.tree\t-\t4\t0\t3\t25\t1.2\n"
    "collision\trock.blocker\t-2\t0\t-2\t1\t1.5\t1\n"
    "nav_node\tpath.start\t-8\t0\t0\n"
    "nav_node\tpath.middle\t0\t0\t0\n"
    "nav_node\tpath.end\t8\t0\t0\n"
    "nav_edge\tpath.start\tpath.middle\n"
    "nav_edge\tpath.middle\tpath.end\n";

void Prepare(TemporaryDirectory& directory)
{
    directory.Write("worlds/test.tworld", WorldText);
    directory.Write("regions/test.tregion", RegionText);
    directory.Write("models/terrain.obj", "v 0 0 0\nv 1 0 0\nv 0 0 1\nf 1 2 3\n");
    directory.Write("models/tree.obj", "v 0 0 0\nv 0 2 0\nv 1 0 0\nf 1 2 3\n");
    directory.Write("materials/snow.tmat", "talvisota-material\t1\nid = material.snow\nbase_color = 0.8,0.85,0.9\nroughness = 0.9\nmetallic = 0\n");
    directory.Write("catalog.tmanifest",
        "talvisota-content-manifest\t1\n"
        "asset\tworld.test\tdefinition\tworlds/test.tworld\trequired\tmodel.terrain\tproject\tproject\n"
        "asset\tregion.test\tdefinition\tregions/test.tregion\trequired\tworld.test,model.terrain,model.tree,material.snow\tproject\tproject\n"
        "asset\tmodel.terrain\tmodel\tmodels/terrain.obj\trequired\t-\tproject\tproject\n"
        "asset\tmodel.tree\tmodel\tmodels/tree.obj\trequired\t-\tproject\tproject\n"
        "asset\tmaterial.snow\tmaterial\tmaterials/snow.tmat\trequired\t-\tproject\tproject\n");
}

void TestShippedTerrainHeightfieldMatchesVisualMeshVertices()
{
    const std::filesystem::path root = TALVISOTA_SOURCE_DIR;
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    Require(talvisota::content::LoadContentManifest(root / "content/catalog.tmanifest",
            root, registry, manifestDiagnostics), "Shipped content manifest should load.");
    talvisota::content::WorldDefinition world;
    std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
    Require(talvisota::content::LoadWorldDefinition(root / "content/worlds/central-finland.tworld",
            registry, world, worldDiagnostics), "Shipped Central Finland world should load.");
    talvisota::world::WorldRegion region;
    std::vector<talvisota::world::WorldRegionDiagnostic> regionDiagnostics;
    Require(talvisota::world::LoadWorldRegion(root / "content/worlds/central-finland.tregion",
            registry, world, region, regionDiagnostics), "Shipped Central Finland region should load.");
    Require(region.placements.size() == 12U && region.collision.size() == 11U,
            "Shipped environment should contain terrain, eight spruces, three boulders, and their collision volumes.");
    const auto spruceCount = std::count_if(region.placements.begin(), region.placements.end(),
        [](const auto& placement) { return placement.category == talvisota::world::PlacementCategory::Vegetation; });
    const auto boulderCount = std::count_if(region.placements.begin(), region.placements.end(),
        [](const auto& placement) { return placement.category == talvisota::world::PlacementCategory::Rock; });
    Require(spruceCount == 8 && boulderCount == 3,
            "Shipped region composition should retain all authored vegetation and rock placements.");
    bool blocked = false;
    std::string collisionError;
    Require(talvisota::world::QueryCollision(region, {-27.1F, 5.9F, -22.1F},
            {-26.9F, 6.1F, -21.9F}, blocked, collisionError) && blocked,
            "Spruce trunk collision should occupy its authored world position.");
    Require(talvisota::world::QueryCollision(region, {-11.1F, -0.1F, -8.1F},
            {-10.9F, 0.1F, -7.9F}, blocked, collisionError) && blocked,
            "Boulder collision should occupy its authored world position.");
    Require(talvisota::world::QueryCollision(region, {-0.5F, 0.0F, -0.5F},
            {0.5F, 1.8F, 0.5F}, blocked, collisionError) && !blocked,
            "The central trail corridor should remain clear of authored obstacle volumes.");
    std::ifstream model(root / "assets/models/terrain/central-finland.obj");
    Require(static_cast<bool>(model), "Shipped terrain OBJ should be readable.");
    std::size_t vertexIndex = 0U;
    std::string line;
    while (std::getline(model, line))
    {
        std::istringstream record(line);
        std::string type;
        record >> type;
        if (type != "v")
            continue;
        float x = 0.0F, y = 0.0F, z = 0.0F;
        Require(static_cast<bool>(record >> x >> y >> z), "Terrain OBJ vertex must contain three coordinates.");
        const std::size_t row = vertexIndex / region.terrain.columns;
        const std::size_t column = vertexIndex % region.terrain.columns;
        Require(row < region.terrain.rows, "Terrain OBJ has more vertices than the authored heightfield.");
        const float sampleX = region.terrain.originX + static_cast<float>(column) * region.terrain.spacingX;
        const float sampleZ = region.terrain.originZ + static_cast<float>(row) * region.terrain.spacingZ;
        float sampledHeight = 0.0F;
        std::array<float, 3> normal{};
        std::string error;
        Require(talvisota::world::SampleTerrainHeight(region, sampleX, sampleZ,
                sampledHeight, normal, error), "Terrain mesh vertex must have a corresponding surface sample.");
        Require(std::abs(sampledHeight - y) <= 1.0e-4F,
                "Terrain collision heightfield must match the visual OBJ vertex elevation.");
        Require(std::abs(x - sampleX) <= 1.0e-4F && std::abs(z - sampleZ) <= 1.0e-4F,
                "Terrain OBJ grid coordinates must match the heightfield origin and spacing.");
        ++vertexIndex;
    }
    Require(vertexIndex == region.terrain.rows * region.terrain.columns,
            "Terrain OBJ vertex count must exactly match the authored heightfield grid.");
}

void TestLoadsComposedRegionAndIndependentSpatialData()
{
    TemporaryDirectory directory;
    Prepare(directory);
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    Require(talvisota::content::LoadContentManifest(directory.Path() / "catalog.tmanifest",
            directory.Path(), registry, manifestDiagnostics), "Valid region manifest should load.");

    talvisota::content::WorldDefinition world;
    std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
    Require(talvisota::content::LoadWorldDefinition(directory.Path() / "worlds/test.tworld",
            registry, world, worldDiagnostics), "Parent world definition should load.");

    talvisota::world::WorldRegion region;
    std::vector<talvisota::world::WorldRegionDiagnostic> diagnostics;
    Require(talvisota::world::LoadWorldRegion(directory.Path() / "regions/test.tregion",
            registry, world, region, diagnostics), "Valid region should load.");
    Require(region.id == "region.test" && region.worldId == world.id, "Region identity and parent should be retained.");
    Require(region.placements.size() == 2U, "Terrain and vegetation placements should be retained.");
    Require(region.placements[0].category == talvisota::world::PlacementCategory::Terrain,
            "Placement categories should be parsed.");
    Require(region.placements[1].materialAssetId.empty() && region.placements[1].uniformScale == 1.2F,
            "Optional material and transform data should be parsed.");
    Require(region.collision.size() == 1U && region.collision[0].halfExtents[1] == 1.5F,
            "Collision boxes should be represented independently of visual placements.");
    Require(region.navigationNodes.size() == 3U && region.navigationEdges.size() == 2U,
            "Navigation graph should be loaded independently of collision and visual geometry.");
    Require(region.terrain.rows == 3U && region.terrain.columns == 3U,
            "Terrain heightfield dimensions should be retained.");
    float terrainHeight = -99.0F;
    std::array<float, 3> terrainNormal{9.0F, 9.0F, 9.0F};
    std::string terrainError;
    Require(talvisota::world::SampleTerrainHeight(region, -8.0F, -8.0F,
            terrainHeight, terrainNormal, terrainError),
            "Terrain surface should be sampleable inside its bounds.");
    Require(terrainHeight == 1.0F && terrainNormal == std::array<float, 3>{0.0F, 1.0F, 0.0F},
            "Bilinear terrain sampling should preserve a flat surface and upward normal.");
    Require(!talvisota::world::SampleTerrainHeight(region, 17.0F, 0.0F,
            terrainHeight, terrainNormal, terrainError),
            "Terrain sampling outside the authored grid must fail.");
    Require(terrainHeight == 1.0F && terrainNormal == std::array<float, 3>{0.0F, 1.0F, 0.0F},
            "Failed terrain sampling must preserve caller outputs.");
    Require(talvisota::world::SampleTerrainHeight(region, 16.0F, 16.0F,
            terrainHeight, terrainNormal, terrainError),
            "The final terrain grid vertex must be sampleable.");
    region.terrain.elevations = {0.0F, 1.0F, 2.0F, 0.0F, 1.0F, 2.0F, 0.0F, 1.0F, 2.0F};
    Require(talvisota::world::SampleTerrainHeight(region, -8.0F, -8.0F,
            terrainHeight, terrainNormal, terrainError),
            "A sloped terrain cell must be sampleable.");
    Require(std::abs(terrainHeight - 0.5F) < 1.0e-6F &&
            terrainNormal[0] < 0.0F && terrainNormal[1] > 0.99F,
            "Bilinear interpolation and gradient normals must follow the sampled terrain slope.");
    const auto transform = talvisota::world::BuildPlacementTransform(region.placements[1]);
    Require(transform[12] == 4.0F && transform[13] == 0.0F && transform[14] == 3.0F,
            "Placement transforms should preserve world-space translation.");
    std::vector<std::string> path;
    std::string pathError;
    Require(talvisota::world::FindNavigationPath(region, "path.start", "path.end", path, pathError),
            "Directed navigation path should be found.");
    Require(path == std::vector<std::string>({"path.start", "path.middle", "path.end"}),
            "Pathfinding should return the connected route in order.");
    const auto previousPath = path;
    Require(!talvisota::world::FindNavigationPath(region, "path.end", "path.start", path, pathError),
            "Navigation edges must remain directed.");
    Require(path == previousPath, "Failed pathfinding must preserve caller output.");
    bool intersects = false;
    Require(talvisota::world::QueryCollision(region, {-2.5F, 0.0F, -2.5F},
            {-1.5F, 0.5F, -1.5F}, intersects, pathError) && intersects,
            "Collision query should detect overlap with authored collision boxes.");
    Require(talvisota::world::QueryCollision(region, {8.0F, 2.0F, 8.0F},
            {9.0F, 3.0F, 9.0F}, intersects, pathError) && !intersects,
            "Collision query should report clear space.");
    intersects = true;
    Require(!talvisota::world::QueryCollision(region, {2.0F, 0.0F, 0.0F},
            {1.0F, 1.0F, 1.0F}, intersects, pathError) && intersects,
            "Invalid collision bounds must fail without modifying the result.");
}

void TestRejectsMissingDependenciesAndInvalidSpatialRecords()
{
    TemporaryDirectory directory;
    Prepare(directory);
    auto bad = std::string(RegionText);
    const auto placement = bad.find("model.tree");
    bad.replace(placement, std::string("model.tree").size(), "model.missing");
    directory.Write("regions/test.tregion", bad);
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    Require(talvisota::content::LoadContentManifest(directory.Path() / "catalog.tmanifest",
            directory.Path(), registry, manifestDiagnostics), "Manifest should load before region validation.");
    talvisota::content::WorldDefinition world;
    std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
    Require(talvisota::content::LoadWorldDefinition(directory.Path() / "worlds/test.tworld",
            registry, world, worldDiagnostics), "World definition should load.");
    talvisota::world::WorldRegion region;
    std::vector<talvisota::world::WorldRegionDiagnostic> diagnostics;
    Require(!talvisota::world::LoadWorldRegion(directory.Path() / "regions/test.tregion",
            registry, world, region, diagnostics), "Unknown placement assets must be rejected.");
    Require(!diagnostics.empty(), "Unknown placement assets must produce diagnostics.");
    bad = std::string(RegionText);
    const auto heightRow = bad.find("height_row\t1\t1,1,1\n");
    bad.erase(heightRow, std::string("height_row\t1\t1,1,1\n").size());
    directory.Write("regions/test.tregion", bad);
    diagnostics.clear();
    Require(!talvisota::world::LoadWorldRegion(directory.Path() / "regions/test.tregion",
            registry, world, region, diagnostics),
            "A heightfield with a missing sample row must be rejected.");
    Require(!diagnostics.empty(), "Missing height rows must produce diagnostics.");

    bad = std::string(RegionText);
    const auto firstHeightRow = bad.find("height_row\t0\t1,1,1\n");
    bad.insert(firstHeightRow, "height_row\t0\t1,1,1\n");
    directory.Write("regions/test.tregion", bad);
    diagnostics.clear();
    Require(!talvisota::world::LoadWorldRegion(directory.Path() / "regions/test.tregion",
            registry, world, region, diagnostics),
            "Duplicate height rows must be rejected.");

    bad = std::string(RegionText);
    const auto edge = bad.find("nav_edge\tpath.middle\tpath.end");
    bad.replace(edge, std::string("nav_edge\tpath.middle\tpath.end").size(),
                "nav_edge\tpath.middle\tpath.unknown");
    directory.Write("regions/test.tregion", bad);
    diagnostics.clear();
    Require(!talvisota::world::LoadWorldRegion(directory.Path() / "regions/test.tregion",
            registry, world, region, diagnostics), "Navigation edges to undeclared nodes must be rejected.");
}

void TestSessionLoadReloadFailureAndUnloadAreAtomic()
{
    TemporaryDirectory directory;
    Prepare(directory);
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    Require(talvisota::content::LoadContentManifest(directory.Path() / "catalog.tmanifest",
            directory.Path(), registry, manifestDiagnostics), "Manifest should load.");
    talvisota::content::WorldDefinition world;
    std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
    Require(talvisota::content::LoadWorldDefinition(directory.Path() / "worlds/test.tworld",
            registry, world, worldDiagnostics), "World should load.");

    talvisota::world::WorldRegionSession session;
    std::vector<talvisota::world::WorldRegionDiagnostic> diagnostics;
    Require(session.Load(directory.Path() / "regions/test.tregion", registry, world, diagnostics),
            "Session should load valid region.");
    Require(session.IsLoaded() && session.Active() != nullptr, "Loaded session must expose its active region.");
    const auto* previous = session.Active();
    directory.Write("regions/test.tregion", "talvisota-region\t1\nid\tbroken\n");
    Require(!session.Reload(directory.Path() / "regions/test.tregion", registry, world, diagnostics),
            "Invalid reload must fail.");
    Require(session.IsLoaded() && session.Active() == previous && session.Active()->id == "region.test",
            "Failed reload must preserve the previously active region.");
    session.Unload();
    Require(!session.IsLoaded() && session.Active() == nullptr, "Unload must release active region state.");
}
} // namespace

int main()
{
    try
    {
        TestShippedTerrainHeightfieldMatchesVisualMeshVertices();
        TestLoadsComposedRegionAndIndependentSpatialData();
        TestRejectsMissingDependenciesAndInvalidSpatialRecords();
        TestSessionLoadReloadFailureAndUnloadAreAtomic();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "World region test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All world region tests passed.\n";
    return 0;
}
