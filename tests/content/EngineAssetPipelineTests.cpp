#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/VantaAssetPackage.hpp"
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

class PipelineFixture final
{
public:
    PipelineFixture()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() / ("talvisota-pipeline-" + std::to_string(stamp));
        package_ = root_.parent_path() / (root_.filename().string() + "-package");
        std::filesystem::create_directories(root_);
        Write("models/tree.obj",
            "mtllib tree.mtl\n"
            "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        Write("models/tree.mtl",
            "newmtl bark\nKd 0.5 0.4 0.3\nmap_Kd ../textures/albedo.bmp\n");
        Write("models/embedded.gltf", R"json({
  "asset":{"version":"2.0"},
  "scene":0,
  "scenes":[{"nodes":[0]}],
  "nodes":[{"mesh":0}],
  "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],
  "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"roughnessFactor":0.8,"metallicFactor":0.0}}],
  "textures":[{"source":0}],
  "images":[{"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAHUlEQVR42gESAO3/AOHr+P/r8vr/ANfk9f/m7/r/hy0PF2JNGOQAAAAASUVORK5CYII="}],
  "buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAgD8AAAAAAACAPwAAgD8AAAAAAACAPwAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAgD8AAIA/AAABAAIA","byteLength":102}],
  "bufferViews":[
    {"buffer":0,"byteOffset":0,"byteLength":36},
    {"buffer":0,"byteOffset":36,"byteLength":36},
    {"buffer":0,"byteOffset":72,"byteLength":24},
    {"buffer":0,"byteOffset":96,"byteLength":6}
  ],
  "accessors":[
    {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
    {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
    {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
    {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}
  ]
})json");
        WriteBmp("textures/albedo.bmp");
        WritePcmWav("audio/wind.wav");
        WriteProcessedTexture("textures/ground.ttex");
        Write("catalog.tmanifest",
            "talvisota-content-manifest\t1\n"
            "asset\taudio.wind\taudio\taudio/wind.wav\trequired\t-\tproject\tproject\n"
            "asset\tmaterial.tree\tmaterial\tmodels/tree.mtl\trequired\t-\tproject\tproject\n"
            "asset\tmodel.tree\tmodel\tmodels/tree.obj\trequired\tmaterial.tree,texture.albedo\tproject\tproject\n"
            "asset\tmodel.embedded\tmodel\tmodels/embedded.gltf\trequired\t-\tproject\tproject\n"
            "asset\ttexture.albedo\ttexture\ttextures/albedo.bmp\trequired\t-\tproject\tproject\n"
            "asset\ttexture.ground\ttexture\ttextures/ground.ttex\trequired\t-\tproject\tproject\n");
    }
    ~PipelineFixture()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        std::filesystem::remove_all(package_, ignored);
    }
    void Write(const std::filesystem::path& relative, const std::string& value)
    {
        std::filesystem::create_directories((root_ / relative).parent_path());
        std::ofstream file(root_ / relative, std::ios::binary);
        file.write(value.data(), static_cast<std::streamsize>(value.size()));
        Require(static_cast<bool>(file), "Could not write pipeline fixture.");
    }
    void WriteBmp(const std::filesystem::path& relative)
    {
        std::filesystem::create_directories((root_ / relative).parent_path());
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
        std::ofstream file(root_ / relative, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(static_cast<bool>(file), "Could not write pipeline BMP.");
    }
    void WritePcmWav(const std::filesystem::path& relative)
    {
        std::filesystem::create_directories((root_ / relative).parent_path());
        const std::vector<std::uint8_t> bytes = {
            'R','I','F','F', 38,0,0,0, 'W','A','V','E',
            'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
            0x40,0x1f,0,0, 0x80,0x3e,0,0, 2,0, 16,0,
            'd','a','t','a', 2,0,0,0, 0,0
        };
        std::ofstream file(root_ / relative, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(static_cast<bool>(file), "Could not write pipeline WAV.");
    }
    void WriteProcessedTexture(const std::filesystem::path& relative)
    {
        std::filesystem::create_directories((root_ / relative).parent_path());
        std::vector<vanta::assets::TextureMip> mips(1);
        mips[0].width = 1U;
        mips[0].height = 1U;
        mips[0].rgba = {160U, 170U, 180U, 255U};
        std::string error;
        Require(vanta::assets::WriteProcessedTexture(root_ / relative, mips,
                    vanta::assets::TextureColorSpace::Srgb, error),
                "Could not write processed terrain texture fixture.");
    }
    const std::filesystem::path& Root() const { return root_; }
    const std::filesystem::path& Package() const { return package_; }
private:
    std::filesystem::path root_;
    std::filesystem::path package_;
};

void TestCompleteAssetImportValidationAndPackagingPath()
{
    PipelineFixture fixture;
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(fixture.Root() / "catalog.tmanifest", fixture.Root(),
                                registry, manifestDiagnostics),
            "Representative asset pipeline manifest should load.");

    EngineAssetSummary media;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(ValidateEngineAssets(registry, media, diagnostics),
            "OBJ/MTL, external image, processed texture, and WAV assets should all validate.");
    Require(media.importedModels == 2U && media.decodedTextures == 3U && media.decodedAudio == 1U,
            "Media validation summary should account for every unique source asset.");

    AssetPackageSummary built;
    Require(BuildAssetPackage(registry, fixture.Package(), built, diagnostics),
            "Complete validated asset set should package successfully.");
    Require(built.assetCount == 6U, "Package should contain all five registered source assets.");

    AssetPackageSummary verified;
    Require(ValidateAssetPackage(fixture.Package(), verified, diagnostics),
            "Packaged representative asset set should pass full SHA-256 verification.");
    Require(verified.assetCount == built.assetCount && verified.totalBytes == built.totalBytes,
            "Package revalidation summary should match the build result.");
}
}

int main()
{
    try
    {
        TestCompleteAssetImportValidationAndPackagingPath();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "End-to-end asset pipeline test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "End-to-end asset pipeline test passed.\n";
    return 0;
}
