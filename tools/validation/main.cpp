#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/MaterialDefinition.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/content/AuthoredDefinition.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
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

        std::unordered_map<std::string, talvisota::content::WorldDefinition> worlds;
        for (const auto& asset : registry.Assets())
        {
            if (asset.kind != talvisota::content::AssetKind::Definition ||
                LowerExtension(asset.relativePath) != ".tworld")
                continue;
            if (!asset.required)
            {
                std::error_code optionalError;
                const bool present = std::filesystem::exists(contentRoot / asset.relativePath, optionalError);
                if (optionalError)
                {
                    PrintDiagnostic(contentRoot / asset.relativePath, 0U, "path", "Could not inspect optional world.");
                    return 1;
                }
                if (!present)
                    continue;
            }
            talvisota::content::WorldDefinition world;
            std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
            if (!talvisota::content::LoadWorldDefinition(contentRoot / asset.relativePath, registry,
                                                         world, worldDiagnostics))
            {
                for (const auto& diagnostic : worldDiagnostics)
                    PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
                return 1;
            }
            worlds.emplace(world.id, std::move(world));
        }

        std::size_t materialCount = 0;
        std::size_t worldCount = 0;
        std::size_t authoredDefinitionCount = 0;
        std::size_t regionCount = 0;
        for (const auto& asset : registry.Assets())
        {
            if (!asset.required)
            {
                std::error_code optionalError;
                const bool present = std::filesystem::exists(contentRoot / asset.relativePath, optionalError);
                if (optionalError)
                {
                    PrintDiagnostic(contentRoot / asset.relativePath, 0U, "path",
                                    "Could not inspect optional asset.");
                    return 1;
                }
                if (!present)
                    continue;
            }
            if (asset.kind == talvisota::content::AssetKind::Definition &&
                LowerExtension(asset.relativePath) == ".tworld")
            {
                ++worldCount;
                continue;
            }

            if (asset.kind == talvisota::content::AssetKind::Definition &&
                LowerExtension(asset.relativePath) == ".tregion")
            {
                const talvisota::content::WorldDefinition* parentWorld = nullptr;
                for (const auto& dependency : asset.dependencies)
                {
                    const auto* dependencyAsset = registry.Find(dependency);
                    if (dependencyAsset != nullptr &&
                        dependencyAsset->kind == talvisota::content::AssetKind::Definition &&
                        LowerExtension(dependencyAsset->relativePath) == ".tworld")
                    {
                        if (parentWorld != nullptr)
                        {
                            PrintDiagnostic(contentRoot / asset.relativePath, 0U, "world",
                                            "Region must declare exactly one parent world dependency.");
                            return 1;
                        }
                        const auto found = worlds.find(dependency);
                        if (found != worlds.end())
                            parentWorld = &found->second;
                    }
                }
                if (parentWorld == nullptr)
                {
                    PrintDiagnostic(contentRoot / asset.relativePath, 0U, "world",
                                    "Region parent world dependency is missing or invalid.");
                    return 1;
                }
                talvisota::world::WorldRegion region;
                std::vector<talvisota::world::WorldRegionDiagnostic> regionDiagnostics;
                if (!talvisota::world::LoadWorldRegion(contentRoot / asset.relativePath, registry,
                                                       *parentWorld, region, regionDiagnostics))
                {
                    for (const auto& diagnostic : regionDiagnostics)
                        PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
                    return 1;
                }
                ++regionCount;
                continue;
            }

            if (asset.kind == talvisota::content::AssetKind::Definition)
            {
                const std::string extension = LowerExtension(asset.relativePath);
                if (extension != ".tmission" && extension != ".tfaction" &&
                    extension != ".tcharacter" && extension != ".tequipment")
                {
                    PrintDiagnostic(contentRoot / asset.relativePath, 0U, "format",
                                    "Definition extension has no implemented typed parser.");
                    return 1;
                }
                talvisota::content::AuthoredDefinition definition;
                std::vector<talvisota::content::AuthoredDefinitionDiagnostic> definitionDiagnostics;
                if (!talvisota::content::LoadAuthoredDefinition(contentRoot / asset.relativePath,
                                                                registry, definition, definitionDiagnostics))
                {
                    for (const auto& diagnostic : definitionDiagnostics)
                        PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
                    return 1;
                }
                ++authoredDefinitionCount;
                continue;
            }
            if (asset.kind != talvisota::content::AssetKind::Material ||
                LowerExtension(asset.relativePath) == ".mtl")
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
                  << authoredDefinitionCount << " typed mission/faction/character/equipment definitions, "
                  << regionCount << " validated world regions.\n";
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
