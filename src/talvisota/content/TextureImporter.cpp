#include "talvisota/content/TextureImporter.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <utility>

namespace talvisota::content
{
namespace
{
constexpr std::uint64_t MaximumDecodedBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MaximumSourceBytes = 1024ULL * 1024ULL * 1024ULL;

void Add(std::vector<TextureDiagnostic>& diagnostics, const std::filesystem::path& path,
         std::string field, std::string message)
{
    diagnostics.push_back({path, std::move(field), std::move(message)});
}

[[nodiscard]] bool HasRange(std::span<const std::uint8_t> bytes,
                            std::uint64_t offset, std::uint64_t count) noexcept
{
    return offset <= bytes.size() && count <= static_cast<std::uint64_t>(bytes.size()) - offset;
}

[[nodiscard]] std::uint16_t ReadU16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] std::uint32_t ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] std::int32_t ReadI32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    const std::uint32_t bits = ReadU32(bytes, offset);
    return std::bit_cast<std::int32_t>(bits);
}
} // namespace

bool DecodeBmp(std::span<const std::uint8_t> source, const std::filesystem::path& sourcePath,
               DecodedTexture& output, std::vector<TextureDiagnostic>& diagnostics)
{
    diagnostics.clear();
    if (source.size() < 54U)
    {
        Add(diagnostics, sourcePath, "header", "BMP is truncated before the complete file and DIB headers.");
        return false;
    }
    if (source[0] != static_cast<std::uint8_t>('B') || source[1] != static_cast<std::uint8_t>('M'))
    {
        Add(diagnostics, sourcePath, "signature", "Expected the Windows BMP 'BM' signature.");
        return false;
    }

    const std::uint32_t declaredFileSize = ReadU32(source, 2U);
    const std::uint32_t pixelOffset = ReadU32(source, 10U);
    const std::uint32_t dibSize = ReadU32(source, 14U);
    if (dibSize < 40U)
    {
        Add(diagnostics, sourcePath, "dibHeader", "Only BITMAPINFOHEADER-compatible DIB headers (40 bytes or larger) are supported.");
        return false;
    }
    const std::uint64_t dibEnd = 14ULL + dibSize;
    if (!HasRange(source, 14U, dibSize) || pixelOffset < dibEnd || pixelOffset > source.size())
    {
        Add(diagnostics, sourcePath, "pixelOffset", "Pixel data offset overlaps the DIB header or lies outside the file.");
        return false;
    }
    if (declaredFileSize != 0U &&
        (declaredFileSize > source.size() || declaredFileSize < pixelOffset))
    {
        Add(diagnostics, sourcePath, "fileSize", "Declared BMP file size is inconsistent with the supplied bytes.");
        return false;
    }

    const std::int32_t signedWidth = ReadI32(source, 18U);
    const std::int32_t signedHeight = ReadI32(source, 22U);
    if (signedWidth <= 0 || signedHeight == 0 ||
        signedHeight == std::numeric_limits<std::int32_t>::min())
    {
        Add(diagnostics, sourcePath, "dimensions", "BMP width must be positive and height must be non-zero and representable.");
        return false;
    }
    if (ReadU16(source, 26U) != 1U)
    {
        Add(diagnostics, sourcePath, "planes", "BMP plane count must be exactly one.");
        return false;
    }

    const std::uint16_t bitsPerPixel = ReadU16(source, 28U);
    if (bitsPerPixel != 24U && bitsPerPixel != 32U)
    {
        Add(diagnostics, sourcePath, "bitsPerPixel", "Only 24-bit BGR and 32-bit BGRX BMP pixels are supported.");
        return false;
    }
    if (ReadU32(source, 30U) != 0U)
    {
        Add(diagnostics, sourcePath, "compression", "Compressed BMP data is not supported; BI_RGB is required.");
        return false;
    }

    const std::uint64_t width = static_cast<std::uint64_t>(signedWidth);
    const std::uint64_t height = static_cast<std::uint64_t>(
        signedHeight < 0 ? -static_cast<std::int64_t>(signedHeight) : signedHeight);
    const std::uint64_t bytesPerPixel = bitsPerPixel / 8U;
    if (width > std::numeric_limits<std::uint64_t>::max() / bytesPerPixel)
    {
        Add(diagnostics, sourcePath, "dimensions", "BMP row size overflows the supported address range.");
        return false;
    }
    const std::uint64_t unpaddedRowBytes = width * bytesPerPixel;
    if (unpaddedRowBytes > std::numeric_limits<std::uint64_t>::max() - 3ULL)
    {
        Add(diagnostics, sourcePath, "dimensions", "BMP row alignment overflows the supported address range.");
        return false;
    }
    const std::uint64_t rowStride = (unpaddedRowBytes + 3ULL) & ~3ULL;
    if (height > std::numeric_limits<std::uint64_t>::max() / rowStride ||
        width > std::numeric_limits<std::uint64_t>::max() / height ||
        width * height > MaximumDecodedBytes / 4ULL)
    {
        Add(diagnostics, sourcePath, "dimensions", "Decoded texture exceeds the 512 MiB RGBA8 safety limit.");
        return false;
    }
    const std::uint64_t pixelBytes = rowStride * height;
    const std::uint64_t pixelEnd = static_cast<std::uint64_t>(pixelOffset) + pixelBytes;
    if (pixelEnd < pixelOffset || !HasRange(source, pixelOffset, pixelBytes) ||
        (declaredFileSize != 0U && pixelEnd > declaredFileSize))
    {
        Add(diagnostics, sourcePath, "pixelData", "BMP pixel array is truncated or exceeds the declared file size.");
        return false;
    }
    const std::uint32_t declaredImageSize = ReadU32(source, 34U);
    if (declaredImageSize != 0U && declaredImageSize < pixelBytes)
    {
        Add(diagnostics, sourcePath, "imageSize", "Declared uncompressed image size is smaller than the pixel array.");
        return false;
    }

    DecodedTexture candidate;
    candidate.width = static_cast<std::uint32_t>(width);
    candidate.height = static_cast<std::uint32_t>(height);
    candidate.rgba8.resize(static_cast<std::size_t>(width * height * 4ULL));
    const bool topDown = signedHeight < 0;
    const std::size_t outputRowBytes = static_cast<std::size_t>(width * 4ULL);
    const std::size_t inputRowBytes = static_cast<std::size_t>(rowStride);
    const std::size_t sourcePixelOffset = static_cast<std::size_t>(pixelOffset);

    for (std::uint64_t y = 0; y < height; ++y)
    {
        const std::uint64_t sourceY = topDown ? y : height - 1ULL - y;
        const std::size_t sourceRow = sourcePixelOffset +
            static_cast<std::size_t>(sourceY) * inputRowBytes;
        const std::size_t outputRow = static_cast<std::size_t>(y) * outputRowBytes;
        for (std::uint64_t x = 0; x < width; ++x)
        {
            const std::size_t input = sourceRow + static_cast<std::size_t>(x * bytesPerPixel);
            const std::size_t destination = outputRow + static_cast<std::size_t>(x * 4ULL);
            candidate.rgba8[destination] = source[input + 2U];
            candidate.rgba8[destination + 1U] = source[input + 1U];
            candidate.rgba8[destination + 2U] = source[input];
            // BI_RGB does not define the fourth byte as meaningful alpha.
            candidate.rgba8[destination + 3U] = 255U;
        }
    }

    output = std::move(candidate);
    return true;
}

bool LoadBmp(const std::filesystem::path& path, DecodedTexture& output,
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
    return DecodeBmp(bytes, path, output, diagnostics);
}
} // namespace talvisota::content
