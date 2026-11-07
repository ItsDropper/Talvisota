#pragma once

#include "talvisota/content/ContentRegistry.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace talvisota::content
{
enum class MaterialAlphaMode
{
    Opaque,
    Mask,
    Blend
};

struct MaterialDefinition final
{
    std::string id;
    std::array<float, 3> baseColor{1.0F, 1.0F, 1.0F}; // Linear RGB.
    float roughness = 0.8F;
    float metallic = 0.0F;
    std::array<float, 3> emissive{0.0F, 0.0F, 0.0F}; // Linear RGB.
    float normalScale = 1.0F;
    float occlusionStrength = 1.0F;
    float alphaCutoff = 0.5F;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    bool doubleSided = false;
    std::string baseColorTexture;
    std::string normalTexture;
    std::string ormTexture;
    std::string emissiveTexture;
};

struct MaterialDiagnostic final
{
    std::filesystem::path file;
    std::size_t line = 0;
    std::string field;
    std::string message;
};

// Loads a strict, versioned .tmat definition. The material ID must match a material
// record in the already validated manifest; texture slots must reference texture assets.
// The output is replaced only after the complete definition validates.
[[nodiscard]] bool LoadMaterialDefinition(const std::filesystem::path& path,
                                          const ContentRegistry& registry,
                                          MaterialDefinition& output,
                                          std::vector<MaterialDiagnostic>& diagnostics);
} // namespace talvisota::content
