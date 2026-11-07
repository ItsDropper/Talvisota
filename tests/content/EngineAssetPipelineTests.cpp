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
#include <string_view>
#include <vector>

namespace
{
using namespace talvisota::content;


std::string DecodeBase64(std::string_view input)
{
    const auto decode = [](char value) -> int
    {
        if (value >= 'A' && value <= 'Z') return value - 'A';
        if (value >= 'a' && value <= 'z') return value - 'a' + 26;
        if (value >= '0' && value <= '9') return value - '0' + 52;
        if (value == '+') return 62;
        if (value == '/') return 63;
        return -1;
    };
    std::string output;
    for (std::size_t offset = 0; offset + 4U <= input.size(); offset += 4U)
    {
        const int a = decode(input[offset]);
        const int b = decode(input[offset + 1U]);
        const bool padC = input[offset + 2U] == '=';
        const bool padD = input[offset + 3U] == '=';
        const int c = padC ? 0 : decode(input[offset + 2U]);
        const int d = padD ? 0 : decode(input[offset + 3U]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return {};
        const std::uint32_t packed = (static_cast<std::uint32_t>(a) << 18U) |
                                     (static_cast<std::uint32_t>(b) << 12U) |
                                     (static_cast<std::uint32_t>(c) << 6U) |
                                     static_cast<std::uint32_t>(d);
        output.push_back(static_cast<char>((packed >> 16U) & 0xffU));
        if (!padC) output.push_back(static_cast<char>((packed >> 8U) & 0xffU));
        if (!padD) output.push_back(static_cast<char>(packed & 0xffU));
    }
    return output;
}

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
        WriteBase64("audio/wind.ogg",
            "T2dnUwACAAAAAAAAAAA0djN/AAAAAKWOI+wBHgF2b3JiaXMAAAAAAUAfAAAAAAAAcGIAAAAAAACZAU9nZ1MAAAAAAAAAAAAANHYzfwEAAAAl6HyIC1r///////////+1A3ZvcmJpczQAAABYaXBoLk9yZyBsaWJWb3JiaXMgSSAyMDIwMDcwNCAoUmVkdWNpbmcgRW52aXJvbm1lbnQpAQAAABIAAABFTkNPREVSPWxpYnNuZGZpbGUBBXZvcmJpcxJCQ1YBAAABAAxSFCElGVNKYwiVUlIpBR1jUFtHHWPUOUYhZBBTiEkZpXtPKpVYSsgRUlgpRR1TTFNJlVKWKUUdYxRTSCFT1jFloXMUS4ZJCSVsTa50FkvomWOWMUYdY85aSp1j1jFFHWNSUkmhcxg6ZiVkFDpGxehifDA6laJCKL7H3lLpLYWKW4q91xpT6y2EGEtpwQhhc+211dxKasUYY4wxxsXiUyiC0JBVAAABAABABAFCQ1YBAAoAAMJQDEVRgNCQVQBABgCAABRFcRTHcRxHkiTLAkJDVgEAQAAAAgAAKI7hKJIjSZJkWZZlWZameZaouaov+64u667t6roOhIasBADIAAAYhiGH3knMkFOQSSYpVcw5CKH1DjnlFGTSUsaYYoxRzpBTDDEFMYbQKYUQ1E45pQwiCENInWTOIEs96OBi5zgQGrIiAIgCAACMQYwhxpBzDEoGIXKOScggRM45KZ2UTEoorbSWSQktldYi55yUTkompbQWUsuklNZCKwUAAAQ4AAAEWAiFhqwIAKIAABCDkFJIKcSUYk4xh5RSjinHkFLMOcWYcowx6CBUzDHIHIRIKcUYc0455iBkDCrmHIQMMgEAAAEOAAABFkKhISsCgDgBAIMkaZqlaaJoaZooeqaoqqIoqqrleabpmaaqeqKpqqaquq6pqq5seZ5peqaoqp4pqqqpqq5rqqrriqpqy6ar2rbpqrbsyrJuu7Ks256qyrapurJuqq5tu7Js664s27rkearqmabreqbpuqrr2rLqurLtmabriqor26bryrLryratyrKua6bpuqKr2q6purLtyq5tu7Ks+6br6rbqyrquyrLu27au+7KtC7vourauyq6uq7Ks67It67Zs20LJ81TVM03X9UzTdVXXtW3VdW1bM03XNV1XlkXVdWXVlXVddWVb90zTdU1XlWXTVWVZlWXddmVXl0XXtW1Vln1ddWVfl23d92VZ133TdXVblWXbV2VZ92Vd94VZt33dU1VbN11X103X1X1b131htm3fF11X11XZ1oVVlnXf1n1lmHWdMLqurqu27OuqLOu+ruvGMOu6MKy6bfyurQvDq+vGseu+rty+j2rbvvDqtjG8um4cu7Abv+37xrGpqm2brqvrpivrumzrvm/runGMrqvrqiz7uurKvm/ruvDrvi8Mo+vquirLurDasq/Lui4Mu64bw2rbwu7aunDMsi4Mt+8rx68LQ9W2heHVdaOr28ZvC8PSN3a+AACAAQcAgAATykChISsCgDgBAAYhCBVjECrGIIQQUgohpFQxBiFjDkrGHJQQSkkhlNIqxiBkjknIHJMQSmiplNBKKKWlUEpLoZTWUmotptRaDKG0FEpprZTSWmopttRSbBVjEDLnpGSOSSiltFZKaSlzTErGoKQOQiqlpNJKSa1lzknJoKPSOUippNJSSam1UEproZTWSkqxpdJKba3FGkppLaTSWkmptdRSba21WiPGIGSMQcmck1JKSamU0lrmnJQOOiqZg5JKKamVklKsmJPSQSglg4xKSaW1kkoroZTWSkqxhVJaa63VmFJLNZSSWkmpxVBKa621GlMrNYVQUgultBZKaa21VmtqLbZQQmuhpBZLKjG1FmNtrcUYSmmtpBJbKanFFluNrbVYU0s1lpJibK3V2EotOdZaa0ot1tJSjK21mFtMucVYaw0ltBZKaa2U0lpKrcXWWq2hlNZKKrGVklpsrdXYWow1lNJiKSm1kEpsrbVYW2w1ppZibLHVWFKLMcZYc0u11ZRai621WEsrNcYYa2415VIAAMCAAwBAgAlloNCQlQBAFAAAYAxjjEFoFHLMOSmNUs45JyVzDkIIKWXOQQghpc45CKW01DkHoZSUQikppRRbKCWl1losAACgwAEAIMAGTYnFAQoNWQkARAEAIMYoxRiExiClGIPQGKMUYxAqpRhzDkKlFGPOQcgYc85BKRljzkEnJYQQQimlhBBCKKWUAgAAChwAAAJs0JRYHKDQkBUBQBQAAGAMYgwxhiB0UjopEYRMSielkRJaCylllkqKJcbMWomtxNhICa2F1jJrJcbSYkatxFhiKgAA7MABAOzAQig0ZCUAkAcAQBijFGPOOWcQYsw5CCE0CDHmHIQQKsaccw5CCBVjzjkHIYTOOecghBBC55xzEEIIoYMQQgillNJBCCGEUkrpIIQQQimldBBCCKGUUgoAACpwAAAIsFFkc4KRoEJDVgIAeQAAgDFKOSclpUYpxiCkFFujFGMQUmqtYgxCSq3FWDEGIaXWYuwgpNRajLV2EFJqLcZaQ0qtxVhrziGl1mKsNdfUWoy15tx7ai3GWnPOuQAA3AUHALADG0U2JxgJKjRkJQCQBwBAIKQUY4w5h5RijDHnnENKMcaYc84pxhhzzjnnFGOMOeecc4wx55xzzjnGmHPOOeecc84556CDkDnnnHPQQeicc845CCF0zjnnHIQQCgAAKnAAAAiwUWRzgpGgQkNWAgDhAACAMZRSSimllFJKqKOUUkoppZRSAiGllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimVUkoppZRSSimllFJKKaUAIN8KBwD/BxtnWEk6KxwNLjRkJQAQDgAAGMMYhIw5JyWlhjEIpXROSkklNYxBKKVzElJKKYPQWmqlpNJSShmElGILIZWUWgqltFZrKam1lFIoKcUaS0qppdYy5ySkklpLrbaYOQelpNZaaq3FEEJKsbXWUmuxdVJSSa211lptLaSUWmstxtZibCWlllprqcXWWkyptRZbSy3G1mJLrcXYYosxxhoLAOBucACASLBxhpWks8LR4EJDVgIAIQEABDJKOeecgxBCCCFSijHnoIMQQgghREox5pyDEEIIIYSMMecghBBCCKGUkDHmHIQQQgghhFI65yCEUEoJpZRSSucchBBCCKWUUkoJIYQQQiillFJKKSGEEEoppZRSSiklhBBCKKWUUkoppYQQQiillFJKKaWUEEIopZRSSimllBJCCKGUUkoppZRSQgillFJKKaWUUkooIYRSSimllFJKCSWUUkoppZRSSikhlFJKKaWUUkoppQAAgAMHAIAAI+gko8oibDThwgMQAAAAAgACTACBAYKCUQgChBEIAAAAAAAIAPgAAEgKgIiIaOYMDhASFBYYGhweICIkAAAAAAAAAAAAAAAABE9nZ1MABCADAAAAAAAANHYzfwIAAACkIk3lBRcWFBgWjpWZeeUVAPzTXQAAANJsV3SWdftspiKWl5ndTeYBAAAAABzLAACECm9pcQEDlpeZ3U3mAQAAAAAoAwAQKlTqwQCOlpkn8woAe+fuAgDABmku+v7UyF3HQQBqhlVd5esNLBGgAQC8ghrm8fmWaTY2");
        WriteBase64("audio/ice.flac",
            "ZkxhQwAAACIQABAAAAHcAAHcAfQA8AAAAyB/a20x6fr45hR4Z2rR3N4jhAAAKCAAAAByZWZlcmVuY2UgbGliRkxBQyAxLjQuMyAyMDIzMDYyMwAAAAD/+HQIAAMf90gAAAisEFEWCRkmtK+Fbvap73OYBPsxBhBiGb1SnOr9EMQYIRjZ+5zqWpDCGEGYkilc51/IxhhDCEupRVKXxkMMIYxkWOc5XXoZhAhBDSdTlOr9kYMQQQjWupxV7wzGGGIIlzlcp+uxBDDDEZl7nK5VSIIEIII29Kope9JjDDCGI1flFUr7MQYQYhm9Upzq/RDEGCEY2fuc6lqQwhhBmJIpXOdfyMYYQwhLqUVSl8ZDDCGMZFjnOV16GYQIQQ0nU5Tq/ZCCDDEM1rqcVe8MxhhiCJc5XKfrsQQwwxGZe5yuVUiCBCCCNvSqKXvSYwwwhiNX5RVK+zEGEGIZveOpSr0QxBghGNn7nOpakMIYQZiSKVznX8jGGEMIS6lFUpfGQwwhjGRY5zldehmECEENJ1OU6v2QggwxDNa6nFXvDMYYYgiXOVyn67EEMMMRmXucrlVIggQggjb1zld3pMYYYQxGr8oqlfZiDCDEM3qlOdX6IYgwQjGz9znUtSGEMIMxJFK5zr+RjDCGEJdSiqUvjIYYQxjIsc5yuvQzCBCCGk6nKdX7IQQYYhmtdTir3hmMMMQRLnK5T9diCGGGIzL3OVyqkQQIQQRt65yu70mMMMIYjVzqTg==");
        WriteProcessedTexture("textures/ground.ttex");
        Write("catalog.tmanifest",
            "talvisota-content-manifest\t1\n"
            "asset\taudio.wind\taudio\taudio/wind.wav\trequired\t-\tproject\tproject\n"
            "asset\taudio.ambience\taudio\taudio/wind.ogg\trequired\t-\tproject\tproject\n"
            "asset\taudio.ice\taudio\taudio/ice.flac\trequired\t-\tproject\tproject\n"
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
    void WriteBase64(const std::filesystem::path& relative, std::string_view encoded)
    {
        const std::string decoded = DecodeBase64(encoded);
        Require(!decoded.empty(), "Could not decode compressed audio fixture.");
        Write(relative, decoded);
    }
    void WriteBmp(const std::filesystem::path& relative)
    {
        std::filesystem::create_directories((root_ / relative).parent_path());
        constexpr std::uint32_t width = 4U;
        constexpr std::uint32_t height = 4U;
        constexpr std::uint32_t rowStride = 12U;
        constexpr std::uint32_t pixelBytes = rowStride * height;
        std::vector<std::uint8_t> bytes(54U + pixelBytes, 0U);
        bytes[0] = 'B'; bytes[1] = 'M';
        const auto put32 = [&bytes](std::size_t offset, std::uint32_t value)
        {
            for (std::size_t index = 0; index < 4U; ++index)
                bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU);
        };
        put32(2U, static_cast<std::uint32_t>(bytes.size()));
        put32(10U, 54U);
        put32(14U, 40U);
        put32(18U, width);
        put32(22U, height);
        bytes[26U] = 1U;
        bytes[28U] = 24U;
        put32(34U, pixelBytes);
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t pixel = 54U + y * rowStride + x * 3U;
                bytes[pixel] = static_cast<std::uint8_t>(150U + x * 7U);
                bytes[pixel + 1U] = static_cast<std::uint8_t>(185U + y * 5U);
                bytes[pixel + 2U] = static_cast<std::uint8_t>(215U + x * 3U + y * 2U);
            }
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
        vanta::assets::CpuTexture source;
        Require(vanta::assets::LoadWicTexture(root_ / "textures/albedo.bmp", source),
                "Could not decode the source BMP for texture processing.");
        std::vector<vanta::assets::TextureMip> mips;
        std::string error;
        Require(vanta::assets::BuildTextureMipChain(source,
                    vanta::assets::TextureColorSpace::Srgb, mips, error),
                "Could not build a deterministic source texture mip chain.");
        Require(mips.size() > 1U && mips.front().width == source.width &&
                    mips.front().height == source.height &&
                    mips.back().width == 1U && mips.back().height == 1U,
                "Texture processing must generate a complete mip chain.");
        Require(vanta::assets::WriteProcessedTexture(root_ / relative, mips,
                    vanta::assets::TextureColorSpace::Srgb, error),
                "Could not write processed terrain texture fixture.");
        vanta::assets::ProcessedTexture verified;
        Require(vanta::assets::LoadProcessedTexture(root_ / relative, verified, error) &&
                    verified.mipChain.size() == mips.size(),
                "Processed texture must be readable immediately after writing.");

        std::vector<vanta::assets::TextureMip> repeatedMips;
        Require(vanta::assets::BuildTextureMipChain(source,
                    vanta::assets::TextureColorSpace::Srgb, repeatedMips, error),
                "Repeated texture processing should succeed.");
        Require(repeatedMips.size() == mips.size(),
                "Repeated texture processing should produce the same mip count.");
        for (std::size_t index = 0; index < mips.size(); ++index)
            Require(repeatedMips[index].width == mips[index].width &&
                        repeatedMips[index].height == mips[index].height &&
                        repeatedMips[index].rgba == mips[index].rgba,
                    "Repeated texture processing should produce byte-identical mip pixels.");

        const auto repeatedPath = root_ / "textures/ground-repeat.ttex";
        Require(vanta::assets::WriteProcessedTexture(repeatedPath, repeatedMips,
                    vanta::assets::TextureColorSpace::Srgb, error),
                "Repeated processed texture should be writable.");
        const auto readBytes = [](const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>());
        };
        Require(readBytes(root_ / relative) == readBytes(repeatedPath),
                "Repeated source processing should produce byte-identical .ttex containers.");
        std::filesystem::remove(repeatedPath);
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
    Require(media.importedModels == 2U && media.decodedTextures == 3U && media.decodedAudio == 3U,
            "Media validation summary should account for every unique source asset.");

    AssetPackageSummary built;
    Require(BuildAssetPackage(registry, fixture.Package(), built, diagnostics),
            "Complete validated asset set should package successfully.");
    Require(built.assetCount == 8U, "Package should contain all five registered source assets.");

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
