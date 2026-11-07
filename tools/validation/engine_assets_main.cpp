#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/VantaAssetValidation.hpp"

#include <iostream>

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Talvisota engine-backed asset validator\n"
                  << "Usage: talvisota_asset_validate <manifest-path> <content-root>\n";
        return 0;
    }
    if (argc != 3)
    {
        std::cerr << "Usage: talvisota_asset_validate <manifest-path> <content-root>\n";
        return 2;
    }
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    if (!talvisota::content::LoadContentManifest(argv[1], argv[2], registry, manifestDiagnostics))
    {
        for (const auto& diagnostic : manifestDiagnostics)
            std::cerr << diagnostic.file.generic_string() << ':' << diagnostic.line
                      << " [" << diagnostic.field << "]: " << diagnostic.message << '\n';
        return 1;
    }
    talvisota::content::EngineAssetSummary summary;
    std::vector<talvisota::content::EngineAssetDiagnostic> diagnostics;
    if (!talvisota::content::ValidateEngineAssets(registry, summary, diagnostics))
    {
        for (const auto& diagnostic : diagnostics)
            std::cerr << diagnostic.file.generic_string() << " [" << diagnostic.field
                      << "]: " << diagnostic.message << '\n';
        return 1;
    }
    std::cout << "Imported " << summary.importedModels << " models, decoded "
              << summary.decodedTextures << " unique textures and validated "
              << summary.decodedAudio << " audio assets using CPU asset services.\n";
    return 0;
}
