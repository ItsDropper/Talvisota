#include "talvisota/content/TextureSource.hpp"

#include "talvisota/content/TgaImporter.hpp"

#include <algorithm>
#include <cctype>

namespace talvisota::content
{
bool LoadTextureSource(const std::filesystem::path& path, DecodedTexture& output,
                       std::vector<TextureDiagnostic>& diagnostics)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value)
    {
        return static_cast<char>(std::tolower(value));
    });
    if (extension == ".bmp")
        return LoadBmp(path, output, diagnostics);
    if (extension == ".tga")
        return LoadTga(path, output, diagnostics);
    diagnostics.clear();
    diagnostics.push_back({path, "format",
        "No decoder is implemented for this texture extension. Supported source formats are BMP and TGA."});
    return false;
}
} // namespace talvisota::content
