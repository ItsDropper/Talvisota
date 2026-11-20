#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/AuthoredDefinition.hpp"
#include "talvisota/content/MaterialDefinition.hpp"
#include "talvisota/content/WorldDefinition.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace talvisota::content;

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("talvisota-content-tests-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }
    void Write(const std::filesystem::path& path, const std::string& contents)
    {
        std::filesystem::create_directories((path_ / path).parent_path());
        std::ofstream output(path_ / path, std::ios::binary);
        output << contents;
        Require(static_cast<bool>(output), "Could not write test fixture.");
    }
    void Manifest(const std::string& contents) { Write("catalog.tmanifest", contents); }
private:
    std::filesystem::path path_;
};

bool Load(const TemporaryDirectory& directory, talvisota::content::ContentRegistry& registry,
          std::vector<talvisota::content::ContentDiagnostic>& diagnostics)
{
    return talvisota::content::LoadContentManifest(directory.Path() / "catalog.tmanifest",
                                                   directory.Path(), registry, diagnostics);
}

void TestDependencyOrderAndStableLookup()
{
    TemporaryDirectory directory;
    directory.Write("models/tree.obj", "mesh");
    directory.Write("materials/tree.tmat", "{}");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tworld.tree\tmodel\tmodels/tree.obj\trequired\tmaterial.tree\tproject\tproject\n"
        "asset\tmaterial.tree\tmaterial\tmaterials/tree.tmat\trequired\t-\tproject\tproject\n");
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> diagnostics;
    Require(Load(directory, registry, diagnostics), "Valid manifest should load.");
    Require(registry.Assets().size() == 2U, "Both manifest records should be retained.");
    Require(registry.Find("world.tree") != nullptr, "Stable ID lookup should resolve.");
    Require(registry.Find("missing") == nullptr, "Unknown stable IDs should not resolve.");
    Require(registry.LoadOrder().size() == 2U, "Both assets should be in load order.");
    const auto& order = registry.LoadOrder();
    Require(registry.Assets()[order[0]].id == "material.tree", "Dependencies must load before dependents.");
    Require(registry.Assets()[order[1]].id == "world.tree", "Dependent should follow its dependency.");
}

void TestDuplicateIdsAreRejectedAtomically()
{
    TemporaryDirectory directory;
    directory.Write("a.json", "{}");
    directory.Write("b.json", "{}");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tduplicate\tdefinition\ta.json\trequired\t-\tproject\tproject\n"
        "asset\tduplicate\tdefinition\tb.json\trequired\t-\tproject\tproject\n");
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> diagnostics;
    directory.Write("valid.json", "{}");
    directory.Manifest("talvisota-content-manifest\t1\nasset\tstable.asset\tdefinition\tvalid.json\trequired\t-\tproject\tproject\n");
    Require(Load(directory, registry, diagnostics), "A valid registry should load before atomicity is checked.");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tduplicate\tdefinition\ta.json\trequired\t-\tproject\tproject\n"
        "asset\tduplicate\tdefinition\tb.json\trequired\t-\tproject\tproject\n");
    Require(!Load(directory, registry, diagnostics), "Duplicate IDs must fail.");
    Require(!diagnostics.empty(), "Duplicate ID failure must include a diagnostic.");
    Require(diagnostics.front().file == directory.Path() / "catalog.tmanifest", "Diagnostics must identify the exact manifest file.");
    Require(diagnostics.front().line == 3U, "Duplicate diagnostics must identify the offending line.");
    Require(registry.Assets().size() == 1U && registry.Find("stable.asset") != nullptr,
            "Failed reloads must preserve the previously published registry.");
}

void TestTraversalAndUnsupportedExtensionsAreRejected()
{
    TemporaryDirectory directory;
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tbad.path\tmodel\t../outside.obj\trequired\t-\tproject\tproject\n");
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> diagnostics;
    Require(!Load(directory, registry, diagnostics), "Parent traversal must fail.");

    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tbad.extension\tmodel\tmodels/tree.png\trequired\t-\tproject\tproject\n");
    diagnostics.clear();
    Require(!Load(directory, registry, diagnostics), "Mismatched file extensions must fail.");
}

void TestMissingRequiredFileAndDependencyAreRejected()
{
    TemporaryDirectory directory;
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tworld.main\tdefinition\tworld/main.json\trequired\tasset.missing\tproject\tproject\n");
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> diagnostics;
    Require(!Load(directory, registry, diagnostics), "Missing required file and dependency must fail.");
    Require(diagnostics.size() >= 2U, "Both independent validation failures should be reported.");
}

void TestOptionalMissingFileIsAllowed()
{
    TemporaryDirectory directory;
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tui.optional\ttexture\tui/optional.png\toptional\t-\tproject\tproject\n");
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> diagnostics;
    Require(Load(directory, registry, diagnostics), "Missing optional assets should not fail the manifest.");
}

void TestProvenanceRejectsControlCharacters()
{
    TemporaryDirectory directory;
    std::string manifest =
        "talvisota-content-manifest\t1\n"
        "asset\tasset.safe\tdefinition\tassets/safe.json\trequired\t-\tproject";
    manifest.push_back('\0');
    manifest += "source\tproject\n";
    directory.Write("assets/safe.json", "{}");
    directory.Manifest(manifest);
    ContentRegistry registry;
    std::vector<ContentDiagnostic> diagnostics;
    Require(!Load(directory, registry, diagnostics),
            "Embedded NUL in provenance metadata must be rejected.");
    Require(!diagnostics.empty() && diagnostics.front().field == "provenance",
            "Invalid provenance must produce a provenance diagnostic.");
}

void TestDependencyCyclesAreRejected()
{
    TemporaryDirectory directory;
    directory.Write("a.json", "{}");
    directory.Write("b.json", "{}");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tasset.a\tdefinition\ta.json\trequired\tasset.b\tproject\tproject\n"
        "asset\tasset.b\tdefinition\tb.json\trequired\tasset.a\tproject\tproject\n");
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> diagnostics;
    Require(!Load(directory, registry, diagnostics), "Dependency cycles must fail.");
    Require(!diagnostics.empty(), "Cycle rejection must explain the error.");
}

void TestTypedMaterialDefinitionAndTextureReferences()
{
    TemporaryDirectory directory;
    directory.Write("materials/snow.tmat",
        "talvisota-material\t1\n"
        "id = material.surface.snow\n"
        "base_color = 0.82, 0.86, 0.9\n"
        "roughness = 0.91\n"
        "metallic = 0.0\n"
        "normal_scale = 0.7\n"
        "occlusion_strength = 0.8\n"
        "alpha_mode = opaque\n"
        "double_sided = false\n"
        "base_color_texture = texture.surface.snow_albedo\n"
        "normal_texture = texture.surface.snow_normal\n"
        "orm_texture = texture.surface.snow_orm\n");
    directory.Write("textures/snow-albedo.png", "albedo");
    directory.Write("textures/snow-normal.png", "normal");
    directory.Write("textures/snow-orm.png", "orm");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tmaterial.surface.snow\tmaterial\tmaterials/snow.tmat\trequired\ttexture.surface.snow_albedo,texture.surface.snow_normal,texture.surface.snow_orm\tproject\tproject\n"
        "asset\ttexture.surface.snow_albedo\ttexture\ttextures/snow-albedo.png\trequired\t-\tproject\tproject\n"
        "asset\ttexture.surface.snow_normal\ttexture\ttextures/snow-normal.png\trequired\t-\tproject\tproject\n"
        "asset\ttexture.surface.snow_orm\ttexture\ttextures/snow-orm.png\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> registryDiagnostics;
    Require(Load(directory, registry, registryDiagnostics), "Material and texture manifest should load.");
    MaterialDefinition material;
    std::vector<MaterialDiagnostic> diagnostics;
    Require(LoadMaterialDefinition(directory.Path() / "materials/snow.tmat", registry, material, diagnostics),
            "Valid typed material definition should load.");
    Require(material.id == "material.surface.snow", "Material stable ID should be retained.");
    Require(material.roughness == 0.91F && material.metallic == 0.0F, "PBR scalar values should be parsed exactly.");
    Require(material.normalTexture == "texture.surface.snow_normal", "Texture references should be retained.");
    Require(material.alphaMode == MaterialAlphaMode::Opaque && !material.doubleSided,
            "Surface presentation flags should be parsed.");
}

void TestMaterialRejectsInvalidPbrValuesAndWrongTextureKinds()
{
    TemporaryDirectory directory;
    directory.Write("materials/bad.tmat",
        "talvisota-material\t1\n"
        "id = material.surface.bad\n"
        "base_color = 1.0, 1.0, 1.0\n"
        "roughness = 1.4\n"
        "metallic = 0.0\n"
        "normal_texture = material.other\n");
    directory.Write("materials/other.tmat",
        "talvisota-material\t1\n"
        "id = material.other\n"
        "base_color = 1, 1, 1\n"
        "roughness = 0.5\n"
        "metallic = 0\n");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tmaterial.surface.bad\tmaterial\tmaterials/bad.tmat\trequired\tmaterial.other\tproject\tproject\n"
        "asset\tmaterial.other\tmaterial\tmaterials/other.tmat\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> registryDiagnostics;
    Require(Load(directory, registry, registryDiagnostics), "Manifest for invalid material test should load.");
    MaterialDefinition material;
    material.id = "unchanged";
    std::vector<MaterialDiagnostic> diagnostics;
    Require(!LoadMaterialDefinition(directory.Path() / "materials/bad.tmat", registry, material, diagnostics),
            "Out-of-range PBR and wrong texture-kind references must fail.");
    Require(material.id == "unchanged", "Failed material loads must not publish partial definitions.");
    Require(diagnostics.size() >= 2U, "Independent material errors should both be reported.");
}


void TestTypedWorldDefinitionAndCoordinateBounds()
{
    TemporaryDirectory directory;
    directory.Write("worlds/central-finland.tworld",
        "talvisota-world\t1\n"
        "id = world.central_finland.test\n"
        "name = Central Finland Test Region\n"
        "unit = meter\n"
        "up_axis = y\n"
        "north_axis = +z\n"
        "handedness = right\n"
        "bounds_min = -500, -20, -500\n"
        "bounds_max = 500, 80, 500\n"
        "sea_level = 0\n");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tworld.central_finland.test\tdefinition\tworlds/central-finland.tworld\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> registryDiagnostics;
    Require(Load(directory, registry, registryDiagnostics), "World definition manifest should load.");
    WorldDefinition world;
    std::vector<WorldDiagnostic> diagnostics;
    Require(LoadWorldDefinition(directory.Path() / "worlds/central-finland.tworld", registry, world, diagnostics),
            "Valid world definition should load.");
    Require(world.id == "world.central_finland.test" && world.name == "Central Finland Test Region",
            "World identity should be retained.");
    Require(world.boundsMin[0] == -500.0F && world.boundsMax[2] == 500.0F,
            "World bounds should be parsed in metres.");
    Require(world.hasSeaLevel && world.seaLevel == 0.0F, "Optional sea level should be parsed.");
}

void TestWorldTerrainModelReferenceRequiresManifestDependency()
{
    TemporaryDirectory directory;
    directory.Write("worlds/terrain.tworld",
        "talvisota-world\t1\n"
        "id = world.terrain\n"
        "name = Terrain World\n"
        "unit = meter\n"
        "up_axis = y\n"
        "north_axis = +z\n"
        "handedness = right\n"
        "bounds_min = -10, -5, -10\n"
        "bounds_max = 10, 5, 10\n"
        "terrain_model_id = model.terrain\n");
    directory.Write("models/terrain.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tworld.terrain\tdefinition\tworlds/terrain.tworld\trequired\tmodel.terrain\tproject\tproject\n"
        "asset\tmodel.terrain\tmodel\tmodels/terrain.obj\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(Load(directory, registry, manifestDiagnostics), "World and terrain manifest should load.");
    WorldDefinition world;
    std::vector<WorldDiagnostic> diagnostics;
    Require(LoadWorldDefinition(directory.Path() / "worlds/terrain.tworld", registry, world, diagnostics),
            "Registered world terrain model dependency should validate.");
    Require(world.terrainModelId == "model.terrain", "World should retain its terrain model ID.");

    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tworld.terrain\tdefinition\tworlds/terrain.tworld\trequired\t-\tproject\tproject\n"
        "asset\tmodel.terrain\tmodel\tmodels/terrain.obj\trequired\t-\tproject\tproject\n");
    Require(Load(directory, registry, manifestDiagnostics), "Manifest without world dependency should load structurally.");
    diagnostics.clear();
    Require(!LoadWorldDefinition(directory.Path() / "worlds/terrain.tworld", registry, world, diagnostics),
            "World terrain references omitted from manifest dependencies must fail.");
    Require(!diagnostics.empty() && diagnostics.front().field == "terrain_model_id",
            "Missing terrain dependency must produce a field-specific diagnostic.");

    std::filesystem::remove(directory.Path() / "models/terrain.obj");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tworld.terrain\tdefinition\tworlds/terrain.tworld\trequired\tmodel.terrain\tproject\tproject\n"
        "asset\tmodel.terrain\tmodel\tmodels/terrain.obj\toptional\t-\tproject\tproject\n");
    Require(Load(directory, registry, manifestDiagnostics),
            "An absent optional model may remain in the registry when not referenced.");
    diagnostics.clear();
    Require(!LoadWorldDefinition(directory.Path() / "worlds/terrain.tworld", registry, world, diagnostics),
            "A world must reject an absent optional model that it explicitly references.");
    Require(!diagnostics.empty() && diagnostics.front().field == "terrain_model_id",
            "An absent referenced terrain model must identify the terrain_model_id field.");
}

void TestWorldRejectsInvertedBoundsAndCoordinateMismatchAtomically()
{
    TemporaryDirectory directory;
    directory.Write("worlds/invalid.tworld",
        "talvisota-world\t1\n"
        "id = world.invalid\n"
        "name = Invalid World\n"
        "unit = centimeter\n"
        "up_axis = z\n"
        "north_axis = +z\n"
        "handedness = right\n"
        "bounds_min = 10, 0, -10\n"
        "bounds_max = 0, 20, 10\n");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tworld.invalid\tdefinition\tworlds/invalid.tworld\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> registryDiagnostics;
    Require(Load(directory, registry, registryDiagnostics), "Invalid world fixture manifest should load.");
    WorldDefinition world;
    world.id = "unchanged";
    std::vector<WorldDiagnostic> diagnostics;
    Require(!LoadWorldDefinition(directory.Path() / "worlds/invalid.tworld", registry, world, diagnostics),
            "Invalid units, axes, and inverted bounds must fail.");
    Require(world.id == "unchanged", "Failed world loads must not publish partial definitions.");
    Require(diagnostics.size() >= 3U, "Independent world validation errors should be reported.");
}


void TestTypedMissionFactionCharacterAndEquipmentDefinitions()
{
    TemporaryDirectory directory;
    directory.Write("worlds/test.tworld",
        "talvisota-world\t1\n"
        "id = world.test\n"
        "name = Test World\n"
        "unit = meter\n"
        "up_axis = y\n"
        "north_axis = +z\n"
        "handedness = right\n"
        "bounds_min = -10, -10, -10\n"
        "bounds_max = 10, 10, 10\n");
    directory.Write("factions/finnish.tfaction",
        "talvisota-faction\t1\n"
        "id = faction.finnish\n"
        "name = Finnish Forces\n"
        "side = finnish\n");
    directory.Write("characters/player.tcharacter",
        "talvisota-character\t1\n"
        "id = character.player\n"
        "name = Player Character\n"
        "faction_id = faction.finnish\n"
        "model_id = model.player\n"
        "portrait_texture_id = texture.portrait\n");
    directory.Write("equipment/uniform.tequipment",
        "talvisota-equipment\t1\n"
        "id = equipment.uniform\n"
        "name = Winter Uniform\n"
        "category = clothing\n"
        "model_id = model.player\n"
        "icon_texture_id = texture.portrait\n");
    directory.Write("regions/test.tregion", "region fixture");
    directory.Write("missions/first.tmission",
        "talvisota-mission\t1\n"
        "id = mission.first\n"
        "name = First Mission\n"
        "world_id = world.test\n"
        "region_id = region.test\n"
        "player_character_id = character.player\n"
        "faction_id = faction.finnish\n"
        "objective = objective.inspect-well -> prop.well.farmstead\n"
        "objective = objective.inspect-woodpile -> prop.woodpile.west\n"
        "objective = objective.report-church -> church.sakkola\n");
    directory.Write("models/player.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    directory.Write("textures/portrait.bmp", "fixture");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tworld.test\tdefinition\tworlds/test.tworld\trequired\t-\tproject\tproject\n"
        "asset\tfaction.finnish\tdefinition\tfactions/finnish.tfaction\trequired\t-\tproject\tproject\n"
        "asset\tmodel.player\tmodel\tmodels/player.obj\trequired\t-\tproject\tproject\n"
        "asset\ttexture.portrait\ttexture\ttextures/portrait.bmp\trequired\t-\tproject\tproject\n"
        "asset\tcharacter.player\tdefinition\tcharacters/player.tcharacter\trequired\tfaction.finnish,model.player,texture.portrait\tproject\tproject\n"
        "asset\tequipment.uniform\tdefinition\tequipment/uniform.tequipment\trequired\tmodel.player,texture.portrait\tproject\tproject\n"
        "asset\tregion.test\tdefinition\tregions/test.tregion\trequired\tworld.test\tproject\tproject\n"
        "asset\tmission.first\tdefinition\tmissions/first.tmission\trequired\tworld.test,region.test,character.player,faction.finnish\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> registryDiagnostics;
    if (!Load(directory, registry, registryDiagnostics))
    {
        for (const auto& diagnostic : registryDiagnostics)
            std::cerr << diagnostic.file.generic_string() << ':' << diagnostic.line
                      << " [" << diagnostic.field << "]: " << diagnostic.message << '\n';
        throw std::runtime_error("Typed definition manifest should load.");
    }

    for (const std::string_view relativePath : {
        "factions/finnish.tfaction", "characters/player.tcharacter",
        "equipment/uniform.tequipment", "missions/first.tmission"})
    {
        AuthoredDefinition definition;
        std::vector<AuthoredDefinitionDiagnostic> diagnostics;
        Require(LoadAuthoredDefinition(directory.Path() / relativePath, registry, definition, diagnostics),
                "Valid typed authored definition should load.");
    }

    AuthoredDefinition mission;
    std::vector<AuthoredDefinitionDiagnostic> missionDiagnostics;
    Require(LoadAuthoredDefinition(directory.Path() / "missions/first.tmission",
                                   registry, mission, missionDiagnostics),
            "Mission with ordered interaction objectives should load.");
    Require(mission.regionId == "region.test" && mission.objectives.size() == 3U,
            "Mission should retain its region and all authored objectives.");
    Require(mission.objectives[0].id == "objective.inspect-well" &&
            mission.objectives[0].interactionTargetId == "prop.well.farmstead" &&
            mission.objectives[1].interactionTargetId == "prop.woodpile.west" &&
            mission.objectives[2].interactionTargetId == "church.sakkola",
            "Mission objectives must preserve authored order and stable placement target IDs.");

    directory.Write("missions/first.tmission",
        "talvisota-mission\t1\n"
        "id = mission.first\n"
        "name = First Mission\n"
        "world_id = world.test\n"
        "region_id = region.test\n"
        "player_character_id = character.player\n"
        "faction_id = faction.finnish\n"
        "objective = objective.inspect-well -> prop.well.farmstead\n"
        "objective = objective.inspect-woodpile -> prop.well.farmstead\n");
    Require(!LoadAuthoredDefinition(directory.Path() / "missions/first.tmission",
                                    registry, mission, missionDiagnostics),
            "Duplicate mission interaction targets must be rejected.");
    Require(mission.objectives.size() == 3U && mission.objectives[0].id == "objective.inspect-well",
            "Rejected mission reload must preserve the previously published definition.");

    directory.Write("missions/first.tmission",
        "talvisota-mission\t1\n"
        "id = mission.first\n"
        "name = First Mission\n"
        "world_id = world.test\n"
        "region_id = region.test\n"
        "player_character_id = character.player\n"
        "faction_id = faction.finnish\n"
        "objective = objective.inspect-well -> prop.well.farmstead\n"
        "objective = objective.inspect-well -> prop.woodpile.west\n");
    Require(!LoadAuthoredDefinition(directory.Path() / "missions/first.tmission",
                                    registry, mission, missionDiagnostics),
            "Duplicate mission objective IDs must be rejected.");

    directory.Write("missions/first.tmission",
        "talvisota-mission\t1\n"
        "id = mission.first\n"
        "name = First Mission\n"
        "world_id = world.test\n"
        "region_id = region.test\n"
        "player_character_id = character.player\n"
        "faction_id = faction.finnish\n");
    Require(!LoadAuthoredDefinition(directory.Path() / "missions/first.tmission",
                                    registry, mission, missionDiagnostics),
            "Mission definitions without objectives must be rejected.");

    directory.Write("missions/first.tmission",
        "talvisota-mission\t1\n"
        "id = mission.first\n"
        "name = First Mission\n"
        "world_id = world.test\n"
        "region_id = region.test\n"
        "player_character_id = character.player\n"
        "faction_id = faction.finnish\n"
        "objective = malformed-objective-record\n");
    Require(!LoadAuthoredDefinition(directory.Path() / "missions/first.tmission",
                                    registry, mission, missionDiagnostics),
            "Malformed mission objective records must be rejected.");
}

void TestTypedDefinitionRejectsUndeclaredDependenciesAtomically()
{
    TemporaryDirectory directory;
    directory.Write("factions/finnish.tfaction",
        "talvisota-faction\t1\nid = faction.finnish\nname = Finnish\nside = finnish\n");
    directory.Write("characters/player.tcharacter",
        "talvisota-character\t1\nid = character.player\nname = Player\nfaction_id = faction.finnish\n");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tfaction.finnish\tdefinition\tfactions/finnish.tfaction\trequired\t-\tproject\tproject\n"
        "asset\tcharacter.player\tdefinition\tcharacters/player.tcharacter\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> registryDiagnostics;
    Require(Load(directory, registry, registryDiagnostics), "Manifest should load before reference dependency validation.");
    AuthoredDefinition definition;
    definition.id = "unchanged";
    std::vector<AuthoredDefinitionDiagnostic> diagnostics;
    Require(!LoadAuthoredDefinition(directory.Path() / "characters/player.tcharacter",
                                    registry, definition, diagnostics),
            "References omitted from the dependency list must fail.");
    Require(definition.id == "unchanged" && !diagnostics.empty(),
            "Failed authored-definition loads must preserve output and diagnose the dependency.");
}

} // namespace

int main()
{
    try
    {
        TestDependencyOrderAndStableLookup();
        TestDuplicateIdsAreRejectedAtomically();
        TestTraversalAndUnsupportedExtensionsAreRejected();
        TestMissingRequiredFileAndDependencyAreRejected();
        TestOptionalMissingFileIsAllowed();
        TestProvenanceRejectsControlCharacters();
        TestDependencyCyclesAreRejected();
        TestTypedMaterialDefinitionAndTextureReferences();
        TestMaterialRejectsInvalidPbrValuesAndWrongTextureKinds();
        TestTypedWorldDefinitionAndCoordinateBounds();
        TestWorldTerrainModelReferenceRequiresManifestDependency();
        TestWorldRejectsInvertedBoundsAndCoordinateMismatchAtomically();
        TestTypedMissionFactionCharacterAndEquipmentDefinitions();
        TestTypedDefinitionRejectsUndeclaredDependenciesAtomically();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Content registry test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All content registry tests passed.\n";
    return 0;
}
