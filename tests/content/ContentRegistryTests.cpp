#include "talvisota/content/ContentRegistry.hpp"

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
    directory.Write("materials/tree.json", "{}");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tworld.tree\tmodel\tmodels/tree.obj\trequired\tmaterial.tree\tproject\tproject\n"
        "asset\tmaterial.tree\tmaterial\tmaterials/tree.json\trequired\t-\tproject\tproject\n");
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
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Content registry test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All content registry tests passed.\n";
    return 0;
}
