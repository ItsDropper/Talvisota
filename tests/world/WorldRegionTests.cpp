#include "talvisota/content/AuthoredDefinition.hpp"
#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <algorithm>
#include <charconv>
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

void TestSakkolaRegionHasMapInformedLandscapeAndDiverseComposition()
{
    const std::filesystem::path root = TALVISOTA_SOURCE_DIR;
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    Require(talvisota::content::LoadContentManifest(root / "content/catalog.tmanifest",
            root, registry, manifestDiagnostics), "Sakkola content manifest should load.");

    talvisota::content::WorldDefinition world;
    std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
    Require(talvisota::content::LoadWorldDefinition(root / "content/worlds/sakkola.tworld",
            registry, world, worldDiagnostics), "Sakkola world definition should load.");
    Require(world.id == "world.sakkola" && world.terrainModelId == "model.terrain.sakkola",
            "Sakkola world should own its registered terrain model.");
    Require(world.hasEnvironment && std::abs(world.environment.sunIntensity - 1.15F) < 0.001F &&
            std::abs(world.environment.ambientIntensity - 0.66F) < 0.001F &&
            std::abs(world.environment.fogDensity - 0.0014F) < 0.00001F &&
            std::abs(world.environment.fogStart - 65.0F) < 0.001F &&
            std::abs(world.environment.windStrength - 0.06F) < 0.001F,
            "Sakkola world should expose the authored overcast winter lighting, aerial perspective, and wind.");

    talvisota::world::WorldRegion region;
    std::vector<talvisota::world::WorldRegionDiagnostic> regionDiagnostics;
    if (!talvisota::world::LoadWorldRegion(root / "content/worlds/sakkola-church-village.tregion", registry, world, region, regionDiagnostics)) {
        for (const auto& diagnostic : regionDiagnostics)
            std::cerr << diagnostic.line << " " << diagnostic.field << ": " << diagnostic.message << "\\n";
        throw std::runtime_error("Sakkola church-village region should load.");
    }
    const auto juniperCount = static_cast<std::size_t>(std::count_if(region.placements.begin(), region.placements.end(),
        [](const auto& placement) { return placement.modelAssetId == "model.environment.juniper"; }));
    Require(juniperCount == 420U,
            "Sakkola winter understory must retain all authored common-juniper clusters.");
    Require(region.id == "region.sakkola.church_village",
            "The first Karelian region should retain its stable Sakkola identity.");
    Require(region.terrain.surfaceProfile == talvisota::world::TerrainSurfaceProfile::SnowDriftV1,
            "Sakkola declares the shared renderer/gameplay snow surface profile.");
    Require(region.terrain.rows == 177U && region.terrain.columns == 177U &&
            std::abs(region.terrain.spacingX - 23.272727272727273F) < 0.001F && std::abs(region.terrain.spacingZ - 23.272727272727273F) < 0.001F,
            "Sakkola should provide a 4,096 metre terrain block with a 177x177 heightfield at approximately 23.2727 metre spacing.");
    const auto [minimumElevation, maximumElevation] = std::minmax_element(
        region.terrain.elevations.begin(), region.terrain.elevations.end());
    Require(minimumElevation != region.terrain.elevations.end() &&
            *maximumElevation - *minimumElevation > 12.0F,
            "Sakkola's authored heightfield must retain its real moraine elevation range.");

    const auto terrainPlacement = std::find_if(region.placements.begin(), region.placements.end(),
        [](const auto& item) { return item.id == "terrain.main"; });
    Require(terrainPlacement != region.placements.end() &&
            terrainPlacement->position == std::array<float, 3>{0.0F, 0.0F, 0.0F} &&
            terrainPlacement->uniformScale == 1.0F,
            "The Sakkola terrain mesh already contains world-space elevations and must not receive a second vertical offset.");

    const auto vegetation = std::count_if(region.placements.begin(), region.placements.end(),
        [](const auto& item) { return item.category == talvisota::world::PlacementCategory::Vegetation; });
    const auto structures = std::count_if(region.placements.begin(), region.placements.end(),
        [](const auto& item) { return item.category == talvisota::world::PlacementCategory::Structure; });
    const auto rocks = std::count_if(region.placements.begin(), region.placements.end(),
        [](const auto& item) { return item.category == talvisota::world::PlacementCategory::Rock; });
    const auto props = std::count_if(region.placements.begin(), region.placements.end(),
        [](const auto& item) { return item.category == talvisota::world::PlacementCategory::Prop; });
    const auto winterBrush = std::count_if(region.placements.begin(), region.placements.end(),
        [](const auto& item) { return item.modelAssetId == "model.environment.winter_brush"; });
    Require(vegetation == 3094U && structures == 48U && rocks == 32U && props == 803U,
            "Sakkola should retain its dense mixed forest and village props while adding distributed ground clutter.");
    Require(winterBrush == 300U && region.placements.size() == 3978U,
            "Sakkola should contain 300 deterministic winter brush clumps and 3,094 mixed vegetation placements distributed across ecological stands.");
    Require(std::count_if(region.placements.begin(), region.placements.end(), [](const auto& placement) {
                return placement.modelAssetId == "model.interior.sakkola_church" ||
                    placement.modelAssetId == "model.interior.sakkola_farmhouse";
            }) == 13,
            "The shared Sakkola region must include the church and all twelve farmhouse interior models.");
    const auto brushInQuadrant = [&region](const bool east, const bool south) {
        return std::count_if(region.placements.begin(), region.placements.end(),
            [=](const auto& item) {
                return item.modelAssetId == "model.environment.winter_brush" &&
                    (item.position[0] >= 0.0F) == east &&
                    (item.position[2] >= 60.0F) == south;
            });
    };
    Require(brushInQuadrant(false, false) >= 50U && brushInQuadrant(true, false) >= 50U &&
            brushInQuadrant(false, true) >= 50U && brushInQuadrant(true, true) >= 50U,
            "Winter brush must populate all four land quadrants instead of accumulating in one strip.");

    std::vector<std::string> species;
    for (const auto& item : region.placements) {
        if (item.category == talvisota::world::PlacementCategory::Vegetation)
            species.push_back(item.modelAssetId);
    }
    std::sort(species.begin(), species.end());
    species.erase(std::unique(species.begin(), species.end()), species.end());
    Require(species.size() == 5U,
            "Sakkola forest should mix spruce, pine, birch, aspen, and alder rather than repeat one tree model.");
    const auto speciesCount = [&region](const std::string& model) {
        return std::count_if(region.placements.begin(), region.placements.end(),
            [&model](const auto& item) {
                return item.category == talvisota::world::PlacementCategory::Vegetation && item.modelAssetId == model;
            });
    };
    Require(speciesCount("model.environment.spruce") >= 140U &&
            speciesCount("model.environment.pine") >= 100U &&
            speciesCount("model.environment.birch") >= 70U &&
            speciesCount("model.environment.aspen") >= 30U &&
            speciesCount("model.environment.alder") >= 40U,
            "The enlarged forest must retain all five species with a deliberate conifer, broadleaf, and shoreline mix.");

    const auto countStand = [&region](const float minX, const float maxX,
        const float minZ, const float maxZ) {
        return std::count_if(region.placements.begin(), region.placements.end(),
            [=](const auto& item) {
                return item.category == talvisota::world::PlacementCategory::Vegetation &&
                    item.position[0] >= minX && item.position[0] <= maxX &&
                    item.position[2] >= minZ && item.position[2] <= maxZ;
            });
    };
    Require(countStand(108.0F, 236.0F, -100.0F, -15.0F) >= 70U &&
            countStand(-236.0F, -108.0F, -100.0F, -15.0F) >= 70U &&
            countStand(-236.0F, -108.0F, 184.0F, 250.0F) >= 50U &&
            countStand(108.0F, 236.0F, 184.0F, 250.0F) >= 50U,
            "Sakkola trees must form four dense, species-mixed forest stands while preserving the central village clearing.");

    std::size_t matureTreeCount = 0U;
    for (const auto& placement : region.placements) {
        const bool tree = placement.category == talvisota::world::PlacementCategory::Vegetation;
        const bool boulder = placement.category == talvisota::world::PlacementCategory::Rock;
        const bool fallenLog = placement.modelAssetId == "model.environment.fallen_log";
        const bool stump = placement.modelAssetId == "model.environment.stump";
        const bool winterBrushClump = placement.modelAssetId == "model.environment.winter_brush";
        const bool shorelineReed = placement.id.rfind("prop.reeds.", 0) == 0;
        if (!tree && !boulder && !fallenLog && !stump && !winterBrushClump && !shorelineReed)
            continue;

        float surfaceHeight = 0.0F;
        std::array<float, 3> surfaceNormal{};
        std::string terrainError;
        Require(talvisota::world::SampleTerrainHeight(region, placement.position[0],
                placement.position[2], surfaceHeight, surfaceNormal, terrainError),
                "Grounded Sakkola environment placements must lie within the terrain heightfield.");
        const float surfaceDisplacement = talvisota::world::SampleTerrainDisplacement(
            region, placement.position[0], placement.position[2]);
        const float groundingError = placement.position[1] + surfaceDisplacement - surfaceHeight;
        if (std::abs(groundingError) > 0.002F) {
            std::cerr << "Ungrounded placement " << placement.id << " model=" << placement.modelAssetId
                      << " positionY=" << placement.position[1]
                      << " displacement=" << surfaceDisplacement
                      << " sampledHeight=" << surfaceHeight
                      << " error=" << groundingError << '\n';
        }
        Require(std::abs(groundingError) <= 0.002F,
                "Rendered trees, boulders, and shoreline reeds must rest on the displaced terrain surface.");

        if (tree) {
            const float maximumScale = placement.modelAssetId == "model.environment.spruce" ? 1.95F :
                placement.modelAssetId == "model.environment.pine" ? 1.55F :
                placement.modelAssetId == "model.environment.birch" ? 1.55F :
                placement.modelAssetId == "model.environment.aspen" ? 1.35F : 1.35F;
            const float minimumScale = placement.modelAssetId == "model.environment.spruce" ? 0.72F :
                placement.modelAssetId == "model.environment.pine" ? 0.70F :
                placement.modelAssetId == "model.environment.birch" ? 0.70F :
                placement.modelAssetId == "model.environment.aspen" ? 0.65F : 0.65F;
            Require(placement.uniformScale >= minimumScale && placement.uniformScale <= maximumScale,
                    "Tree instance scale must stay within species-specific juvenile and mature height limits.");
        }
        if (tree || fallenLog || stump) {
            const std::string collisionId = placement.id + (tree ? ".trunk" : ".collision");
            const auto collision = std::find_if(region.collision.begin(), region.collision.end(),
                [&collisionId](const auto& box) { return box.id == collisionId; });
            Require(collision != region.collision.end(),
                    "Every grounded tree, fallen log, and stump must have its matching collision volume.");
            const float collisionBottom = collision->center[1] - collision->halfExtents[1];
            float expectedCollisionBottom = 0.0F;
            std::string collisionGroundingError;
            Require(talvisota::world::ResolveGroundedPlacementHeight(region,
                        placement.position[0], placement.position[2], placement.position[1],
                        expectedCollisionBottom, collisionGroundingError),
                    "Grounded vegetation and deadwood collision positions must resolve against terrain.");
            if (std::abs(collisionBottom - expectedCollisionBottom) > 0.002F) {
                std::cerr << "Misaligned collision " << collision->id << " placement=" << placement.id
                          << " bottom=" << collisionBottom << " expected=" << expectedCollisionBottom
                          << " displacement=" << surfaceDisplacement << '\n';
            }
            Require(std::abs(collisionBottom - expectedCollisionBottom) <= 0.002F,
                    "Grounded vegetation and deadwood collision bottoms must agree with their displaced terrain height.");
            if (tree && collision->halfExtents[1] * 2.0F >= 11.0F)
                ++matureTreeCount;
        }
    }
    Require(matureTreeCount >= 100U,
            "Sakkola forest must include a mature canopy layer in addition to younger tree growth.");

    const auto water = std::find_if(region.placements.begin(), region.placements.end(),
        [](const auto& item) { return item.id == "ice.suvanto"; });
    Require(water != region.placements.end() &&
            water->modelAssetId == "model.environment.suvanto_ice",
            "Sakkola region should include the frozen Suvanto surface.");
    Require(region.waterSurfaces.size() == 1U,
            "Sakkola should define one explicit frozen Suvanto water surface.");
    const auto& suvantoSurface = region.waterSurfaces.front();
    Require(suvantoSurface.id == "water.suvanto" &&
            suvantoSurface.placementId == "ice.suvanto" &&
            std::abs(suvantoSurface.minX + 2048.0F) < 0.001F &&
            std::abs(suvantoSurface.maxX - 2048.0F) < 0.001F &&
            std::abs(suvantoSurface.minZ + 2048.0F) < 0.001F &&
            std::abs(suvantoSurface.maxZ + 256.0F) < 0.001F &&
            std::abs(suvantoSurface.elevation - 1.82F) < 0.001F,
            "Frozen Suvanto must cover the authored sea-level lake footprint.");
    Require(talvisota::world::SampleTerrainDisplacement(region, 0.0F, -1024.0F) == 0.0F,
            "Snow-drift displacement must not deform the frozen lake.");
    Require(talvisota::world::SampleTerrainDisplacement(region, 0.0F, -200.0F) > 0.01F,
            "Snow-drift displacement must remain active on the north shore.");

    // The expanded terrain mesh's longest refined edge is approximately 16.45 m.
    // Adjacent vertex displacement must remain bounded at that sampling interval;
    // this catches high-frequency FBM octaves that alias across the final triangles.
    constexpr std::array<float, 7> displacementProbeX{
        -1700.0F, -1200.0F, -600.0F, 0.0F, 600.0F, 1200.0F, 1700.0F};
    constexpr std::array<float, 7> displacementProbeZ{
        -200.0F, 0.0F, 300.0F, 700.0F, 1100.0F, 1500.0F, 1900.0F};
    constexpr float refinedTerrainEdge = 16.45F;
    constexpr float maximumAdjacentDisplacementDelta = 0.35F;
    for (const float x : displacementProbeX)
    {
        for (const float z : displacementProbeZ)
        {
            const float displacement = talvisota::world::SampleTerrainDisplacement(region, x, z);
            const float adjacentX = talvisota::world::SampleTerrainDisplacement(
                region, x + refinedTerrainEdge, z);
            const float adjacentZ = talvisota::world::SampleTerrainDisplacement(
                region, x, z + refinedTerrainEdge);
            Require(std::abs(adjacentX - displacement) <= maximumAdjacentDisplacementDelta &&
                    std::abs(adjacentZ - displacement) <= maximumAdjacentDisplacementDelta,
                    "Snow displacement must be band-limited for the refined terrain mesh; "
                    "adjacent 16.45 m samples cannot jump by more than 0.35 m.");
        }
    }

    float lakeHeight = 0.0F;
    std::array<float, 3> lakeNormal{};
    std::string lakeError;
    Require(talvisota::world::SampleTerrainHeight(region, 0.0F, -1024.0F,
            lakeHeight, lakeNormal, lakeError) &&
            std::abs(lakeHeight - 1.82F) < 0.001F &&
            lakeNormal == std::array<float, 3>{0.0F, 1.0F, 0.0F},
            "Terrain sampling and player grounding must use the frozen lake surface.");
    for (const auto& placement : region.placements)
    {
        if (placement.id == suvantoSurface.placementId)
            continue;
        Require(placement.position[0] < suvantoSurface.minX ||
                placement.position[0] > suvantoSurface.maxX ||
                placement.position[2] < suvantoSurface.minZ ||
                placement.position[2] > suvantoSurface.maxZ,
                "Land placements must not be authored inside the frozen lake.");
    }

    std::vector<std::string> path;
    std::string navigationError;
    Require(talvisota::world::FindNavigationPath(region, "path.west", "path.church",
            path, navigationError), "Sakkola village path should connect the west approach to the church.");
    Require(path == std::vector<std::string>({"path.west", "path.crossroads", "path.church"}),
            "Sakkola village navigation should preserve its authored route.");

    const auto* terrainAsset = registry.Find("model.terrain.sakkola");
    Require(terrainAsset != nullptr, "Sakkola terrain model must remain registered.");
    std::ifstream model(root / terrainAsset->relativePath);
    Require(static_cast<bool>(model), "Sakkola terrain model should be readable.");
    std::size_t vertexIndex = 0U;
    std::size_t textureCoordinateCount = 0U;
    std::vector<std::array<float, 3>> visualVertices;
    std::vector<std::array<std::size_t, 3>> visualFaces;
    std::string line;
    while (std::getline(model, line)) {
        std::istringstream record(line);
        std::string type;
        record >> type;
        if (type == "v") {
            float x = 0.0F, y = 0.0F, z = 0.0F;
            Require(static_cast<bool>(record >> x >> y >> z),
                    "Sakkola terrain vertices should contain three coordinates.");
            const std::size_t row = vertexIndex / region.terrain.columns;
            const std::size_t column = vertexIndex % region.terrain.columns;
            Require(row < region.terrain.rows,
                    "Sakkola terrain model must not exceed its authored heightfield.");
            const float sampleX = region.terrain.originX +
                static_cast<float>(column) * region.terrain.spacingX;
            const float sampleZ = region.terrain.originZ +
                static_cast<float>(row) * region.terrain.spacingZ;
            float sampledHeight = 0.0F;
            std::array<float, 3> normal{};
            std::string error;
            Require(talvisota::world::SampleTerrainHeight(region, sampleX, sampleZ,
                    sampledHeight, normal, error),
                    "Sakkola terrain model vertices should have matching heightfield samples.");
            const float displacement = talvisota::world::SampleTerrainDisplacement(
                region, sampleX, sampleZ);
            const float terrainBaseHeight =
                region.terrain.elevations[row * region.terrain.columns + column];
            const float terrainMeshHeight = terrainBaseHeight + displacement;
            const auto waterSurfaceAtSample = std::find_if(region.waterSurfaces.begin(),
                region.waterSurfaces.end(), [sampleX, sampleZ](const auto& surface)
                {
                    return sampleX >= surface.minX && sampleX <= surface.maxX &&
                           sampleZ >= surface.minZ && sampleZ <= surface.maxZ;
                });
            const float expectedSampledHeight = waterSurfaceAtSample == region.waterSurfaces.end()
                ? terrainMeshHeight : waterSurfaceAtSample->elevation;
            Require(std::abs(terrainBaseHeight - y) <= 1.0e-4F &&
                    std::abs(sampledHeight - expectedSampledHeight) <= 1.0e-4F &&
                    std::abs(x - sampleX) <= 0.001F &&
                    std::abs(z - sampleZ) <= 0.001F && normal[1] > 0.0F,
                    "Sakkola terrain OBJ must match raw heightfield elevations while terrain sampling returns displaced land or level ice collision height.");
            visualVertices.push_back({x, y, z});
            ++vertexIndex;
        }
        else if (type == "vt") {
            float u = 0.0F, v = 0.0F;
            Require(static_cast<bool>(record >> u >> v) && std::isfinite(u) && std::isfinite(v),
                    "Sakkola terrain texture coordinates should be finite.");
            ++textureCoordinateCount;
        }
        else if (type == "f") {
            std::array<std::size_t, 3> face{};
            for (std::size_t corner = 0; corner < face.size(); ++corner) {
                std::string token;
                Require(static_cast<bool>(record >> token),
                        "Sakkola terrain faces should contain three vertex/UV indices.");
                const std::size_t slash = token.find('/');
                Require(slash != std::string::npos,
                        "Sakkola terrain faces must reference UV coordinates.");
                const char* begin = token.data();
                const char* end = begin + slash;
                const auto parsed = std::from_chars(begin, end, face[corner]);
                Require(parsed.ec == std::errc{} && parsed.ptr == end,
                        "Sakkola terrain face vertex indices should be valid integers.");
                std::size_t textureIndex = 0U;
                const std::size_t normalSlash = token.find('/', slash + 1U);
                const char* textureBegin = token.data() + slash + 1U;
                const char* textureEnd = token.data() +
                    (normalSlash == std::string::npos ? token.size() : normalSlash);
                const auto parsedTexture = std::from_chars(textureBegin, textureEnd, textureIndex);
                Require(textureBegin != textureEnd && parsedTexture.ec == std::errc{} &&
                        parsedTexture.ptr == textureEnd && textureIndex > 0U &&
                        textureIndex <= textureCoordinateCount,
                        "Sakkola terrain face UV indices should reference the authored texture grid.");
            }
            visualFaces.push_back(face);
        }
    }
    Require(vertexIndex == region.terrain.rows * region.terrain.columns &&
            textureCoordinateCount == vertexIndex,
            "Sakkola visual terrain, UV grid and collision heightfield must have identical dimensions.");
    Require(!visualFaces.empty(), "Sakkola terrain must contain indexed surface triangles.");
    for (const auto& face : visualFaces) {
        Require(face[0] > 0U && face[1] > 0U && face[2] > 0U &&
                face[0] <= visualVertices.size() && face[1] <= visualVertices.size() &&
                face[2] <= visualVertices.size(),
                "Sakkola terrain faces must reference valid source vertices.");
        const auto& a = visualVertices[face[0] - 1U];
        const auto& b = visualVertices[face[1] - 1U];
        const auto& c = visualVertices[face[2] - 1U];
        const float dx1 = b[0] - a[0], dz1 = b[2] - a[2];
        const float dx2 = c[0] - a[0], dz2 = c[2] - a[2];
        const float normalY = dz1 * dx2 - dx1 * dz2;
        Require(normalY > 0.0F,
                "Sakkola terrain triangles must face upward for correct back-face culling and lighting.");
    }
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
    float groundedY = 0.0F;
    std::string groundingError;
    Require(talvisota::world::ResolveGroundedPlacementHeight(region, 0.0F, 0.0F,
            -3.0F, groundedY, groundingError) && std::abs(groundedY - 1.02F) < 1.0e-5F,
            "Grounded placements below the sampled terrain must be lifted just above the surface.");
    Require(talvisota::world::ResolveGroundedPlacementHeight(region, 0.0F, 0.0F,
            3.0F, groundedY, groundingError) && std::abs(groundedY - 3.0F) < 1.0e-5F,
            "Grounded placements already above terrain must preserve their authored elevation.");
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

void TestLargeTerrainHeightfieldLoadsAndSamplesAcrossFullExtent()
{
    TemporaryDirectory directory;
    Prepare(directory);
    auto worldText = std::string(WorldText);
    const auto worldMin = worldText.find("-32, -5, -32");
    worldText.replace(worldMin, std::string("-32, -5, -32").size(), "-200, -5, -200");
    const auto worldMax = worldText.find("32, 20, 32");
    worldText.replace(worldMax, std::string("32, 20, 32").size(), "200, 20, 200");
    directory.Write("worlds/test.tworld", worldText);

    std::ostringstream regionText;
    regionText << "talvisota-region\t1\n"
        << "id\tregion.test\nworld\tworld.test\n"
        << "bounds_min\t-150,-5,-150\nbounds_max\t149,10,149\n"
        << "heightfield\t300\t300\t-150\t-150\t1\t1\n";
    for (std::size_t row = 0; row < 300U; ++row)
    {
        regionText << "height_row\t" << row << '\t';
        for (std::size_t column = 0; column < 300U; ++column)
        {
            if (column != 0U) regionText << ',';
            regionText << ((row == 299U && column == 299U) ? "3.0" : "2.0");
        }
        regionText << '\n';
    }
    regionText << "placement\tterrain.main\tterrain\tmodel.terrain\tmaterial.snow\t0\t0\t0\t0\t1\n";
    directory.Write("regions/test.tregion", regionText.str());

    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    Require(talvisota::content::LoadContentManifest(directory.Path() / "catalog.tmanifest",
            directory.Path(), registry, manifestDiagnostics), "Large-terrain manifest should load.");
    talvisota::content::WorldDefinition world;
    std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
    Require(talvisota::content::LoadWorldDefinition(directory.Path() / "worlds/test.tworld",
            registry, world, worldDiagnostics), "Large-terrain world should load.");
    talvisota::world::WorldRegion region;
    std::vector<talvisota::world::WorldRegionDiagnostic> diagnostics;
    Require(talvisota::world::LoadWorldRegion(directory.Path() / "regions/test.tregion",
            registry, world, region, diagnostics),
            "A 300-by-300 terrain grid must load instead of being rejected by the former 257-sample cap.");
    Require(region.terrain.rows == 300U && region.terrain.columns == 300U &&
            region.terrain.elevations.size() == 90000U,
            "Large terrain dimensions and all elevation samples must be retained.");
    float height = 0.0F;
    std::array<float, 3> normal{};
    std::string error;
    Require(talvisota::world::SampleTerrainHeight(region, 149.0F, 149.0F, height, normal, error),
            "Terrain sampling must work at the far corner of an expanded heightfield.");
    Require(std::abs(height - 3.0F) < 1.0e-5F && normal[1] > 0.0F,
            "The far-corner elevation and finite upward surface normal must be preserved.");
}


void TestTaipaleRiverfrontWorldAndMissionLoad()
{
    const std::filesystem::path root = TALVISOTA_SOURCE_DIR;
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    Require(talvisota::content::LoadContentManifest(root / "content/catalog.tmanifest",
            root, registry, manifestDiagnostics), "Taipale content manifest should load.");

    talvisota::content::WorldDefinition world;
    std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
    Require(talvisota::content::LoadWorldDefinition(root / "content/worlds/taipale.tworld",
            registry, world, worldDiagnostics), "Taipale world definition should load.");
    Require(world.id == "world.taipale" && world.terrainModelId == "model.terrain.taipale" &&
            world.hasEnvironment && world.environment.fogDensity > 0.001F,
            "Taipale must expose its own terrain and winter atmosphere.");

    talvisota::world::WorldRegion region;
    std::vector<talvisota::world::WorldRegionDiagnostic> regionDiagnostics;
    Require(talvisota::world::LoadWorldRegion(root / "content/worlds/taipale-riverfront.tregion",
            registry, world, region, regionDiagnostics),
            "Taipale riverfront region should load with its frozen-water metadata.");
    Require(region.id == "region.taipale.riverfront" &&
            region.terrain.rows == 65U && region.terrain.columns == 65U &&
            region.waterSurfaces.size() == 32U,
            "Taipale must retain its 4 km terrain grid and segmented river surface.");

    float riverHeight = 0.0F;
    std::array<float, 3> riverNormal{};
    std::string error;
    Require(talvisota::world::SampleTerrainHeight(region, 0.0F, 620.0F,
            riverHeight, riverNormal, error) &&
            std::abs(riverHeight - 4.60F) < 0.001F && riverNormal[1] > 0.99F,
            "The frozen river must provide a stable walkable surface height.");

    const auto countModel = [&region](const std::string& model) {
        return std::count_if(region.placements.begin(), region.placements.end(),
            [&model](const auto& placement) { return placement.modelAssetId == model; });
    };
    Require(countModel("model.environment.scots_pine") > 100U &&
            countModel("model.environment.spruce") > 10U &&
            countModel("model.environment.birch") > 10U &&
            countModel("model.environment.boulder") == 36U,
            "Taipale must contain a mixed forest and authored rock field.");
    for (const std::string target : {
            "prop.taipale.watch_post", "prop.taipale.farmstead", "prop.taipale.command_hut"}) {
        Require(std::any_of(region.placements.begin(), region.placements.end(),
                [&target](const auto& placement) { return placement.id == target; }) &&
                std::any_of(region.collision.begin(), region.collision.end(),
                [&target](const auto& collision) { return collision.id == target + ".collision"; }),
                "Every Taipale mission location must have a world placement and interaction clearance.");
    }

    const auto* missionAsset = registry.Find("mission.taipale.riverfront-watch");
    Require(missionAsset != nullptr, "Taipale introductory mission must be registered.");
    talvisota::content::AuthoredDefinition mission;
    std::vector<talvisota::content::AuthoredDefinitionDiagnostic> missionDiagnostics;
    Require(talvisota::content::LoadAuthoredDefinition(root / missionAsset->relativePath,
            registry, mission, missionDiagnostics) &&
            mission.worldId == world.id && mission.regionId == region.id &&
            mission.objectives.size() == 3U,
            "Taipale's authored mission must resolve against its own world and region.");
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
        TestSakkolaRegionHasMapInformedLandscapeAndDiverseComposition();
        TestTaipaleRiverfrontWorldAndMissionLoad();
        TestLoadsComposedRegionAndIndependentSpatialData();
        TestRejectsMissingDependenciesAndInvalidSpatialRecords();
        TestLargeTerrainHeightfieldLoadsAndSamplesAcrossFullExtent();
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
