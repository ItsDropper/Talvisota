#include "talvisota/content/TgaImporter.hpp"

#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace
{
using namespace talvisota::content;
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::uint8_t> Header(std::uint8_t type, std::uint16_t width, std::uint16_t height,
                                 std::uint8_t depth, std::uint8_t descriptor)
{
    std::vector<std::uint8_t> bytes(18U, 0U);
    bytes[2] = type;
    bytes[12] = static_cast<std::uint8_t>(width & 0xffU);
    bytes[13] = static_cast<std::uint8_t>(width >> 8U);
    bytes[14] = static_cast<std::uint8_t>(height & 0xffU);
    bytes[15] = static_cast<std::uint8_t>(height >> 8U);
    bytes[16] = depth;
    bytes[17] = descriptor;
    return bytes;
}

void TestRawTgaNormalizesBgrAndOrigin()
{
    auto bytes = Header(2U, 2U, 1U, 24U, 0x10U); // right-origin
    const std::uint8_t pixels[] = {0U, 255U, 0U, 0U, 0U, 255U}; // green, red in file order
    bytes.insert(bytes.end(), std::begin(pixels), std::end(pixels));
    DecodedTexture image;
    std::vector<TextureDiagnostic> diagnostics;
    Require(DecodeTga(bytes, "right-origin.tga", image, diagnostics), "Valid raw TGA should decode.");
    Require(image.rgba8 == std::vector<std::uint8_t>({
        255U,0U,0U,255U, 0U,255U,0U,255U}),
        "TGA right-origin and BGR channels should normalize to top-left RGBA.");
}

void TestRleTgaExpandsRunPackets()
{
    auto bytes = Header(10U, 2U, 1U, 32U, 0x28U); // top-left, eight alpha bits
    bytes.insert(bytes.end(), {0x81U, 30U, 20U, 10U, 40U});
    DecodedTexture image;
    std::vector<TextureDiagnostic> diagnostics;
    Require(DecodeTga(bytes, "rle.tga", image, diagnostics), "Valid RLE TGA should decode.");
    Require(image.rgba8 == std::vector<std::uint8_t>({
        10U,20U,30U,40U, 10U,20U,30U,40U}),
        "RLE run packets should expand to canonical RGBA.");
}

void TestMalformedTgaFailsWithoutPublishing()
{
    auto bytes = Header(10U, 2U, 1U, 24U, 0x20U);
    bytes.push_back(0x82U); // three pixels in a two-pixel image
    DecodedTexture image;
    image.width = 7U;
    image.height = 9U;
    image.rgba8 = {1U,2U,3U,4U};
    std::vector<TextureDiagnostic> diagnostics;
    Require(!DecodeTga(bytes, "bad-rle.tga", image, diagnostics), "Overlong RLE packet must fail.");
    Require(!diagnostics.empty() && image.width == 7U && image.rgba8.size() == 4U,
            "Failed TGA decoding must preserve the caller's previous image.");
}
}

int main()
{
    try
    {
        TestRawTgaNormalizesBgrAndOrigin();
        TestRleTgaExpandsRunPackets();
        TestMalformedTgaFailsWithoutPublishing();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "TGA importer test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All TGA importer tests passed.\n";
    return 0;
}
