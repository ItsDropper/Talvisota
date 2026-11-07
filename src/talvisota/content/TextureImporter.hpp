#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace talvisota::content
{
struct DecodedTexture final
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Top-left origin, tightly packed RGBA8. BMP colour bytes are preserved as sRGB;
    // the decoder does not perform colour-space conversion.
    std::vector<std::uint8_t> rgba8;
};

struct TextureDiagnostic final
{
    std::filesystem::path file;
    std::string field;
    std::string message;
};

// Decodes Windows BMP files with 24-bit or 32-bit BI_RGB pixels into canonical
// top-left-origin RGBA8. Output is replaced only after complete validation.
[[nodiscard]] bool DecodeBmp(std::span<const std::uint8_t> source,
                             const std::filesystem::path& sourcePath,
                             DecodedTexture& output,
                             std::vector<TextureDiagnostic>& diagnostics);

// Reads and decodes a BMP file with a bounded input size.
[[nodiscard]] bool LoadBmp(const std::filesystem::path& path,
                           DecodedTexture& output,
                           std::vector<TextureDiagnostic>& diagnostics);
} // namespace talvisota::content
