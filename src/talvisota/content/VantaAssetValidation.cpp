#include "talvisota/content/VantaAssetValidation.hpp"

#include "vanta/assets/ModelAssetStore.hpp"
#include "vanta/assets/TextureProcessing.hpp"
#include "vanta/assets/WicTextureLoader.hpp"

#include <algorithm>
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
            if (canonicalSource.extension() == ".ttex")
            {
                vanta::assets::ProcessedTexture processed;
                std::string textureError;
                if (!vanta::assets::LoadProcessedTexture(canonicalSource, processed, textureError))
                {
                    Add(diagnostics, canonicalSource, "processedTexture", textureError);
                    return false;
                }
                ++candidate.decodedTextures;
            }
            else if (!decodeTexture(canonicalSource, nullptr, canonicalSource, "texture"))
                return false;
            continue;
        }
        if (asset.kind == AssetKind::Material && canonicalSource.extension() == ".mtl")
            continue;
        if (asset.kind == AssetKind::Audio)
        {
            Add(diagnostics, sourcePath, "audio",
                "Audio decoding/validation is not implemented in the pinned engine integration; this asset cannot be marked validated.");
            return false;
        }
        if (asset.kind != AssetKind::Model)
            continue;

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
