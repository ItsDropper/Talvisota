#include "talvisota/content/TextureProcessing.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <stdexcept>
#include <vector>

namespace
{
using namespace talvisota::content;
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void TestSrgbMipFilteringAndDeterministicContainer()
{
    DecodedTexture source;
    source.width = 2U;
    source.height = 2U;
    source.rgba8 = {
        0U,0U,0U,0U, 255U,255U,255U,255U,
        0U,0U,0U,0U, 255U,255U,255U,255U
    };
    std::vector<TextureMip> mips;
    std::vector<TextureProcessingDiagnostic> diagnostics;
    Require(BuildTextureMipChain(source, TextureColorSpace::Srgb, mips, diagnostics),
            "Valid source should produce a mip chain.");
    Require(mips.size() == 2U && mips[1].width == 1U && mips[1].height == 1U,
            "2x2 source should produce a terminal 1x1 mip.");
    Require(mips[1].rgba8 == std::vector<std::uint8_t>({188U,188U,188U,128U}),
            "sRGB RGB channels must be averaged in linear light while alpha remains linear.");

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("talvisota-processed-texture-" + std::to_string(stamp));
    const auto destination = directory / "snow.ttex";
    Require(WriteProcessedTexture(destination, mips, TextureColorSpace::Srgb, diagnostics),
            "Valid mip chain should serialize.");
    std::ifstream firstStream(destination, std::ios::binary);
    const std::vector<char> first((std::istreambuf_iterator<char>(firstStream)), {});
    Require(WriteProcessedTexture(destination, mips, TextureColorSpace::Srgb, diagnostics),
            "Repeated serialization should replace the output.");
    std::ifstream secondStream(destination, std::ios::binary);
    const std::vector<char> second((std::istreambuf_iterator<char>(secondStream)), {});
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    Require(first == second, "Repeated serialization must produce byte-identical output.");
    Require(first.size() > 32U && std::string(first.begin(), first.begin() + 8) == "TVTTEX01",
            "Processed output must carry the documented versioned magic.");
}

void TestRejectsIncompleteMipChains()
{
    std::vector<TextureMip> invalid = {{2U,2U,std::vector<std::uint8_t>(16U,0U)}};
    std::vector<TextureProcessingDiagnostic> diagnostics;
    const auto path = std::filesystem::temp_directory_path() / "invalid-mip-chain.ttex";
    Require(!WriteProcessedTexture(path, invalid, TextureColorSpace::Linear, diagnostics),
            "Container writer must reject a mip chain that does not terminate at 1x1.");
    Require(!diagnostics.empty(), "Invalid mip chain must produce a diagnostic.");
}
}

int main()
{
    try
    {
        TestSrgbMipFilteringAndDeterministicContainer();
        TestRejectsIncompleteMipChains();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Texture processing test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All texture processing tests passed.\n";
    return 0;
}
