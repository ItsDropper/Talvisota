#include "talvisota/presentation/DynamicWorld.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

using namespace talvisota::presentation;

namespace
{
void RequireImpl(const bool condition,const int line)
{
    if (!condition) {
        std::cerr << "DynamicWorldTests assertion failed at line " << line << '\n';
        std::abort();
    }
}
#define Require(...) RequireImpl((__VA_ARGS__),__LINE__)
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
    // Coincident placements prove that scene binding is identity-based.
    talvisota::world::WorldPlacement crate;
    crate.id = "placement.test-crate";
    crate.category = talvisota::world::PlacementCategory::Prop;
    crate.modelAssetId = "model.test-crate";
    crate.position = tree.position;
    region.placements.push_back(crate);

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
    instance.placementId = tree.id;
    scene.instances.push_back(instance);
    RenderInstance crateInstance = instance;
    crateInstance.placementId = crate.id;
    scene.instances.push_back(crateInstance);

    DynamicWorldSystem dynamics;
    std::string error;
    Require(dynamics.Initialize(region, scene, error));
    Require(dynamics.Objects().size() == 2U);
    Require(dynamics.Objects().front().instanceIndices.size() == 1U);
    Require(dynamics.Objects().back().instanceIndices.size() == 1U);
    Require(dynamics.Objects().front().instanceIndices.front() == 0U);
    Require(dynamics.Objects().back().instanceIndices.front() == 1U);
    Require(dynamics.Objects().back().anchored && dynamics.Objects().back().sleeping);

    const auto authored = scene.instances.front().world;
    const auto crateAuthored = scene.instances.back().world;
    Require(dynamics.Update(region, scene, 1.0F / 60.0F, 1.0,
        {0.914F, 0.406F}, 0.8F, error));
    Require(scene.instances.front().world != authored);
    Require(scene.instances.back().world == crateAuthored);

    Require(dynamics.ApplyImpulse(tree.id, {0.0F, 8.0F, 0.0F}, {0.5F, 0.4F, 0.2F}, error));
    Require(!dynamics.Objects().front().anchored);
    for (int frame = 0; frame < 30; ++frame)
        Require(dynamics.Update(region, scene, 1.0F / 60.0F,
            1.0 + static_cast<double>(frame) / 60.0, {0.914F, 0.406F}, 0.1F, error));
    Require(scene.instances.front().world[13] > tree.position[1]);
    Require(std::abs(dynamics.Objects().front().rotation[1]) > 0.001F);

    const auto savePath = std::filesystem::temp_directory_path() / "talvisota-dynamic-world-test.state";
    Require(dynamics.Save(savePath, error));
    Require(dynamics.ApplyDamage(tree.id, 0.25F, error));
    Require(dynamics.Save(savePath, error)); // Exercise replacement of an existing save.
    DynamicWorldSystem restored;
    SceneSnapshot restoredScene = scene;
    Require(restored.Initialize(region, restoredScene, error));
    Require(restored.Load(savePath, error));
    Require(!restored.Objects().front().anchored);
    Require(restored.Objects().back().anchored);
    Require(std::abs(restored.Objects().front().integrity - 0.75F) < 0.0001F);
    Require(std::abs(restored.Objects().front().position[1] -
        dynamics.Objects().front().position[1]) < 0.0001F);

    Require(restored.ApplyDamage(tree.id, 0.5F, error));
    Require(std::abs(restored.Objects().front().integrity - 0.25F) < 0.0001F);
    Require(!restored.ApplyDamage("missing", 0.1F, error));

    const auto legacyPath = savePath.string() + ".legacy";
    {
        std::ofstream legacy(legacyPath, std::ios::trunc);
        Require(static_cast<bool>(legacy));
        legacy << "TALVISOTA_WORLD_STATE 1\n" << dynamics.Objects().size() << '\n';
        legacy << std::setprecision(9);
        for (const auto& object : dynamics.Objects())
        {
            legacy << std::quoted(object.id) << ' ' << object.integrity << ' ' << object.mass << ' '
                << object.anchored << ' ' << object.sleeping << ' ' << object.sleepSeconds;
            for (const float value : object.position) legacy << ' ' << value;
            for (const float value : object.velocity) legacy << ' ' << value;
            for (const float value : object.angularVelocity) legacy << ' ' << value;
            legacy << '\n';
        }
        legacy.close();
        Require(static_cast<bool>(legacy));
    }
    DynamicWorldSystem legacyRestored;
    SceneSnapshot legacyScene = scene;
    Require(legacyRestored.Initialize(region, legacyScene, error));
    Require(legacyRestored.Load(legacyPath, error));
    Require(legacyRestored.Objects().front().rotation == std::array<float, 3>{});
    std::error_code ec;
    std::filesystem::remove(savePath, ec);
    std::filesystem::remove(savePath.string() + ".tmp", ec);
    std::filesystem::remove(savePath.string() + ".bak", ec);
    std::filesystem::remove(legacyPath, ec);
    return 0;
}
