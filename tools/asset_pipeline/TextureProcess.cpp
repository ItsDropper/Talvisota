#include "vanta/assets/TextureProcessing.hpp"
#include "vanta/assets/WicTextureLoader.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Talvisota texture processor (VantaEngine WIC + CPU mip pipeline)\n"
                  << "Usage: talvisota_texture_process <source-image> <output.ttex> <srgb|linear>\n";
        return 0;
    }
    if (argc != 4)
    {
        std::cerr << "Usage: talvisota_texture_process <source-image> <output.ttex> <srgb|linear>\n";
        return 2;
    }
    const std::filesystem::path sourcePath(argv[1]);
    const std::filesystem::path outputPath(argv[2]);
    if (outputPath.extension() != ".ttex")
    {
        std::cerr << "Output path must use the .ttex extension.\n";
        return 2;
    }
    vanta::assets::TextureColorSpace colorSpace;
    const std::string_view colorArgument(argv[3]);
    if (colorArgument == "srgb")
        colorSpace = vanta::assets::TextureColorSpace::Srgb;
    else if (colorArgument == "linear")
        colorSpace = vanta::assets::TextureColorSpace::Linear;
    else
    {
        std::cerr << "Colour space must be exactly 'srgb' or 'linear'.\n";
        return 2;
    }

    vanta::assets::CpuTexture source;
    if (!vanta::assets::LoadWicTexture(sourcePath, source))
    {
        std::cerr << "VantaEngine WIC could not decode the source image.\n";
        return 1;
    }
    std::vector<vanta::assets::TextureMip> mipChain;
    std::string error;
    if (!vanta::assets::BuildTextureMipChain(source, colorSpace, mipChain, error) ||
        !vanta::assets::WriteProcessedTexture(outputPath, mipChain, colorSpace, error))
    {
        std::cerr << error << '\n';
        return 1;
    }
    std::cout << "Processed " << source.width << 'x' << source.height
              << " into " << mipChain.size() << " mip levels: " << outputPath.string() << '\n';
    return 0;
}
