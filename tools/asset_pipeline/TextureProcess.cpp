#include "talvisota/content/TextureProcessing.hpp"
#include "talvisota/content/TextureSource.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Talvisota texture processor\n"
                  << "Usage: talvisota_texture_process <source.bmp|source.tga> <output.ttex> <srgb|linear>\n";
        return 0;
    }
    if (argc != 4)
    {
        std::cerr << "Usage: talvisota_texture_process <source.bmp|source.tga> <output.ttex> <srgb|linear>\n";
        return 2;
    }
    const std::string_view colorArgument(argv[3]);
    talvisota::content::TextureColorSpace colorSpace;
    if (colorArgument == "srgb")
        colorSpace = talvisota::content::TextureColorSpace::Srgb;
    else if (colorArgument == "linear")
        colorSpace = talvisota::content::TextureColorSpace::Linear;
    else
    {
        std::cerr << "Colour space must be exactly 'srgb' or 'linear'.\n";
        return 2;
    }

    talvisota::content::DecodedTexture decoded;
    std::vector<talvisota::content::TextureDiagnostic> decodeDiagnostics;
    if (!talvisota::content::LoadTextureSource(argv[1], decoded, decodeDiagnostics))
    {
        for (const auto& diagnostic : decodeDiagnostics)
            std::cerr << diagnostic.file.generic_string() << " [" << diagnostic.field << "]: "
                      << diagnostic.message << '\n';
        return 1;
    }
    std::vector<talvisota::content::TextureMip> mipChain;
    std::vector<talvisota::content::TextureProcessingDiagnostic> processingDiagnostics;
    if (!talvisota::content::BuildTextureMipChain(decoded, colorSpace, mipChain, processingDiagnostics) ||
        !talvisota::content::WriteProcessedTexture(argv[2], mipChain, colorSpace, processingDiagnostics))
    {
        for (const auto& diagnostic : processingDiagnostics)
            std::cerr << diagnostic.file.generic_string() << " [" << diagnostic.field << "]: "
                      << diagnostic.message << '\n';
        return 1;
    }
    std::cout << "Processed " << decoded.width << 'x' << decoded.height << " texture into "
              << mipChain.size() << " mip levels: " << argv[2] << '\n';
    return 0;
}
