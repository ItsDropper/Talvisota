#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/MaterialDefinition.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/content/TextureSource.hpp"
#include "talvisota/content/ObjImporter.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
void PrintUsage(std::ostream& output)
{
    output << "Talvisota content validator\n"
           << "Usage: talvisota_content_validate <manifest-path> <content-root>\n"
           << "       talvisota_content_validate --help\n";
}

void PrintDiagnostic(const std::filesystem::path& file, std::size_t line,
                     const std::string& field, const std::string& message)
{
    std::cerr << file.generic_string();
    if (line != 0U)
        std::cerr << ':' << line;
    if (!field.empty())
        std::cerr << " [" << field << ']';
    std::cerr << ": " << message << '\n';
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        PrintUsage(std::cout);
        return 0;
    }
    if (argc != 3)
    {
        PrintUsage(std::cerr);
        return 2;
    }

    try
    {
        const std::filesystem::path manifestPath(argv[1]);
        const std::filesystem::path contentRoot(argv[2]);
        talvisota::content::ContentRegistry registry;
        std::vector<talvisota::content::ContentDiagnostic> diagnostics;
        if (!talvisota::content::LoadContentManifest(manifestPath, contentRoot, registry, diagnostics))
        {
            for (const auto& diagnostic : diagnostics)
                PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
            return 1;
        }

        std::size_t materialCount = 0;
        std::size_t worldCount = 0;
        std::size_t decodedTextureCount = 0;
        std::size_t importedModelCount = 0;
        for (const auto& asset : registry.Assets())
        {
            if (asset.kind == talvisota::content::AssetKind::Model)
            {
                if (asset.relativePath.extension() != ".obj")
                {
                    PrintDiagnostic(contentRoot / asset.relativePath, 0U, "format",
                                    "Only Wavefront OBJ model decoding is currently implemented; glTF/GLB are not yet supported.");
                    return 1;
                }
                talvisota::content::ObjMesh model;
                std::vector<talvisota::content::ModelDiagnostic> modelDiagnostics;
                if (!talvisota::content::LoadObj(contentRoot / asset.relativePath, {}, model, modelDiagnostics))
                {
                    for (const auto& diagnostic : modelDiagnostics)
                        PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
                    return 1;
                }
                ++importedModelCount;
                continue;
            }
            if (asset.kind == talvisota::content::AssetKind::Texture)
            {
                talvisota::content::DecodedTexture texture;
                std::vector<talvisota::content::TextureDiagnostic> textureDiagnostics;
                if (!talvisota::content::LoadTextureSource(contentRoot / asset.relativePath,
                                                           texture, textureDiagnostics))
                {
                    for (const auto& diagnostic : textureDiagnostics)
                        PrintDiagnostic(diagnostic.file, 0U, diagnostic.field, diagnostic.message);
                    return 1;
                }
                ++decodedTextureCount;
                continue;
            }
            if (asset.kind == talvisota::content::AssetKind::Definition &&
                asset.relativePath.extension() == ".tworld")
            {
                talvisota::content::WorldDefinition world;
                std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
                if (!talvisota::content::LoadWorldDefinition(contentRoot / asset.relativePath, registry,
                                                             world, worldDiagnostics))
                {
                    for (const auto& diagnostic : worldDiagnostics)
                        PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
                    return 1;
                }
                ++worldCount;
                continue;
            }
            if (asset.kind != talvisota::content::AssetKind::Material)
                continue;

            talvisota::content::MaterialDefinition material;
            std::vector<talvisota::content::MaterialDiagnostic> materialDiagnostics;
            if (!talvisota::content::LoadMaterialDefinition(contentRoot / asset.relativePath, registry,
                                                            material, materialDiagnostics))
            {
                for (const auto& diagnostic : materialDiagnostics)
                    PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
                return 1;
            }
            ++materialCount;
        }

        std::cout << "Validated " << registry.Assets().size() << " content assets, "
                  << materialCount << " typed materials, " << worldCount << " typed worlds, "
                  << decodedTextureCount << " decoded BMP/TGA textures, and "
                  << importedModelCount << " imported OBJ models.\n";
        for (const std::size_t index : registry.LoadOrder())
            std::cout << "  " << registry.Assets()[index].id << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Content validation failed: " << exception.what() << '\n';
        return 1;
    }
}
