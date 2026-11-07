#pragma once

#include "talvisota/content/TextureImporter.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace talvisota::content
{
enum class TextureColorSpace : std::uint32_t
{
    Linear = 0,
    Srgb = 1
};

struct TextureMip final
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8;
};

struct TextureProcessingDiagnostic final
{
    std::filesystem::path file;
    std::string field;
    std::string message;
};

// Generates a complete box-filtered mip chain. RGB is filtered in the selected
// colour space; alpha is always filtered linearly.
[[nodiscard]] bool BuildTextureMipChain(const DecodedTexture& source,
                                        TextureColorSpace colorSpace,
                                        std::vector<TextureMip>& output,
                                        std::vector<TextureProcessingDiagnostic>& diagnostics);

// Writes a deterministic little-endian .ttex container. It contains no timestamps,
// absolute paths, host-endian fields, or nondeterministic metadata.
[[nodiscard]] bool WriteProcessedTexture(const std::filesystem::path& destination,
                                         const std::vector<TextureMip>& mipChain,
                                         TextureColorSpace colorSpace,
                                         std::vector<TextureProcessingDiagnostic>& diagnostics);
} // namespace talvisota::content
