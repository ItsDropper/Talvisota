#pragma once

#include "talvisota/content/TextureImporter.hpp"

#include <filesystem>
#include <span>
#include <vector>

namespace talvisota::content
{
// Decodes true-colour TGA 2/10 (24/32-bit) images into canonical RGBA8.
// Supports raw and packet-RLE storage and all four origin combinations.
[[nodiscard]] bool DecodeTga(std::span<const std::uint8_t> source,
                             const std::filesystem::path& sourcePath,
                             DecodedTexture& output,
                             std::vector<TextureDiagnostic>& diagnostics);
[[nodiscard]] bool LoadTga(const std::filesystem::path& path,
                           DecodedTexture& output,
                           std::vector<TextureDiagnostic>& diagnostics);
} // namespace talvisota::content
