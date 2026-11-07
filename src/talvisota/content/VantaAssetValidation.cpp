#include "talvisota/content/VantaAssetValidation.hpp"

#include "vanta/assets/ModelAssetStore.hpp"
#include "vanta/assets/TextureProcessing.hpp"
#include "vanta/assets/WicTextureLoader.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace talvisota::content
{
namespace
{
void Add(std::vector<EngineAssetDiagnostic>& diagnostics, const std::filesystem::path& path,
         std::string field, std::string message)
{
    diagnostics.push_back({path, std::move(field), std::move(message)});
}

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

[[nodiscard]] bool IsWithinRoot(const std::filesystem::path& root,
                                const std::filesystem::path& candidate)
{
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute())
        return false;
    for (const auto& component : relative)
        if (component == "..")
            return false;
    return true;
}
[[nodiscard]] std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] bool ValidatePcmWave(const std::filesystem::path& path, std::string& error)
{
    std::error_code sizeError;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
    constexpr std::uintmax_t maximumFileSize = 512ULL * 1024ULL * 1024ULL;
    if (sizeError || fileSize < 44U || fileSize > maximumFileSize ||
        fileSize > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
    {
        error = "WAV file size is invalid or exceeds the 512 MiB validation limit.";
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
    std::ifstream input(path, std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char*>(bytes.data()),
                              static_cast<std::streamsize>(bytes.size())))
    {
        error = "Could not read complete WAV file.";
        return false;
    }
    const auto tag = [&bytes](std::size_t offset, const char* expected)
    {
        return offset + 4U <= bytes.size() &&
               bytes[offset] == static_cast<std::uint8_t>(expected[0]) &&
               bytes[offset + 1U] == static_cast<std::uint8_t>(expected[1]) &&
               bytes[offset + 2U] == static_cast<std::uint8_t>(expected[2]) &&
               bytes[offset + 3U] == static_cast<std::uint8_t>(expected[3]);
    };
    if (!tag(0U, "RIFF") || !tag(8U, "WAVE") ||
        static_cast<std::uint64_t>(ReadU32(bytes, 4U)) + 8ULL != bytes.size())
    {
        error = "WAV must be a complete RIFF/WAVE container with no truncated or trailing bytes.";
        return false;
    }

    bool haveFormat = false;
    bool haveData = false;
    std::uint16_t format = 0U;
    std::uint16_t channels = 0U;
    std::uint16_t bitsPerSample = 0U;
    std::uint16_t blockAlign = 0U;
    std::uint32_t sampleRate = 0U;
    std::size_t dataOffset = 0U;
    std::size_t dataSize = 0U;
    std::size_t cursor = 12U;
    while (cursor < bytes.size())
    {
        if (bytes.size() - cursor < 8U)
        {
            error = "WAV contains a truncated chunk header.";
            return false;
        }
        const std::uint32_t chunkSize = ReadU32(bytes, cursor + 4U);
        const std::size_t payload = cursor + 8U;
        if (static_cast<std::uint64_t>(chunkSize) > bytes.size() - payload)
        {
            error = "WAV chunk extends beyond the RIFF container.";
            return false;
        }
        if (tag(cursor, "fmt "))
        {
            if (haveFormat || chunkSize < 16U)
            {
                error = "WAV must contain exactly one format chunk of at least 16 bytes.";
                return false;
            }
            format = ReadU16(bytes, payload);
            channels = ReadU16(bytes, payload + 2U);
            sampleRate = ReadU32(bytes, payload + 4U);
            const std::uint32_t byteRate = ReadU32(bytes, payload + 8U);
            blockAlign = ReadU16(bytes, payload + 12U);
            bitsPerSample = ReadU16(bytes, payload + 14U);
            haveFormat = true;
            const bool supportedPcm = format == 1U &&
                (bitsPerSample == 8U || bitsPerSample == 16U ||
                 bitsPerSample == 24U || bitsPerSample == 32U);
            const bool supportedFloat = format == 3U && bitsPerSample == 32U;
            const std::uint32_t expectedAlign =
                static_cast<std::uint32_t>(channels) * (bitsPerSample / 8U);
            if ((!supportedPcm && !supportedFloat) || channels == 0U || channels > 8U ||
                sampleRate < 8000U || sampleRate > 192000U ||
                expectedAlign == 0U || expectedAlign > std::numeric_limits<std::uint16_t>::max() ||
                blockAlign != expectedAlign ||
                byteRate != sampleRate * expectedAlign)
            {
                error = "WAV format must be PCM 8/16/24/32-bit or IEEE float32, 1-8 channels, 8-192 kHz, with consistent alignment.";
                return false;
            }
        }
        else if (tag(cursor, "data"))
        {
            if (haveData)
            {
                error = "WAV contains multiple data chunks, which are not supported.";
                return false;
            }
            dataOffset = payload;
            dataSize = chunkSize;
            haveData = true;
        }
        const std::uint64_t next = static_cast<std::uint64_t>(payload) + chunkSize + (chunkSize & 1U);
        if (next > bytes.size())
        {
            error = "WAV chunk padding extends beyond the RIFF container.";
            return false;
        }
        cursor = static_cast<std::size_t>(next);
    }
    if (!haveFormat || !haveData || dataSize == 0U || dataSize % blockAlign != 0U ||
        dataSize / blockAlign > 100000000U)
    {
        error = "WAV is missing format/data, has incomplete sample frames, or exceeds the frame limit.";
        return false;
    }
    const std::size_t bytesPerSample = bitsPerSample / 8U;
    for (std::size_t offset = dataOffset; offset < dataOffset + dataSize; offset += bytesPerSample)
    {
        float decoded = 0.0F;
        if (format == 3U)
        {
            decoded = std::bit_cast<float>(ReadU32(bytes, offset));
            if (!std::isfinite(decoded))
            {
                error = "WAV floating-point audio contains a non-finite sample.";
                return false;
            }
        }
        else if (bitsPerSample == 8U)
            decoded = (static_cast<float>(bytes[offset]) - 128.0F) / 128.0F;
        else if (bitsPerSample == 16U)
            decoded = static_cast<float>(std::bit_cast<std::int16_t>(ReadU16(bytes, offset))) / 32768.0F;
        else if (bitsPerSample == 24U)
        {
            std::int32_t sample = static_cast<std::int32_t>(bytes[offset]) |
                (static_cast<std::int32_t>(bytes[offset + 1U]) << 8U) |
                (static_cast<std::int32_t>(bytes[offset + 2U]) << 16U);
            if ((sample & 0x00800000) != 0)
                sample |= ~0x00ffffff;
            decoded = static_cast<float>(sample) / 8388608.0F;
        }
        else
            decoded = static_cast<float>(std::bit_cast<std::int32_t>(ReadU32(bytes, offset))) / 2147483648.0F;
        if (!std::isfinite(decoded))
        {
            error = "WAV sample decoding produced a non-finite value.";
            return false;
        }
    }
    return true;
}
} // namespace

bool ValidateEngineAssets(const ContentRegistry& registry, EngineAssetSummary& output,
                          std::vector<EngineAssetDiagnostic>& diagnostics)
{
    diagnostics.clear();
    EngineAssetSummary candidate;
    const auto& root = registry.Root();
    std::unordered_map<std::string, const AssetRecord*> assetsByCanonicalPath;
    for (const AssetRecord& asset : registry.Assets())
    {
        std::error_code error;
        const auto path = std::filesystem::weakly_canonical(root / asset.relativePath, error);
        if (error)
        {
            Add(diagnostics, root / asset.relativePath, "path", "Could not canonicalize registered asset path.");
            continue;
        }
        assetsByCanonicalPath.emplace(path.generic_string(), &asset);
    }
    if (!diagnostics.empty())
        return false;

    std::unordered_set<std::string> decodedTexturePaths;
    const auto decodeTexture = [&](const std::filesystem::path& sourcePath,
                                   const AssetRecord* owner,
                                   const std::filesystem::path& diagnosticPath,
                                   const std::string& field) -> bool
    {
        std::error_code error;
        const auto canonical = std::filesystem::weakly_canonical(sourcePath, error);
        if (error || !IsWithinRoot(root, canonical))
        {
            Add(diagnostics, diagnosticPath, field, "Texture reference is missing or escapes the content root.");
            return false;
        }
        const auto registered = assetsByCanonicalPath.find(canonical.generic_string());
        if (registered == assetsByCanonicalPath.end() ||
            registered->second->kind != AssetKind::Texture)
        {
            Add(diagnostics, diagnosticPath, field,
                "Texture reference must resolve to a manifest-registered texture asset.");
            return false;
        }
        const AssetRecord* textureAsset = registered->second;
        if (owner != nullptr &&
            std::find(owner->dependencies.begin(), owner->dependencies.end(), textureAsset->id) ==
                owner->dependencies.end())
        {
            Add(diagnostics, diagnosticPath, field,
                "Model texture reference must be declared as a manifest dependency: " + textureAsset->id);
            return false;
        }
        if (!decodedTexturePaths.insert(canonical.generic_string()).second)
            return true;
        if (LowerExtension(canonical) == ".ttex")
        {
            vanta::assets::ProcessedTexture processed;
            std::string textureError;
            if (!vanta::assets::LoadProcessedTexture(canonical, processed, textureError))
            {
                Add(diagnostics, canonical, "processedTexture", textureError);
                return false;
            }
            ++candidate.decodedTextures;
            return true;
        }

        vanta::assets::CpuTexture texture;
        if (!vanta::assets::LoadWicTexture(canonical, texture))
        {
            Add(diagnostics, canonical, "decode",
                "VantaEngine WIC could not decode this texture source.");
            return false;
        }
        const std::uint64_t expectedBytes =
            static_cast<std::uint64_t>(texture.width) * texture.height * 4ULL;
        if (texture.width == 0U || texture.height == 0U ||
            expectedBytes != texture.rgba.size())
        {
            Add(diagnostics, canonical, "pixels",
                "WIC returned inconsistent texture dimensions or RGBA8 storage.");
            return false;
        }
        ++candidate.decodedTextures;
        return true;
    };

    vanta::assets::ModelAssetStore modelStore;
    for (const std::size_t assetIndex : registry.LoadOrder())
    {
        const AssetRecord& asset = registry.Assets()[assetIndex];
        const auto sourcePath = root / asset.relativePath;
        std::error_code sourceError;
        const auto canonicalSource = std::filesystem::weakly_canonical(sourcePath, sourceError);
        if (sourceError)
        {
            Add(diagnostics, sourcePath, "path", "Could not resolve registered asset path.");
            return false;
        }
        if (!std::filesystem::exists(canonicalSource, sourceError))
        {
            if (!asset.required && !sourceError)
                continue;
            Add(diagnostics, sourcePath, "path", "Required registered asset is missing.");
            return false;
        }
        if (sourceError || !std::filesystem::is_regular_file(canonicalSource, sourceError) ||
            sourceError || !IsWithinRoot(root, canonicalSource))
        {
            Add(diagnostics, sourcePath, "path", "Registered asset is not a regular file inside the content root.");
            return false;
        }
        if (asset.kind == AssetKind::Texture)
        {
            if (!decodeTexture(canonicalSource, nullptr, canonicalSource, "texture"))
                return false;
            continue;
        }
        if (asset.kind == AssetKind::Material && LowerExtension(canonicalSource) == ".mtl")
            continue;
        if (asset.kind == AssetKind::Audio)
        {
            std::string audioError;
            const std::string extension = LowerExtension(canonicalSource);
            if (extension != ".wav" || !ValidatePcmWave(canonicalSource, audioError))
            {
                Add(diagnostics, canonicalSource, "audio",
                    extension == ".wav" ? audioError :
                    "Only PCM/IEEE-float WAV decoding is implemented; OGG and FLAC remain unsupported.");
                return false;
            }
            ++candidate.decodedAudio;
            continue;
        }
        if (asset.kind != AssetKind::Model)
            continue;

        if (LowerExtension(canonicalSource) == ".obj")
        {
            std::ifstream obj(canonicalSource);
            if (!obj)
            {
                Add(diagnostics, canonicalSource, "model", "Could not open OBJ for material dependency preflight.");
                return false;
            }
            bool sawMaterialLibrary = false;
            std::string line;
            std::size_t lineNumber = 0U;
            while (std::getline(obj, line))
            {
                ++lineNumber;
                std::istringstream row(line);
                std::string tag;
                row >> tag;
                if (tag != "mtllib")
                    continue;
                std::string libraryName;
                std::string extra;
                if (!(row >> libraryName) || (row >> extra) || sawMaterialLibrary)
                {
                    Add(diagnostics, canonicalSource, "materialLibrary",
                        "OBJ must contain at most one mtllib declaration with exactly one relative filename (line " +
                        std::to_string(lineNumber) + ").");
                    return false;
                }
                sawMaterialLibrary = true;
                const std::filesystem::path relativeLibrary = std::filesystem::u8path(libraryName);
                if (relativeLibrary.is_absolute() || relativeLibrary.has_root_name())
                {
                    Add(diagnostics, canonicalSource, "materialLibrary",
                        "OBJ material library path must be relative to the model.");
                    return false;
                }
                for (const auto& component : relativeLibrary)
                {
                    if (component == "..")
                    {
                        Add(diagnostics, canonicalSource, "materialLibrary",
                            "OBJ material library path may not traverse parent directories.");
                        return false;
                    }
                }
                std::error_code libraryError;
                const auto libraryPath = std::filesystem::weakly_canonical(
                    canonicalSource.parent_path() / relativeLibrary, libraryError);
                if (libraryError || !IsWithinRoot(root, libraryPath))
                {
                    Add(diagnostics, canonicalSource, "materialLibrary",
                        "OBJ material library resolves outside the content root or cannot be resolved.");
                    return false;
                }
                const auto registered = assetsByCanonicalPath.find(libraryPath.generic_string());
                if (registered == assetsByCanonicalPath.end() ||
                    registered->second->kind != AssetKind::Material ||
                    std::find(asset.dependencies.begin(), asset.dependencies.end(),
                              registered->second->id) == asset.dependencies.end())
                {
                    Add(diagnostics, canonicalSource, "materialLibrary",
                        "OBJ material library must be registered as a material asset and declared as a model dependency.");
                    return false;
                }
            }
            if (!obj.eof())
            {
                Add(diagnostics, canonicalSource, "materialLibrary", "I/O error during OBJ material dependency preflight.");
                return false;
            }
        }

        vanta::assets::ModelHandle handle;
        std::string importError;
        if (!modelStore.LoadModel(sourcePath, handle, importError))
        {
            Add(diagnostics, sourcePath, "model", importError);
            return false;
        }
        const vanta::assets::CpuModel* model = modelStore.GetModel(handle);
        if (model == nullptr)
        {
            (void)modelStore.Unload(handle);
            Add(diagnostics, sourcePath, "model", "Model store published no readable CPU model.");
            return false;
        }
        bool dependenciesValid = true;
        for (const auto& materialLibrary : model->materialLibraries)
        {
            std::error_code libraryError;
            const auto canonicalLibrary = std::filesystem::weakly_canonical(materialLibrary, libraryError);
            if (libraryError || !IsWithinRoot(root, canonicalLibrary))
            {
                Add(diagnostics, sourcePath, "materialLibrary",
                    "OBJ material library is missing or escapes the content root.");
                dependenciesValid = false;
                continue;
            }
            const auto registeredLibrary = assetsByCanonicalPath.find(canonicalLibrary.generic_string());
            if (registeredLibrary == assetsByCanonicalPath.end() ||
                registeredLibrary->second->kind != AssetKind::Material ||
                std::find(asset.dependencies.begin(), asset.dependencies.end(),
                          registeredLibrary->second->id) == asset.dependencies.end())
            {
                Add(diagnostics, sourcePath, "materialLibrary",
                    "OBJ material library must be registered as a material asset and declared as a model dependency.");
                dependenciesValid = false;
            }
        }
        const auto validateMaterialTexture = [&](const std::filesystem::path& texturePath,
                                                 const char* field) -> bool
        {
            if (texturePath.empty())
                return true;
            const auto resolved = texturePath.is_absolute()
                ? texturePath : sourcePath.parent_path() / texturePath;
            return decodeTexture(resolved, &asset, sourcePath, field);
        };
        bool texturesValid = dependenciesValid;
        for (const vanta::assets::CpuMaterial& material : model->materials)
        {
            texturesValid = validateMaterialTexture(material.diffuseTexture, "baseColorTexture") && texturesValid;
            texturesValid = validateMaterialTexture(material.normalTexture, "normalTexture") && texturesValid;
            texturesValid = validateMaterialTexture(material.roughnessTexture, "roughnessTexture") && texturesValid;
            texturesValid = validateMaterialTexture(material.metallicTexture, "metallicTexture") && texturesValid;
            texturesValid = validateMaterialTexture(material.ambientOcclusionTexture, "occlusionTexture") && texturesValid;
        }
        (void)modelStore.Unload(handle);
        if (!texturesValid)
            return false;
        ++candidate.importedModels;
    }

    output = candidate;
    return true;
}
} // namespace talvisota::content
