#include "talvisota/presentation/MaterialRenderBridge.hpp"

#include <array>
#include <filesystem>
#include <system_error>

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
    // The current renderer contract implements the metallic-roughness PBR core.
    // Reject advanced authored properties rather than silently rendering them wrong.
    const bool hasEmissive = definition.emissive[0] != 0.0F ||
        definition.emissive[1] != 0.0F || definition.emissive[2] != 0.0F ||
        !definition.emissiveTexture.empty();
    if (hasEmissive || definition.normalScale != 1.0F ||
        definition.occlusionStrength != 1.0F ||
        definition.alphaMode != content::MaterialAlphaMode::Opaque ||
        definition.alphaCutoff != 0.5F || definition.doubleSided)
    {
        error = "Typed material requests emissive, normal scaling, occlusion strength, or "
                "alpha features that are not yet supported by the active renderer contract.";
        return false;
    }

    RenderMaterial candidate;
    candidate.baseColor = definition.baseColor;
    candidate.roughness = definition.roughness;
    candidate.metallic = definition.metallic;

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
