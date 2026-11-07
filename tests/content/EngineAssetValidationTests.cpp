#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/VantaAssetValidation.hpp"
#include "vanta/assets/TextureProcessing.hpp"

#include <chrono>
#include <cstdint>
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
    if (!condition) throw std::runtime_error(message);
}

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("talvisota-engine-assets-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    void Write(const std::filesystem::path& relative, const std::string& value)
    {
        std::filesystem::create_directories((path_ / relative).parent_path());
        std::ofstream file(path_ / relative, std::ios::binary);
        file << value;
        Require(static_cast<bool>(file), "Could not write asset fixture.");
    }
    void WriteBmp(const std::filesystem::path& relative)
    {
        std::filesystem::create_directories((path_ / relative).parent_path());
        std::vector<std::uint8_t> bytes(58U, 0U);
        bytes[0] = 'B'; bytes[1] = 'M';
        const auto put32 = [&bytes](std::size_t offset, std::uint32_t value)
        {
            for (std::size_t index = 0; index < 4U; ++index)
                bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU);
        };
        bytes[2] = 58U;
        put32(10U, 54U);
        put32(14U, 40U);
        put32(18U, 1U);
        put32(22U, 1U);
        bytes[26U] = 1U;
        bytes[28U] = 24U;
        put32(34U, 4U);
        bytes[54U] = 32U; bytes[55U] = 96U; bytes[56U] = 180U;
        std::ofstream file(path_ / relative, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(static_cast<bool>(file), "Could not write BMP fixture.");
    }
    void Manifest(const std::string& value) { Write("catalog.tmanifest", value); }
    [[nodiscard]] const std::filesystem::path& Path() const { return path_; }
private:
    std::filesystem::path path_;
};

void TestImportsRegisteredObjAndDecodesRegisteredTexture()
{
    TemporaryDirectory directory;
    directory.Write("models/tree.obj",
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    directory.WriteBmp("textures/albedo.bmp");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tmodel.tree\tmodel\tmodels/tree.obj\trequired\t-\tproject\tproject\n"
        "asset\ttexture.tree.albedo\ttexture\ttextures/albedo.bmp\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(directory.Path() / "catalog.tmanifest", directory.Path(),
                                registry, manifestDiagnostics),
            "Valid engine asset fixture manifest should load.");
    EngineAssetSummary summary;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(ValidateEngineAssets(registry, summary, diagnostics),
            "VantaEngine should import OBJ geometry and decode the WIC texture.");
    Require(summary.importedModels == 1U && summary.decodedTextures == 1U,
            "Engine asset summary should count the imported model and unique decoded texture.");
}

void TestValidatesProcessedTextureContainer()
{
    TemporaryDirectory directory;
    std::vector<vanta::assets::TextureMip> mipChain(1);
    mipChain[0].width = 1U;
    mipChain[0].height = 1U;
    mipChain[0].rgba = {32U, 96U, 180U, 255U};
    std::string writeError;
    Require(vanta::assets::WriteProcessedTexture(directory.Path() / "textures/ground.ttex",
                                                  mipChain, vanta::assets::TextureColorSpace::Srgb,
                                                  writeError),
            "Could not write valid processed texture fixture.");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\ttexture.ground\ttexture\ttextures/ground.ttex\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(directory.Path() / "catalog.tmanifest", directory.Path(),
                                registry, manifestDiagnostics),
            "Processed texture manifest should load.");
    EngineAssetSummary summary;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(ValidateEngineAssets(registry, summary, diagnostics),
            "Valid TVTTEX01 container should pass engine-backed validation.");
    Require(summary.decodedTextures == 1U,
            "Processed texture should be counted as one validated texture.");
}

void TestObjMaterialLibraryMustBeRegisteredAndDeclared()
{
    TemporaryDirectory directory;
    directory.Write("models/tree.obj",
        "mtllib tree.mtl\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    directory.Write("models/tree.mtl", "newmtl bark\nKd 0.5 0.4 0.3\n");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tmodel.tree\tmodel\tmodels/tree.obj\trequired\t-\tproject\tproject\n"
        "asset\tmaterial.tree\tmaterial\tmodels/tree.mtl\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(directory.Path() / "catalog.tmanifest", directory.Path(),
                                registry, manifestDiagnostics),
            "OBJ/MTL fixture manifest should load.");
    EngineAssetSummary summary;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(!ValidateEngineAssets(registry, summary, diagnostics),
            "OBJ material library omitted from model dependencies must fail validation.");
    Require(!diagnostics.empty() && diagnostics.front().field == "materialLibrary",
            "Undeclared OBJ material library should report its dependency field.");
}

void TestMalformedTextureFailsWithoutPublishingSummary()
{
    TemporaryDirectory directory;
    directory.Write("models/tree.obj",
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    directory.Write("textures/bad.bmp", "not a bitmap");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tmodel.tree\tmodel\tmodels/tree.obj\trequired\t-\tproject\tproject\n"
        "asset\ttexture.tree.bad\ttexture\ttextures/bad.bmp\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(directory.Path() / "catalog.tmanifest", directory.Path(),
                                registry, manifestDiagnostics),
            "Malformed media fixture manifest should load structurally.");
    EngineAssetSummary summary;
    summary.decodedTextures = 42U;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(!ValidateEngineAssets(registry, summary, diagnostics),
            "Malformed registered texture must fail engine-backed asset validation.");
    Require(summary.decodedTextures == 42U && !diagnostics.empty(),
            "Failed engine validation must preserve summary output and report a diagnostic.");
}
}


void TestAudioAssetsFailClosedUntilDecoderExists()
{
    TemporaryDirectory directory;
    directory.Write("audio/ambience.wav", "unvalidated audio");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\taudio.ambience\taudio\taudio/ambience.wav\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    if (!LoadContentManifest(directory.Path() / "catalog.tmanifest", directory.Path(),
                             registry, manifestDiagnostics))
    {
        for (const auto& diagnostic : manifestDiagnostics)
            std::cerr << diagnostic.file.generic_string() << ':' << diagnostic.line
                      << " [" << diagnostic.field << "]: " << diagnostic.message << '\n';
        throw std::runtime_error("Audio manifest record should be structurally valid.");
    }
    EngineAssetSummary summary;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(!ValidateEngineAssets(registry, summary, diagnostics),
            "Audio assets must not be reported as validated without a decoder.");
    Require(!diagnostics.empty() && diagnostics.front().field == "audio",
            "Unsupported audio validation must provide a precise diagnostic.");
}

int main()
{
    try
    {
        TestImportsRegisteredObjAndDecodesRegisteredTexture();
        TestValidatesProcessedTextureContainer();
        TestObjMaterialLibraryMustBeRegisteredAndDeclared();
        TestMalformedTextureFailsWithoutPublishingSummary();
        TestAudioAssetsFailClosedUntilDecoderExists();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Engine asset validation test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All engine asset validation tests passed.\n";
    return 0;
}
