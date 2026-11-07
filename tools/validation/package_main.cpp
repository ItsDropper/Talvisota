#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/MaterialDefinition.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/content/AuthoredDefinition.hpp"
#include "talvisota/content/VantaAssetPackage.hpp"
#include "talvisota/content/VantaAssetValidation.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
std::string LowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

bool ValidateTypedContent(const talvisota::content::ContentRegistry& registry)
{
    using namespace talvisota::content;
    std::unordered_map<std::string, WorldDefinition> worlds;
    for (const AssetRecord& asset : registry.Assets())
    {
        if (asset.kind != AssetKind::Definition || LowerExtension(asset.relativePath) != ".tworld")
            continue;
        const auto path = registry.Root() / asset.relativePath;
        if (!asset.required)
        {
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            if (error)
            {
                std::cerr << path.generic_string() << " [path]: Could not inspect optional world definition.\n";
                return false;
            }
            if (!exists)
                continue;
        }
        WorldDefinition definition;
        std::vector<WorldDiagnostic> diagnostics;
        if (!LoadWorldDefinition(path, registry, definition, diagnostics))
        {
            for (const auto& item : diagnostics)
                std::cerr << item.file.generic_string() << ':' << item.line
                          << " [" << item.field << "]: " << item.message << '\n';
            return false;
        }
        worlds.emplace(definition.id, std::move(definition));
    }

    for (const AssetRecord& asset : registry.Assets())
    {
        const auto path = registry.Root() / asset.relativePath;
        if (!asset.required)
        {
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            if (error)
            {
                std::cerr << path.generic_string() << " [path]: Could not inspect optional asset.\n";
                return false;
            }
            if (!exists)
                continue;
        }
        if (asset.kind == AssetKind::Definition)
        {
            const std::string extension = LowerExtension(asset.relativePath);
            if (extension == ".tworld")
            {
                continue;
            }
            else if (extension == ".tregion")
            {
                const WorldDefinition* parentWorld = nullptr;
                for (const auto& dependency : asset.dependencies)
                {
                    const auto* dependencyAsset = registry.Find(dependency);
                    if (dependencyAsset != nullptr && dependencyAsset->kind == AssetKind::Definition &&
                        LowerExtension(dependencyAsset->relativePath) == ".tworld")
                    {
                        if (parentWorld != nullptr)
                        {
                            std::cerr << path.generic_string()
                                      << " [world]: Region must declare exactly one parent world dependency.\n";
                            return false;
                        }
                        const auto found = worlds.find(dependency);
                        if (found != worlds.end())
                            parentWorld = &found->second;
                    }
                }
                if (parentWorld == nullptr)
                {
                    std::cerr << path.generic_string()
                              << " [world]: Region parent world dependency is missing or invalid.\n";
                    return false;
                }
                talvisota::world::WorldRegion region;
                std::vector<talvisota::world::WorldRegionDiagnostic> diagnostics;
                if (!talvisota::world::LoadWorldRegion(path, registry, *parentWorld, region, diagnostics))
                {
                    for (const auto& item : diagnostics)
                        std::cerr << item.file.generic_string() << ':' << item.line
                                  << " [" << item.field << "]: " << item.message << '\n';
                    return false;
                }
            }
            else if (extension == ".tmission" || extension == ".tfaction" ||
                     extension == ".tcharacter" || extension == ".tequipment")
            {
                AuthoredDefinition definition;
                std::vector<AuthoredDefinitionDiagnostic> diagnostics;
                if (!LoadAuthoredDefinition(path, registry, definition, diagnostics))
                {
                    for (const auto& item : diagnostics)
                        std::cerr << item.file.generic_string() << ':' << item.line
                                  << " [" << item.field << "]: " << item.message << '\\n';
                    return false;
                }
            }
            else
            {
                std::cerr << path.generic_string()
                          << " [format]: Definition extension has no implemented typed parser.\n";
                return false;
            }
        }
        else if (asset.kind == AssetKind::Material && LowerExtension(asset.relativePath) == ".tmat")
        {
            MaterialDefinition definition;
            std::vector<MaterialDiagnostic> diagnostics;
            if (!LoadMaterialDefinition(path, registry, definition, diagnostics))
            {
                for (const auto& item : diagnostics)
                    std::cerr << item.file.generic_string() << ':' << item.line
                              << " [" << item.field << "]: " << item.message << '\n';
                return false;
            }
        }
    }
    return true;
}
}

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
    if (!ValidateTypedContent(registry))
        return 1;
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
