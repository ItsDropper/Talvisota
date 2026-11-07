#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
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
