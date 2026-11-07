#include "talvisota/content/TgaImporter.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <utility>

namespace talvisota::content
{
namespace
{
constexpr std::uint64_t MaximumSourceBytes = 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MaximumDecodedBytes = 512ULL * 1024ULL * 1024ULL;

void Add(std::vector<TextureDiagnostic>& diagnostics, const std::filesystem::path& path,
         std::string field, std::string message)
{
    diagnostics.push_back({path, std::move(field), std::move(message)});
}

[[nodiscard]] std::uint16_t U16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}
} // namespace

bool DecodeTga(std::span<const std::uint8_t> source, const std::filesystem::path& sourcePath,
               DecodedTexture& output, std::vector<TextureDiagnostic>& diagnostics)
{
    diagnostics.clear();
    if (source.size() < 18U)
    {
        Add(diagnostics, sourcePath, "header", "TGA header is truncated.");
        return false;
    }
    const std::uint8_t idLength = source[0];
    const std::uint8_t colorMapType = source[1];
    const std::uint8_t imageType = source[2];
    const std::uint16_t width = U16(source, 12U);
    const std::uint16_t height = U16(source, 14U);
    const std::uint8_t depth = source[16U];
    const std::uint8_t descriptor = source[17U];
    if (colorMapType != 0U)
    {
        Add(diagnostics, sourcePath, "colorMapType", "Colour-mapped TGA files are not supported.");
        return false;
    }
    if (imageType != 2U && imageType != 10U)
    {
        Add(diagnostics, sourcePath, "imageType", "Only true-colour TGA types 2 (raw) and 10 (RLE) are supported.");
        return false;
    }
    if (width == 0U || height == 0U)
    {
        Add(diagnostics, sourcePath, "dimensions", "TGA width and height must be non-zero.");
        return false;
    }
    if (depth != 24U && depth != 32U)
    {
        Add(diagnostics, sourcePath, "pixelDepth", "Only 24-bit BGR and 32-bit BGRA TGA pixels are supported.");
        return false;
    }
    if ((descriptor & 0xc0U) != 0U)
    {
        Add(diagnostics, sourcePath, "descriptor", "Interleaved TGA scanlines are not supported.");
        return false;
    }
    const std::uint8_t alphaBits = static_cast<std::uint8_t>(descriptor & 0x0fU);
    if ((depth == 24U && alphaBits != 0U) || (depth == 32U && alphaBits != 0U && alphaBits != 8U))
    {
        Add(diagnostics, sourcePath, "descriptor", "TGA alpha-bit count is inconsistent with the pixel depth.");
        return false;
    }

    const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * height;
    const std::uint64_t bytesPerPixel = depth / 8U;
    if (pixelCount > MaximumDecodedBytes / 4ULL)
    {
        Add(diagnostics, sourcePath, "dimensions", "Decoded texture exceeds the 512 MiB RGBA8 safety limit.");
        return false;
    }
    std::size_t cursor = 18U + static_cast<std::size_t>(idLength);
    if (cursor > source.size())
    {
        Add(diagnostics, sourcePath, "imageId", "TGA image ID extends beyond the source file.");
        return false;
    }

    DecodedTexture candidate;
    candidate.width = width;
    candidate.height = height;
    candidate.rgba8.resize(static_cast<std::size_t>(pixelCount * 4ULL));
    const bool topOrigin = (descriptor & 0x20U) != 0U;
    const bool rightOrigin = (descriptor & 0x10U) != 0U;
    std::uint64_t decodedPixels = 0U;

    const auto writePixel = [&](const std::array<std::uint8_t, 4>& pixel)
    {
        const std::uint64_t fileX = decodedPixels % width;
        const std::uint64_t fileY = decodedPixels / width;
        const std::uint64_t x = rightOrigin ? width - 1ULL - fileX : fileX;
        const std::uint64_t y = topOrigin ? fileY : height - 1ULL - fileY;
        const std::size_t destination = static_cast<std::size_t>((y * width + x) * 4ULL);
        for (std::size_t channel = 0; channel < 4U; ++channel)
            candidate.rgba8[destination + channel] = pixel[channel];
        ++decodedPixels;
    };
    const auto readPixel = [&](std::array<std::uint8_t, 4>& pixel) -> bool
    {
        if (cursor > source.size() || bytesPerPixel > source.size() - cursor)
            return false;
        pixel = {source[cursor + 2U], source[cursor + 1U], source[cursor],
                 depth == 32U ? source[cursor + 3U] : static_cast<std::uint8_t>(255U)};
        cursor += static_cast<std::size_t>(bytesPerPixel);
        return true;
    };

    if (imageType == 2U)
    {
        if (pixelCount > (source.size() - cursor) / bytesPerPixel)
        {
            Add(diagnostics, sourcePath, "pixelData", "Raw TGA pixel array is truncated.");
            return false;
        }
        while (decodedPixels < pixelCount)
        {
            std::array<std::uint8_t, 4> pixel{};
            if (!readPixel(pixel))
            {
                Add(diagnostics, sourcePath, "pixelData", "Raw TGA pixel array is truncated.");
                return false;
            }
            writePixel(pixel);
        }
    }
    else
    {
        while (decodedPixels < pixelCount)
        {
            if (cursor >= source.size())
            {
                Add(diagnostics, sourcePath, "packet", "TGA RLE packet header is missing.");
                return false;
            }
            const std::uint8_t packet = source[cursor++];
            const std::uint64_t count = static_cast<std::uint64_t>(packet & 0x7fU) + 1ULL;
            if (count > pixelCount - decodedPixels)
            {
                Add(diagnostics, sourcePath, "packet", "TGA RLE packet expands beyond the declared image dimensions.");
                return false;
            }
            if ((packet & 0x80U) != 0U)
            {
                std::array<std::uint8_t, 4> pixel{};
                if (!readPixel(pixel))
                {
                    Add(diagnostics, sourcePath, "packet", "TGA RLE run packet is truncated.");
                    return false;
                }
                for (std::uint64_t index = 0; index < count; ++index)
                    writePixel(pixel);
            }
            else
            {
                for (std::uint64_t index = 0; index < count; ++index)
                {
                    std::array<std::uint8_t, 4> pixel{};
                    if (!readPixel(pixel))
                    {
                        Add(diagnostics, sourcePath, "packet", "TGA raw packet is truncated.");
                        return false;
                    }
                    writePixel(pixel);
                }
            }
        }
    }

    output = std::move(candidate);
    return true;
}

bool LoadTga(const std::filesystem::path& path, DecodedTexture& output,
             std::vector<TextureDiagnostic>& diagnostics)
{
    diagnostics.clear();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        Add(diagnostics, path, "file", "Texture source could not be opened.");
        return false;
    }
    const std::streamoff end = static_cast<std::streamoff>(input.tellg());
    if (end < 0 || static_cast<std::uint64_t>(end) > MaximumSourceBytes)
    {
        Add(diagnostics, path, "fileSize", "Texture source is unreadable or exceeds the 1 GiB input safety limit.");
        return false;
    }
    const auto size = static_cast<std::size_t>(end);
    std::vector<std::uint8_t> bytes(size);
    input.seekg(0, std::ios::beg);
    if (size != 0U)
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!input || static_cast<std::size_t>(input.gcount()) != size)
    {
        Add(diagnostics, path, "file", "Texture source could not be read completely.");
        return false;
    }
    return DecodeTga(bytes, path, output, diagnostics);
}
} // namespace talvisota::content
