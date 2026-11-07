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
    void WriteBase64(const std::filesystem::path& relative, std::string_view encoded)
    {
        const std::string decoded = DecodeBase64(encoded);
        Require(!decoded.empty(), "Could not decode base64 audio fixture.");
        Write(relative, decoded);
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
    void WritePcmWav(const std::filesystem::path& relative)
    {
        std::filesystem::create_directories((path_ / relative).parent_path());
        const std::vector<std::uint8_t> bytes = {
            'R','I','F','F', 38,0,0,0, 'W','A','V','E',
            'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
            0x40,0x1f,0,0, 0x80,0x3e,0,0, 2,0, 16,0,
            'd','a','t','a', 2,0,0,0, 0,0
        };
        std::ofstream file(path_ / relative, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(static_cast<bool>(file), "Could not write PCM WAV fixture.");
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

void TestMissingOptionalAssetIsSkipped()
{
    TemporaryDirectory directory;
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\ttexture.optional\ttexture\ttextures/optional.bmp\toptional\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(directory.Path() / "catalog.tmanifest", directory.Path(),
                                registry, manifestDiagnostics),
            "Missing optional asset manifest should load.");
    EngineAssetSummary summary;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(ValidateEngineAssets(registry, summary, diagnostics),
            "Missing optional asset should not fail validation.");
    Require(summary.decodedTextures == 0U && summary.importedModels == 0U,
            "Missing optional asset should not be counted as decoded.");
}

void TestValidatesProcessedTextureContainer()
{
    TemporaryDirectory directory;
    directory.Write("textures/.keep", "");
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

    directory.Write("materials/tree.mtl", "newmtl bark\nKd 0.5 0.4 0.3\n");
    directory.Write("models/tree.obj",
        "mtllib ../materials/tree.mtl\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\tmodel.tree\tmodel\tmodels/tree.obj\trequired\tmaterial.tree\tproject\tproject\n"
        "asset\tmaterial.tree\tmaterial\tmaterials/tree.mtl\trequired\t-\tproject\tproject\n");
    Require(LoadContentManifest(directory.Path() / "catalog.tmanifest", directory.Path(),
                                registry, manifestDiagnostics),
            "Contained parent-relative MTL manifest should load.");
    diagnostics.clear();
    Require(ValidateEngineAssets(registry, summary, diagnostics),
            "OBJ material sidecars may use parent-relative paths that remain inside the content root.");

    directory.Write("models/tree.obj",
        "mtllib ../../outside.mtl\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    diagnostics.clear();
    Require(!ValidateEngineAssets(registry, summary, diagnostics),
            "OBJ material sidecars resolving outside the content root must fail.");
    Require(!diagnostics.empty() && diagnostics.front().field == "materialLibrary",
            "Escaping OBJ sidecar paths must report a materialLibrary diagnostic.");
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


void TestDecodesPcmWaveAssets()
{
    TemporaryDirectory directory;
    directory.WritePcmWav("audio/ambience.wav");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\taudio.ambience\taudio\taudio/ambience.wav\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(directory.Path() / "catalog.tmanifest", directory.Path(),
                                registry, manifestDiagnostics),
            "PCM WAV manifest should load.");
    EngineAssetSummary summary;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(ValidateEngineAssets(registry, summary, diagnostics),
            "Valid PCM WAV should pass audio decoding and validation.");
    Require(summary.decodedAudio == 1U,
            "Validated PCM WAV should be counted as decoded audio.");
}

void TestDecodesOggVorbisAndFlacAssets()
{
    TemporaryDirectory directory;
    directory.WriteBase64("audio/ambience.ogg",
        "T2dnUwACAAAAAAAAAAA0djN/AAAAAKWOI+wBHgF2b3JiaXMAAAAAAUAfAAAAAAAAcGIAAAAAAACZAU9nZ1MAAAAAAAAAAAAANHYzfwEAAAAl6HyIC1r///////////+1A3ZvcmJpczQAAABYaXBoLk9yZyBsaWJWb3JiaXMgSSAyMDIwMDcwNCAoUmVkdWNpbmcgRW52aXJvbm1lbnQpAQAAABIAAABFTkNPREVSPWxpYnNuZGZpbGUBBXZvcmJpcxJCQ1YBAAABAAxSFCElGVNKYwiVUlIpBR1jUFtHHWPUOUYhZBBTiEkZpXtPKpVYSsgRUlgpRR1TTFNJlVKWKUUdYxRTSCFT1jFloXMUS4ZJCSVsTa50FkvomWOWMUYdY85aSp1j1jFFHWNSUkmhcxg6ZiVkFDpGxehifDA6laJCKL7H3lLpLYWKW4q91xpT6y2EGEtpwQhhc+211dxKasUYY4wxxsXiUyiC0JBVAAABAABABAFCQ1YBAAoAAMJQDEVRgNCQVQBABgCAABRFcRTHcRxHkiTLAkJDVgEAQAAAAgAAKI7hKJIjSZJkWZZlWZameZaouaov+64u667t6roOhIasBADIAAAYhiGH3knMkFOQSSYpVcw5CKH1DjnlFGTSUsaYYoxRzpBTDDEFMYbQKYUQ1E45pQwiCENInWTOIEs96OBi5zgQGrIiAIgCAACMQYwhxpBzDEoGIXKOScggRM45KZ2UTEoorbSWSQktldYi55yUTkompbQWUsuklNZCKwUAAAQ4AAAEWAiFhqwIAKIAABCDkFJIKcSUYk4xh5RSjinHkFLMOcWYcowx6CBUzDHIHIRIKcUYc0455iBkDCrmHIQMMgEAAAEOAAABFkKhISsCgDgBAIMkaZqlaaJoaZooeqaoqqIoqqrleabpmaaqeqKpqqaquq6pqq5seZ5peqaoqp4pqqqpqq5rqqrriqpqy6ar2rbpqrbsyrJuu7Ks256qyrapurJuqq5tu7Js664s27rkearqmabreqbpuqrr2rLqurLtmabriqor26bryrLryratyrKua6bpuqKr2q6purLtyq5tu7Ks+6br6rbqyrquyrLu27au+7KtC7vourauyq6uq7Ks67It67Zs20LJ81TVM03X9UzTdVXXtW3VdW1bM03XNV1XlkXVdWXVlXVddWVb90zTdU1XlWXTVWVZlWXddmVXl0XXtW1Vln1ddWVfl23d92VZ133TdXVblWXbV2VZ92Vd94VZt33dU1VbN11X103X1X1b131htm3fF11X11XZ1oVVlnXf1n1lmHWdMLqurqu27OuqLOu+ruvGMOu6MKy6bfyurQvDq+vGseu+rty+j2rbvvDqtjG8um4cu7Abv+37xrGpqm2brqvrpivrumzrvm/runGMrqvrqiz7uurKvm/ruvDrvi8Mo+vquirLurDasq/Lui4Mu64bw2rbwu7aunDMsi4Mt+8rx68LQ9W2heHVdaOr28ZvC8PSN3a+AACAAQcAgAATykChISsCgDgBAAYhCBVjECrGIIQQUgohpFQxBiFjDkrGHJQQSkkhlNIqxiBkjknIHJMQSmiplNBKKKWlUEpLoZTWUmotptRaDKG0FEpprZTSWmopttRSbBVjEDLnpGSOSSiltFZKaSlzTErGoKQOQiqlpNJKSa1lzknJoKPSOUippNJSSam1UEproZTWSkqxpdJKba3FGkppLaTSWkmptdRSba21WiPGIGSMQcmck1JKSamU0lrmnJQOOiqZg5JKKamVklKsmJPSQSglg4xKSaW1kkoroZTWSkqxhVJaa63VmFJLNZSSWkmpxVBKa621GlMrNYVQUgultBZKaa21VmtqLbZQQmuhpBZLKjG1FmNtrcUYSmmtpBJbKanFFluNrbVYU0s1lpJibK3V2EotOdZaa0ot1tJSjK21mFtMucVYaw0ltBZKaa2U0lpKrcXWWq2hlNZKKrGVklpsrdXYWow1lNJiKSm1kEpsrbVYW2w1ppZibLHVWFKLMcZYc0u11ZRai621WEsrNcYYa2415VIAAMCAAwBAgAlloNCQlQBAFAAAYAxjjEFoFHLMOSmNUs45JyVzDkIIKWXOQQghpc45CKW01DkHoZSUQikppRRbKCWl1losAACgwAEAIMAGTYnFAQoNWQkARAEAIMYoxRiExiClGIPQGKMUYxAqpRhzDkKlFGPOQcgYc85BKRljzkEnJYQQQimlhBBCKKWUAgAAChwAAAJs0JRYHKDQkBUBQBQAAGAMYgwxhiB0UjopEYRMSielkRJaCylllkqKJcbMWomtxNhICa2F1jJrJcbSYkatxFhiKgAA7MABAOzAQig0ZCUAkAcAQBijFGPOOWcQYsw5CCE0CDHmHIQQKsaccw5CCBVjzjkHIYTOOecghBBC55xzEEIIoYMQQgillNJBCCGEUkrpIIQQQimldBBCCKGUUgoAACpwAAAIsFFkc4KRoEJDVgIAeQAAgDFKOSclpUYpxiCkFFujFGMQUmqtYgxCSq3FWDEGIaXWYuwgpNRajLV2EFJqLcZaQ0qtxVhrziGl1mKsNdfUWoy15tx7ai3GWnPOuQAA3AUHALADG0U2JxgJKjRkJQCQBwBAIKQUY4w5h5RijDHnnENKMcaYc84pxhhzzjnnFGOMOeecc4wx55xzzjnGmHPOOeecc84556CDkDnnnHPQQeicc845CCF0zjnnHIQQCgAAKnAAAAiwUWRzgpGgQkNWAgDhAACAMZRSSimllFJKqKOUUkoppZRSAiGllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimVUkoppZRSSimllFJKKaUAIN8KBwD/BxtnWEk6KxwNLjRkJQAQDgAAGMMYhIw5JyWlhjEIpXROSkklNYxBKKVzElJKKYPQWmqlpNJSShmElGILIZWUWgqltFZrKam1lFIoKcUaS0qppdYy5ySkklpLrbaYOQelpNZaaq3FEEJKsbXWUmuxdVJSSa211lptLaSUWmstxtZibCWlllprqcXWWkyptRZbSy3G1mJLrcXYYosxxhoLAOBucACASLBxhpWks8LR4EJDVgIAIQEABDJKOeecgxBCCCFSijHnoIMQQgghREox5pyDEEIIIYSMMecghBBCCKGUkDHmHIQQQgghhFI65yCEUEoJpZRSSucchBBCCKWUUkoJIYQQQiillFJKKSGEEEoppZRSSiklhBBCKKWUUkoppYQQQiillFJKKaWUEEIopZRSSimllBJCCKGUUkoppZRSQgillFJKKaWUUkooIYRSSimllFJKCSWUUkoppZRSSikhlFJKKaWUUkoppQAAgAMHAIAAI+gko8oibDThwgMQAAAAAgACTACBAYKCUQgChBEIAAAAAAAIAPgAAEgKgIiIaOYMDhASFBYYGhweICIkAAAAAAAAAAAAAAAABE9nZ1MABCADAAAAAAAANHYzfwIAAACkIk3lBRcWFBgWjpWZeeUVAPzTXQAAANJsV3SWdftspiKWl5ndTeYBAAAAABzLAACECm9pcQEDlpeZ3U3mAQAAAAAoAwAQKlTqwQCOlpkn8woAe+fuAgDABmku+v7UyF3HQQBqhlVd5esNLBGgAQC8ghrm8fmWaTY2");
    directory.WriteBase64("audio/ice.flac",
        "ZkxhQwAAACIQABAAAAHcAAHcAfQA8AAAAyB/a20x6fr45hR4Z2rR3N4jhAAAKCAAAAByZWZlcmVuY2UgbGliRkxBQyAxLjQuMyAyMDIzMDYyMwAAAAD/+HQIAAMf90gAAAisEFEWCRkmtK+Fbvap73OYBPsxBhBiGb1SnOr9EMQYIRjZ+5zqWpDCGEGYkilc51/IxhhDCEupRVKXxkMMIYxkWOc5XXoZhAhBDSdTlOr9kYMQQQjWupxV7wzGGGIIlzlcp+uxBDDDEZl7nK5VSIIEIII29Kope9JjDDCGI1flFUr7MQYQYhm9Upzq/RDEGCEY2fuc6lqQwhhBmJIpXOdfyMYYQwhLqUVSl8ZDDCGMZFjnOV16GYQIQQ0nU5Tq/ZCCDDEM1rqcVe8MxhhiCJc5XKfrsQQwwxGZe5yuVUiCBCCCNvSqKXvSYwwwhiNX5RVK+zEGEGIZveOpSr0QxBghGNn7nOpakMIYQZiSKVznX8jGGEMIS6lFUpfGQwwhjGRY5zldehmECEENJ1OU6v2QggwxDNa6nFXvDMYYYgiXOVyn67EEMMMRmXucrlVIggQggjb1zld3pMYYYQxGr8oqlfZiDCDEM3qlOdX6IYgwQjGz9znUtSGEMIMxJFK5zr+RjDCGEJdSiqUvjIYYQxjIsc5yuvQzCBCCGk6nKdX7IQQYYhmtdTir3hmMMMQRLnK5T9diCGGGIzL3OVyqkQQIQQRt65yu70mMMMIYjVzqTg==");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\taudio.ambience\taudio\taudio/ambience.ogg\trequired\t-\tproject\tproject\n"
        "asset\taudio.ice\taudio\taudio/ice.flac\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(directory.Path() / "catalog.tmanifest", directory.Path(),
                                registry, manifestDiagnostics),
            "Valid compressed audio manifest should load.");
    EngineAssetSummary summary;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(ValidateEngineAssets(registry, summary, diagnostics),
            "Ogg Vorbis and FLAC audio should decode successfully.");
    Require(summary.decodedAudio == 2U, "Both compressed streams should be counted as decoded.");
}

void TestMalformedCompressedAudioFailsClosed()
{
    TemporaryDirectory directory;
    directory.Write("audio/ambience.ogg", "not an OGG stream");
    directory.Manifest(
        "talvisota-content-manifest\t1\n"
        "asset\taudio.ambience\taudio\taudio/ambience.ogg\trequired\t-\tproject\tproject\n");
    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Require(LoadContentManifest(directory.Path() / "catalog.tmanifest", directory.Path(),
                                registry, manifestDiagnostics),
            "Compressed audio manifest should load structurally.");
    EngineAssetSummary summary;
    std::vector<EngineAssetDiagnostic> diagnostics;
    Require(!ValidateEngineAssets(registry, summary, diagnostics),
            "Unsupported compressed audio must fail closed.");
    Require(!diagnostics.empty() && diagnostics.front().field == "audio",
            "Unsupported audio should report a precise diagnostic.");
}

int main()
{
    try
    {
        TestImportsRegisteredObjAndDecodesRegisteredTexture();
        TestMissingOptionalAssetIsSkipped();
        TestValidatesProcessedTextureContainer();
        TestObjMaterialLibraryMustBeRegisteredAndDeclared();
        TestMalformedTextureFailsWithoutPublishingSummary();
        TestDecodesPcmWaveAssets();
        TestDecodesOggVorbisAndFlacAssets();
        TestMalformedCompressedAudioFailsClosed();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Engine asset validation test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All engine asset validation tests passed.\n";
    return 0;
}
