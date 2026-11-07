#include "talvisota/content/TextureImporter.hpp"

#include <cstdint>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace talvisota::content;

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void PutU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
}

void PutU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    for (std::size_t index = 0; index < 4U; ++index)
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xffU);
}

std::vector<std::uint8_t> MakeBmp24()
{
    // Two pixels wide, two rows high. Each 24-bit row is padded to an 8-byte stride.
    std::vector<std::uint8_t> bytes(70U, 0U);
    bytes[0] = 'B'; bytes[1] = 'M';
    PutU32(bytes, 2U, static_cast<std::uint32_t>(bytes.size()));
    PutU32(bytes, 10U, 54U);
    PutU32(bytes, 14U, 40U);
    PutU32(bytes, 18U, 2U);
    PutU32(bytes, 22U, 2U);
    PutU16(bytes, 26U, 1U);
    PutU16(bytes, 28U, 24U);
    PutU32(bytes, 34U, 16U);
    // Bottom row in file order: blue, white.
    const std::uint8_t pixels[] = {
        255U, 0U, 0U, 255U, 255U, 255U, 0U, 0U,
        0U, 0U, 255U, 0U, 0U, 0U, 0U, 0U
    };
    for (std::size_t index = 0; index < sizeof(pixels); ++index)
        bytes[54U + index] = pixels[index];
    return bytes;
}

void TestDecodesBottomUpPaddedBgrToTopDownRgba()
{
    const auto bytes = MakeBmp24();
    DecodedTexture image;
    std::vector<TextureDiagnostic> diagnostics;
    Require(DecodeBmp(bytes, "fixture.bmp", image, diagnostics), "Valid 24-bit BMP should decode.");
    Require(image.width == 2U && image.height == 2U, "Decoded dimensions should match the BMP.");
    const std::vector<std::uint8_t> expected = {
        255U, 0U, 0U, 255U, 0U, 0U, 0U, 255U,
        0U, 0U, 255U, 255U, 255U, 255U, 255U, 255U
    };
    Require(image.rgba8 == expected, "Rows, BGR channels, row padding, and opaque alpha must be normalized.");
}

void TestDecodesTopDown32BitBgrxAsOpaque()
{
    std::vector<std::uint8_t> bytes(58U, 0U);
    bytes[0] = 'B'; bytes[1] = 'M';
    PutU32(bytes, 2U, static_cast<std::uint32_t>(bytes.size()));
    PutU32(bytes, 10U, 54U);
    PutU32(bytes, 14U, 40U);
    PutU32(bytes, 18U, 1U);
    PutU32(bytes, 22U, 0xffffffffU); // -1: top-down
    PutU16(bytes, 26U, 1U);
    PutU16(bytes, 28U, 32U);
    bytes[54U] = 3U; bytes[55U] = 2U; bytes[56U] = 1U; bytes[57U] = 0U;
    DecodedTexture image;
    std::vector<TextureDiagnostic> diagnostics;
    Require(DecodeBmp(bytes, "top-down.bmp", image, diagnostics), "Valid top-down 32-bit BMP should decode.");
    Require(image.rgba8 == std::vector<std::uint8_t>({1U, 2U, 3U, 255U}),
            "32-bit BI_RGB reserved byte must not be mistaken for alpha.");
}


void TestLoadsBmpFileFromDisk()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("talvisota-texture-importer-" + std::to_string(stamp));
    std::filesystem::create_directories(directory);
    const auto path = directory / "source.bmp";
    const auto bytes = MakeBmp24();
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(static_cast<bool>(file), "Could not write BMP file fixture.");
    }
    DecodedTexture image;
    std::vector<TextureDiagnostic> diagnostics;
    const bool loaded = LoadBmp(path, image, diagnostics);
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    Require(loaded, "LoadBmp should read and decode a valid file.");
    Require(image.width == 2U && image.height == 2U && image.rgba8.size() == 16U,
            "LoadBmp should publish decoded dimensions and pixels.");
}

void TestRejectsMalformedAndUnsupportedHeadersAtomically()
{
    DecodedTexture image;
    image.width = 77U;
    image.height = 88U;
    image.rgba8 = {9U, 8U, 7U, 6U};
    std::vector<TextureDiagnostic> diagnostics;
    auto bytes = MakeBmp24();
    bytes[0] = 'Z';
    Require(!DecodeBmp(bytes, "bad-signature.bmp", image, diagnostics), "Invalid signature must fail.");
    Require(!diagnostics.empty() && image.width == 77U && image.rgba8.size() == 4U,
            "Failure must diagnose and preserve the previously published image.");

    bytes = MakeBmp24();
    PutU32(bytes, 30U, 1U);
    Require(!DecodeBmp(bytes, "compressed.bmp", image, diagnostics), "Compressed BMP must fail explicitly.");
    bytes = MakeBmp24();
    PutU32(bytes, 22U, 0x80000000U);
    Require(!DecodeBmp(bytes, "overflow-height.bmp", image, diagnostics), "INT32_MIN height must be rejected.");
    bytes = MakeBmp24();
    bytes.resize(60U);
    Require(!DecodeBmp(bytes, "truncated.bmp", image, diagnostics), "Truncated pixel data must fail.");
}

} // namespace

int main()
{
    try
    {
        TestDecodesBottomUpPaddedBgrToTopDownRgba();
        TestDecodesTopDown32BitBgrxAsOpaque();
        TestLoadsBmpFileFromDisk();
        TestRejectsMalformedAndUnsupportedHeadersAtomically();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Texture importer test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All texture importer tests passed.\n";
    return 0;
}
