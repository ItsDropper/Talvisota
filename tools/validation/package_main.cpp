#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/VantaAssetPackage.hpp"
#include "talvisota/content/VantaAssetValidation.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Talvisota deterministic content package tool\n"
                  << "Usage: talvisota_content_package <manifest-path> <content-root> <package-dir>\n"
                  << "       talvisota_content_package --verify <package-dir>\n";
        return 0;
    }
    std::vector<talvisota::content::EngineAssetDiagnostic> diagnostics;
    talvisota::content::AssetPackageSummary summary;
    if (argc == 3 && std::string_view(argv[1]) == "--verify")
    {
        if (!talvisota::content::ValidateAssetPackage(argv[2], summary, diagnostics))
        {
            for (const auto& item : diagnostics)
                std::cerr << item.file.generic_string() << " [" << item.field << "]: " << item.message << '\n';
            return 1;
        }
        std::cout << "Verified " << summary.assetCount << " package assets (" << summary.totalBytes << " bytes).\n";
        return 0;
    }
    if (argc != 4)
    {
        std::cerr << "Usage: talvisota_content_package <manifest-path> <content-root> <package-dir>\n";
        return 2;
    }
    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    if (!talvisota::content::LoadContentManifest(argv[1], argv[2], registry, manifestDiagnostics))
    {
        for (const auto& item : manifestDiagnostics)
            std::cerr << item.file.generic_string() << ':' << item.line << " [" << item.field << "]: " << item.message << '\n';
        return 1;
    }
    talvisota::content::EngineAssetSummary assetSummary;
    if (!talvisota::content::ValidateEngineAssets(registry, assetSummary, diagnostics))
    {
        for (const auto& item : diagnostics)
            std::cerr << item.file.generic_string() << " [" << item.field << "]: " << item.message << '\n';
        return 1;
    }
    if (!talvisota::content::BuildAssetPackage(registry, argv[3], summary, diagnostics))
    {
        for (const auto& item : diagnostics)
            std::cerr << item.file.generic_string() << " [" << item.field << "]: " << item.message << '\n';
        return 1;
    }
    std::cout << "Packaged " << summary.assetCount << " assets (" << summary.totalBytes << " bytes); "
              << assetSummary.importedModels << " models, " << assetSummary.decodedTextures
              << " textures and " << assetSummary.decodedAudio << " audio assets validated.\n";
    return 0;
}
