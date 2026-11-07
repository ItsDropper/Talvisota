#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/VantaAssetPackage.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace talvisota::content;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

const std::string& WorldText()
{
    static const std::string value =
        "talvisota-world\\t1\\n"
        "id = world.fixture\\n"
        "name = Package Fixture\\n"
        "unit = meter\\n"
        "up_axis = y\\n"
        "north_axis = +z\\n"
        "handedness = right\\n"
        "bounds_min = -1, -1, -1\\n"
        "bounds_max = 1, 1, 1\\n";
    return value;
}

class Fixture final
{
public:
    Fixture()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() / ("talvisota-package-" + std::to_string(stamp));
        package_ = root_.parent_path() / (root_.filename().string() + "-output");
        std::filesystem::create_directories(root_);
        Write("definitions/world.tworld", "fixture definition\n");
        Write("catalog.tmanifest",
            "talvisota-content-manifest\t1\n"
            "asset\tworld.fixture\tdefinition\tdefinitions/world.tworld\trequired\t-\tproject\tproject\n"
            "asset\tworld.optional\tdefinition\tdefinitions/optional.tworld\toptional\t-\tproject\tproject\n");
    }
    ~Fixture()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        std::filesystem::remove_all(package_, ignored);
    }
    void Write(const std::filesystem::path& relative, const std::string& content)
    {
        std::filesystem::create_directories((root_ / relative).parent_path());
        std::ofstream file(root_ / relative, std::ios::binary);
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        Require(static_cast<bool>(file), "Could not write package fixture.");
    }
    const std::filesystem::path& Root() const { return root_; }
    const std::filesystem::path& Package() const { return package_; }
private:
    std::filesystem::path root_;
    std::filesystem::path package_;
};

void TestBuildsAndVerifiesDeterministicInventory()
{
    Fixture fixture;
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(fixture.Root() / "catalog.tmanifest", fixture.Root(),
                                registry, manifestDiagnostics),
            "Package fixture manifest should load.");
    AssetPackageSummary built;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(BuildAssetPackage(registry, fixture.Package(), built, diagnostics),
            "Package builder should publish a valid package.");
    Require(built.assetCount == 1U && built.totalBytes == std::string("fixture definition\n").size(),
            "Package should include the required file and omit the absent optional file.");
    AssetPackageSummary verified;
    Require(ValidateAssetPackage(fixture.Package(), verified, diagnostics),
            "Published package should pass full inventory and SHA-256 verification.");
    Require(verified.assetCount == built.assetCount && verified.totalBytes == built.totalBytes,
            "Build and verification summaries should match.");
    std::string firstText;
    {
        std::ifstream firstInventory(fixture.Package() / "package.tmanifest", std::ios::binary);
        firstText.assign(std::istreambuf_iterator<char>(firstInventory),
                         std::istreambuf_iterator<char>());
    }
    Require(firstText.find("\tproject\tproject\n") != std::string::npos,
            "Package inventory must preserve source and licence metadata.");
    AssetPackageSummary rebuilt;
    Require(BuildAssetPackage(registry, fixture.Package(), rebuilt, diagnostics),
            "Rebuilding an existing package should replace it after validation.");
    std::string secondText;
    {
        std::ifstream secondInventory(fixture.Package() / "package.tmanifest", std::ios::binary);
        secondText.assign(std::istreambuf_iterator<char>(secondInventory),
                          std::istreambuf_iterator<char>());
    }
    Require(firstText == secondText, "Identical inputs must produce byte-identical package inventories.");
}

void TestRejectsUnindexedPackageFile()
{
    Fixture fixture;
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(fixture.Root() / "catalog.tmanifest", fixture.Root(),
                                registry, manifestDiagnostics),
            "Unindexed-file fixture manifest should load.");
    AssetPackageSummary built;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(BuildAssetPackage(registry, fixture.Package(), built, diagnostics),
            "Package builder should create unindexed-file fixture.");
    std::ofstream extra(fixture.Package() / "extra.bin", std::ios::binary);
    extra << "unindexed";
    extra.close();
    AssetPackageSummary verified;
    Require(!ValidateAssetPackage(fixture.Package(), verified, diagnostics),
            "Package verification must reject unindexed files.");
    Require(!diagnostics.empty() && diagnostics.front().field == "inventory",
            "Unindexed files should report an inventory diagnostic.");
}

void TestRejectsModifiedPackageAsset()
{
    Fixture fixture;
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(fixture.Root() / "catalog.tmanifest", fixture.Root(),
                                registry, manifestDiagnostics),
            "Tamper fixture manifest should load.");
    AssetPackageSummary built;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(BuildAssetPackage(registry, fixture.Package(), built, diagnostics),
            "Package builder should create tamper fixture.");
    {
        std::ofstream file(fixture.Package() / "definitions/world.tworld",
                           std::ios::binary | std::ios::trunc);
        file << "modified";
    }
    AssetPackageSummary verified;
    Require(!ValidateAssetPackage(fixture.Package(), verified, diagnostics),
            "Package verification must reject changed asset bytes.");
    Require(!diagnostics.empty() && diagnostics.front().field == "integrity",
            "Changed package bytes should report an integrity diagnostic.");
}
}

int main()
{
    try
    {
        TestBuildsAndVerifiesDeterministicInventory();
        TestRejectsUnindexedPackageFile();
        TestRejectsModifiedPackageAsset();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Asset package test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All asset package tests passed.\n";
    return 0;
}
