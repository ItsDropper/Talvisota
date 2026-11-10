#include "talvisota/presentation/MaterialRenderBridge.hpp"

#include <array>
#include <filesystem>
#include <system_error>
#include <utility>

namespace talvisota::presentation
{
namespace
{
bool ResolveTexture(const content::ContentRegistry& registry, const std::string& id,
                    std::string& output, std::string& error)
{
    if (id.empty())
    {
        output.clear();
        return true;
    }

    const content::AssetRecord* asset = registry.Find(id);
    if (asset == nullptr || asset->kind != content::AssetKind::Texture)
    {
        error = "Typed material references a missing or non-texture asset: " + id;
        return false;
    }

    std::error_code filesystemError;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(registry.Root() / asset->relativePath, filesystemError);
    if (filesystemError || !std::filesystem::is_regular_file(resolved, filesystemError) ||
        filesystemError)
    {
        error = "Typed material texture is missing or unreadable: " + id;
        return false;
    }

    output = resolved.string();
    return true;
}
} // namespace

bool ApplyMaterialDefinition(const content::MaterialDefinition& definition,
                             const content::ContentRegistry& registry,
                             RenderMaterial& output, std::string& error)
{
    // Emissive texture maps still require an additional GPU texture slot; all scalar
    // emissive properties and the three alpha modes are supported by both backends.
    if (!definition.emissiveTexture.empty())
    {
        error = "Typed material requests an emissive texture map, which is not yet "
                "implemented by the active renderer backends.";
        return false;
    }

    RenderMaterial candidate;
    candidate.baseColor = definition.baseColor;
    candidate.roughness = definition.roughness;
    candidate.metallic = definition.metallic;
    candidate.emissive = definition.emissive;
    candidate.normalScale = definition.normalScale;
    candidate.occlusionStrength = definition.occlusionStrength;
    candidate.alphaCutoff = definition.alphaCutoff;
    candidate.alphaMode = definition.alphaMode == content::MaterialAlphaMode::Mask
        ? RenderAlphaMode::Mask : definition.alphaMode == content::MaterialAlphaMode::Blend
            ? RenderAlphaMode::Blend : RenderAlphaMode::Opaque;
    candidate.doubleSided = definition.doubleSided;

    if (!ResolveTexture(registry, definition.baseColorTexture,
                        candidate.baseColorTexture, error) ||
        !ResolveTexture(registry, definition.normalTexture,
                        candidate.normalTexture, error))
        return false;

    if (!definition.ormTexture.empty())
    {
        std::string ormPath;
        if (!ResolveTexture(registry, definition.ormTexture, ormPath, error))
            return false;

        // Engine shader contract: R=AO, G=roughness, B=metallic. The shader
        // samples the roughness and metallic slots separately, so both must
        // reference the same packed image and carry the packed-channel flag.
        candidate.roughnessTexture = ormPath;
        candidate.metallicTexture = ormPath;
        candidate.ambientOcclusionTexture = ormPath;
        candidate.packedMetallicRoughness = true;
    }

    output = std::move(candidate);
    error.clear();
    return true;
}
} // namespace talvisota::presentation
