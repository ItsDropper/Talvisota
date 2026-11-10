#include "talvisota/presentation/DynamicWorld.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace talvisota::presentation;

namespace
{
void Require(const bool condition)
{
    if (!condition) std::abort();
}
}

int main()
{
    talvisota::world::WorldRegion region;
    region.id = "region.dynamic-test";
    region.worldId = "world.dynamic-test";
    region.boundsMin = {-20.0F, -5.0F, -20.0F};
    region.boundsMax = {20.0F, 20.0F, 20.0F};
    region.terrain.rows = 2U;
    region.terrain.columns = 2U;
    region.terrain.originX = -20.0F;
    region.terrain.originZ = -20.0F;
    region.terrain.spacingX = 40.0F;
    region.terrain.spacingZ = 40.0F;
    region.terrain.elevations = {0.0F, 0.0F, 0.0F, 0.0F};

    talvisota::world::WorldPlacement tree;
    tree.id = "placement.test-tree";
    tree.category = talvisota::world::PlacementCategory::Vegetation;
    tree.modelAssetId = "model.test-tree";
    tree.position = {2.0F, 0.0F, 3.0F};
    region.placements.push_back(tree);

    SceneSnapshot scene;
    RenderMesh mesh;
    mesh.vertices = {
        {{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        {{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        {{0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, 0.0F, 1.0F}
    };
    mesh.indices = {0U, 1U, 2U};
    scene.meshes.push_back(mesh);
    scene.materials.emplace_back();
    RenderInstance instance;
    instance.meshIndex = 0U;
    instance.materialIndex = 0U;
    instance.world = talvisota::world::BuildPlacementTransform(tree);
    scene.instances.push_back(instance);

    DynamicWorldSystem dynamics;
    std::string error;
    Require(dynamics.Initialize(region, scene, error));
    Require(dynamics.Objects().size() == 1U);
    Require(dynamics.Objects().front().instanceIndices.size() == 1U);

    const auto authored = scene.instances.front().world;
    Require(dynamics.Update(region, scene, 1.0F / 60.0F, 1.0,
        {0.914F, 0.406F}, 0.8F, error));
    Require(scene.instances.front().world != authored);

    Require(dynamics.ApplyImpulse(tree.id, {0.0F, 8.0F, 0.0F}, {0.5F, 0.0F, 0.2F}, error));
    Require(!dynamics.Objects().front().anchored);
    for (int frame = 0; frame < 30; ++frame)
        Require(dynamics.Update(region, scene, 1.0F / 60.0F,
            1.0 + static_cast<double>(frame) / 60.0, {0.914F, 0.406F}, 0.1F, error));
    Require(scene.instances.front().world[13] > tree.position[1]);

    const auto savePath = std::filesystem::temp_directory_path() / "talvisota-dynamic-world-test.state";
    Require(dynamics.Save(savePath, error));
    DynamicWorldSystem restored;
    SceneSnapshot restoredScene = scene;
    Require(restored.Initialize(region, restoredScene, error));
    Require(restored.Load(savePath, error));
    Require(!restored.Objects().front().anchored);
    Require(std::abs(restored.Objects().front().position[1] -
        dynamics.Objects().front().position[1]) < 0.0001F);

    Require(restored.ApplyDamage(tree.id, 0.5F, error));
    Require(std::abs(restored.Objects().front().integrity - 0.5F) < 0.0001F);
    Require(!restored.ApplyDamage("missing", 0.1F, error));
    std::error_code ec;
    std::filesystem::remove(savePath, ec);
    std::filesystem::remove(savePath.string() + ".tmp", ec);
    return 0;
}
