#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/MaterialDefinition.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
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
        TestDependencyCyclesAreRejected();
        TestTypedMaterialDefinitionAndTextureReferences();
        TestMaterialRejectsInvalidPbrValuesAndWrongTextureKinds();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Content registry test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All content registry tests passed.\n";
    return 0;
}
