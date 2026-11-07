#include "talvisota/content/VantaAssetPackage.hpp"
#include "talvisota/content/AuthoredDefinition.hpp"
#include "talvisota/content/MaterialDefinition.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <locale>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace talvisota::content
{
namespace
{
constexpr std::string_view PackageHeader = "talvisota-content-package\t1";
constexpr std::string_view InventoryName = "package.tmanifest";
constexpr std::string_view CatalogName = "content.tmanifest";

void Add(std::vector<EngineAssetDiagnostic>& diagnostics, const std::filesystem::path& path,
         std::string field, std::string message)
{
    diagnostics.push_back({path, std::move(field), std::move(message)});
}

[[nodiscard]] bool IsWithin(const std::filesystem::path& root,
                            const std::filesystem::path& candidate)
{
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute())
        return false;
    for (const auto& component : relative)
        if (component == "..")
            return false;
    return true;
}

[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path)
{
    const std::u8string encoded = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

[[nodiscard]] std::string KindName(const AssetKind kind)
{
    switch (kind)
    {
    case AssetKind::Model: return "model";
    case AssetKind::Texture: return "texture";
    case AssetKind::Audio: return "audio";
    case AssetKind::Definition: return "definition";
    case AssetKind::Material: return "material";
    }
    return {};
}

[[nodiscard]] bool HashFile(const std::filesystem::path& path, std::string& output,
                            std::uint64_t& size, std::string& error)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<UCHAR> hashObject;
    std::array<UCHAR, 32> digest{};
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    DWORD digestLength = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status))
    {
        error = "Could not open the Windows SHA-256 provider.";
        return false;
    }
    const auto close = [&]()
    {
        if (hash != nullptr) BCryptDestroyHash(hash);
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    };
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0);
    if (!BCRYPT_SUCCESS(status) ||
        !BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&digestLength), sizeof(digestLength), &resultLength, 0)) ||
        digestLength != digest.size())
    {
        error = "Could not query SHA-256 provider dimensions.";
        close();
        return false;
    }
    hashObject.resize(objectLength);
    status = BCryptCreateHash(algorithm, &hash, hashObject.data(), objectLength, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status))
    {
        error = "Could not create SHA-256 hash state.";
        close();
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        error = "Could not open package file for hashing.";
        close();
        return false;
    }
    std::array<char, 64U * 1024U> buffer{};
    size = 0;
    while (input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0)
        {
            status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                                    static_cast<ULONG>(count), 0);
            if (!BCRYPT_SUCCESS(status))
            {
                error = "SHA-256 update failed.";
                close();
                return false;
            }
            if (size > std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(count))
            {
                error = "File size exceeds the supported 64-bit range.";
                close();
                return false;
            }
            size += static_cast<std::uint64_t>(count);
        }
    }
    if (!input.eof())
    {
        error = "I/O error while hashing package file.";
        close();
        return false;
    }
    status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    close();
    if (!BCRYPT_SUCCESS(status))
    {
        error = "SHA-256 finalization failed.";
        return false;
    }
    constexpr char digits[] = "0123456789abcdef";
    output.clear();
    output.reserve(digest.size() * 2U);
    for (const UCHAR value : digest)
    {
        output.push_back(digits[value >> 4U]);
        output.push_back(digits[value & 0x0fU]);
    }
    return true;
}

[[nodiscard]] std::vector<std::string_view> SplitTabs(const std::string& line)
{
    std::vector<std::string_view> fields;
    std::string_view remaining(line);
    for (;;)
    {
        const std::size_t separator = remaining.find('\t');
        if (separator == std::string_view::npos)
        {
            fields.push_back(remaining);
            return fields;
        }
        fields.push_back(remaining.substr(0, separator));
        remaining.remove_prefix(separator + 1U);
    }
}

[[nodiscard]] bool ParseSize(std::string_view text, std::uint64_t& value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

[[nodiscard]] bool IsStableId(std::string_view value)
{
    if (value.empty() || !((value.front() >= 'a' && value.front() <= 'z') ||
                           (value.front() >= '0' && value.front() <= '9')))
        return false;
    return std::all_of(value.begin(), value.end(), [](char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '.' || c == '_' || c == '-' || c == ':';
    });
}

[[nodiscard]] bool IsSafeInventoryField(std::string_view value)
{
    return !value.empty() && value != "-" &&
        std::all_of(value.begin(), value.end(), [](unsigned char character)
        {
            return character >= 0x20U && character != 0x7fU;
        });
}


[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

[[nodiscard]] bool WriteCatalogManifest(const ContentRegistry& registry,
                                        const std::filesystem::path& destination,
                                        std::string& error)
{
    std::vector<const AssetRecord*> assets;
    assets.reserve(registry.Assets().size());
    for (const AssetRecord& asset : registry.Assets())
        assets.push_back(&asset);
    std::sort(assets.begin(), assets.end(), [](const AssetRecord* left, const AssetRecord* right)
    {
        return left->id < right->id;
    });

    std::ostringstream manifest;
    manifest.imbue(std::locale::classic());
    manifest << "talvisota-content-manifest\t1\n";
    for (const AssetRecord* asset : assets)
    {
        std::vector<std::string> dependencies = asset->dependencies;
        std::sort(dependencies.begin(), dependencies.end());
        std::string dependencyList = "-";
        if (!dependencies.empty())
        {
            dependencyList.clear();
            for (std::size_t index = 0; index < dependencies.size(); ++index)
            {
                if (index != 0U)
                    dependencyList.push_back(',');
                dependencyList += dependencies[index];
            }
        }
        manifest << "asset\t" << asset->id << '\t' << KindName(asset->kind) << '\t'
                 << PathUtf8(asset->relativePath.lexically_normal()) << '\t'
                 << (asset->required ? "required" : "optional") << '\t'
                 << dependencyList << '\t' << asset->source << '\t' << asset->license << '\n';
    }

    const std::string content = manifest.str();
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output)
    {
        error = "Could not write packaged content catalog.";
        return false;
    }
    return true;
}

[[nodiscard]] bool ValidateTypedDefinitions(const ContentRegistry& registry,
                                            std::vector<EngineAssetDiagnostic>& diagnostics)
{
    std::unordered_map<std::string, WorldDefinition> worlds;
    for (const AssetRecord& asset : registry.Assets())
    {
        if (asset.kind != AssetKind::Definition || LowerExtension(asset.relativePath) != ".tworld")
            continue;
        const auto path = registry.Root() / asset.relativePath;
        WorldDefinition definition;
        std::vector<WorldDiagnostic> issues;
        if (!LoadWorldDefinition(path, registry, definition, issues))
        {
            for (const auto& issue : issues)
                Add(diagnostics, issue.file, issue.field, issue.message);
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
                Add(diagnostics, path, "path", "Could not inspect optional packaged asset.");
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
                            Add(diagnostics, path, "world",
                                "Region must declare exactly one parent world dependency.");
                            return false;
                        }
                        const auto found = worlds.find(dependency);
                        if (found != worlds.end())
                            parentWorld = &found->second;
                    }
                }
                if (parentWorld == nullptr)
                {
                    Add(diagnostics, path, "world", "Region parent world dependency is missing or invalid.");
                    return false;
                }
                world::WorldRegion region;
                std::vector<world::WorldRegionDiagnostic> issues;
                if (!world::LoadWorldRegion(path, registry, *parentWorld, region, issues))
                {
                    for (const auto& issue : issues)
                        Add(diagnostics, issue.file, issue.field, issue.message);
                    return false;
                }
            }
            else if (extension == ".tmission" || extension == ".tfaction" ||
                     extension == ".tcharacter" || extension == ".tequipment")
            {
                AuthoredDefinition definition;
                std::vector<AuthoredDefinitionDiagnostic> issues;
                if (!LoadAuthoredDefinition(path, registry, definition, issues))
                {
                    for (const auto& issue : issues)
                        Add(diagnostics, issue.file, issue.field, issue.message);
                    return false;
                }
            }
            else
            {
                Add(diagnostics, path, "format",
                    "Packaged definition has no implemented typed parser.");
                return false;
            }
        }
        else if (asset.kind == AssetKind::Material && LowerExtension(asset.relativePath) == ".tmat")
        {
            MaterialDefinition definition;
            std::vector<MaterialDiagnostic> issues;
            if (!LoadMaterialDefinition(path, registry, definition, issues))
            {
                for (const auto& issue : issues)
                    Add(diagnostics, issue.file, issue.field, issue.message);
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool ValidatePackageSemantics(const ContentRegistry& registry,
                                            std::vector<EngineAssetDiagnostic>& diagnostics)
{
    if (!ValidateTypedDefinitions(registry, diagnostics))
        return false;
    EngineAssetSummary media;
    return ValidateEngineAssets(registry, media, diagnostics);
}

[[nodiscard]] bool IsHexDigest(std::string_view value)
{
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
} // namespace

bool ValidateAssetPackage(const std::filesystem::path& packageRoot, AssetPackageSummary& output,
                          std::vector<EngineAssetDiagnostic>& diagnostics)
{
    diagnostics.clear();
    AssetPackageSummary candidate;
    std::error_code error;
    const auto root = std::filesystem::canonical(packageRoot, error);
    if (error || !std::filesystem::is_directory(root, error) || error)
    {
        Add(diagnostics, packageRoot, "packageRoot", "Package root is not an accessible directory.");
        return false;
    }
    std::ifstream inventory(root / InventoryName, std::ios::binary);
    if (!inventory)
    {
        Add(diagnostics, root / InventoryName, "inventory", "Package inventory is missing.");
        return false;
    }
    std::string line;
    if (!std::getline(inventory, line) || line != PackageHeader)
    {
        Add(diagnostics, root / InventoryName, "header", "Unsupported package inventory version.");
        return false;
    }
    if (!std::getline(inventory, line))
    {
        Add(diagnostics, root / InventoryName, "catalog", "Package inventory is missing its content catalog record.");
        return false;
    }
    const auto catalogFields = SplitTabs(line);
    std::uint64_t expectedCatalogSize = 0U;
    if (catalogFields.size() != 3U || catalogFields[0] != "catalog" ||
        !ParseSize(catalogFields[1], expectedCatalogSize) || !IsHexDigest(catalogFields[2]))
    {
        Add(diagnostics, root / InventoryName, "catalog", "Malformed package content catalog record.");
        return false;
    }
    const auto catalogPath = std::filesystem::canonical(root / CatalogName, error);
    if (error || !IsWithin(root, catalogPath) ||
        !std::filesystem::is_regular_file(catalogPath, error) || error)
    {
        Add(diagnostics, root / CatalogName, "catalog", "Packaged content catalog is missing or escapes package root.");
        return false;
    }
    std::string actualCatalogDigest;
    std::uint64_t actualCatalogSize = 0U;
    std::string catalogHashError;
    if (!HashFile(catalogPath, actualCatalogDigest, actualCatalogSize, catalogHashError) ||
        actualCatalogSize != expectedCatalogSize || actualCatalogDigest != catalogFields[2])
    {
        Add(diagnostics, catalogPath, "catalogIntegrity",
            catalogHashError.empty() ? "Packaged content catalog size or SHA-256 does not match inventory."
                                     : catalogHashError);
        return false;
    }

    struct InventoryRecord final
    {
        std::string kind;
        std::string path;
        std::string source;
        std::string license;
        std::uint64_t size = 0U;
        std::string digest;
    };
    std::unordered_map<std::string, InventoryRecord> records;
    std::set<std::string> ids;
    std::set<std::string> paths;
    while (std::getline(inventory, line))
    {
        if (line.empty())
            continue;
        const auto fields = SplitTabs(line);
        if (fields.size() != 8U || fields[0] != "asset" || !IsStableId(fields[1]) ||
            (fields[2] != "model" && fields[2] != "texture" && fields[2] != "audio" &&
             fields[2] != "definition" && fields[2] != "material") ||
            fields[3].empty() || fields[3] == InventoryName ||
            fields[3].find('\\') != std::string_view::npos ||
            fields[3].find('\0') != std::string_view::npos || !IsHexDigest(fields[5]) ||
            !IsSafeInventoryField(fields[6]) || !IsSafeInventoryField(fields[7]) ||
            !ids.insert(std::string(fields[1])).second ||
            !paths.insert(std::string(fields[3])).second)
        {
            Add(diagnostics, root / InventoryName, "record", "Malformed or duplicate package inventory record.");
            return false;
        }
        std::uint64_t expectedSize = 0;
        if (!ParseSize(fields[4], expectedSize))
        {
            Add(diagnostics, root / InventoryName, "size", "Invalid package asset size.");
            return false;
        }
        std::u8string utf8;
        for (char c : fields[3]) utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
        const std::filesystem::path relative(utf8);
        if (relative.empty() || relative.is_absolute() || relative.has_root_name())
        {
            Add(diagnostics, root / InventoryName, "path", "Package path must be relative.");
            return false;
        }
        for (const auto& component : relative)
        {
            if (component == "..")
            {
                Add(diagnostics, root / InventoryName, "path", "Package path traversal is forbidden.");
                return false;
            }
        }
        const auto file = std::filesystem::canonical(root / relative, error);
        if (error || !IsWithin(root, file) || !std::filesystem::is_regular_file(file, error) || error)
        {
            Add(diagnostics, root / relative, "path", "Packaged asset is missing, non-regular, or escapes package root.");
            return false;
        }
        std::string actualDigest;
        std::uint64_t actualSize = 0;
        std::string hashError;
        if (!HashFile(file, actualDigest, actualSize, hashError) ||
            actualSize != expectedSize || actualDigest != fields[5])
        {
            Add(diagnostics, file, "integrity",
                hashError.empty() ? "Package size or SHA-256 digest does not match inventory." : hashError);
            return false;
        }
        if (candidate.totalBytes > std::numeric_limits<std::uint64_t>::max() - actualSize)
        {
            Add(diagnostics, file, "size", "Package total byte count overflows the supported range.");
            return false;
        }
        records.emplace(std::string(fields[1]), InventoryRecord{
            std::string(fields[2]), std::string(fields[3]), std::string(fields[6]),
            std::string(fields[7]), actualSize, actualDigest});
        ++candidate.assetCount;
        candidate.totalBytes += actualSize;
    }
    if (!inventory.eof())
    {
        Add(diagnostics, root / InventoryName, "inventory", "I/O error reading package inventory.");
        return false;
    }

    ContentRegistry packagedRegistry;
    std::vector<ContentDiagnostic> catalogDiagnostics;
    if (!LoadContentManifest(catalogPath, root, packagedRegistry, catalogDiagnostics))
    {
        for (const auto& issue : catalogDiagnostics)
            Add(diagnostics, issue.file, issue.field, issue.message);
        return false;
    }
    std::size_t presentCatalogAssets = 0U;
    for (const AssetRecord& asset : packagedRegistry.Assets())
    {
        std::error_code existsError;
        const bool exists = std::filesystem::exists(root / asset.relativePath, existsError);
        if (existsError)
        {
            Add(diagnostics, root / asset.relativePath, "path", "Could not inspect packaged asset referenced by catalog.");
            return false;
        }
        if (!exists && !asset.required)
            continue;
        ++presentCatalogAssets;
        const auto found = records.find(asset.id);
        if (found == records.end() || found->second.kind != KindName(asset.kind) ||
            found->second.path != PathUtf8(asset.relativePath.lexically_normal()) ||
            found->second.source != asset.source || found->second.license != asset.license)
        {
            Add(diagnostics, root / asset.relativePath, "catalog",
                "Packaged catalog metadata does not match its integrity inventory record.");
            return false;
        }
    }
    for (const auto& [id, record] : records)
    {
        if (packagedRegistry.Find(id) == nullptr)
        {
            Add(diagnostics, root / record.path, "catalog",
                "Integrity inventory contains an asset ID absent from the packaged catalog.");
            return false;
        }
    }
    if (records.size() != presentCatalogAssets || records.size() != candidate.assetCount)
    {
        Add(diagnostics, catalogPath, "catalog", "Packaged catalog and inventory asset counts differ.");
        return false;
    }

    std::set<std::string> actualFiles{std::string(InventoryName), std::string(CatalogName)};
    for (std::filesystem::recursive_directory_iterator it(root, error), end; it != end && !error; it.increment(error))
    {
        const auto status = it->symlink_status(error);
        if (error || std::filesystem::is_symlink(status))
        {
            Add(diagnostics, it->path(), "path", "Package may not contain symbolic links.");
            return false;
        }
        if (std::filesystem::is_regular_file(status))
        {
            const auto relative = PathUtf8(it->path().lexically_relative(root));
            if (!actualFiles.insert(relative).second)
                continue;
        }
    }
    if (error)
    {
        Add(diagnostics, root, "enumeration", "Could not enumerate package files.");
        return false;
    }
    if (actualFiles.size() != paths.size() + 2U)
    {
        Add(diagnostics, root, "inventory", "Package contains unindexed files or missing inventory entries.");
        return false;
    }
    for (const auto& path : paths)
        if (!actualFiles.contains(path))
        {
            Add(diagnostics, root / path, "inventory", "Inventory entry does not correspond to a package file.");
            return false;
        }
    if (!ValidatePackageSemantics(packagedRegistry, diagnostics))
        return false;
    output = candidate;
    return true;
}

bool BuildAssetPackage(const ContentRegistry& registry, const std::filesystem::path& destination,
                       AssetPackageSummary& output, std::vector<EngineAssetDiagnostic>& diagnostics)
{
    diagnostics.clear();
    if (destination.empty())
    {
        Add(diagnostics, destination, "destination", "Package destination is empty.");
        return false;
    }
    std::error_code error;
    const auto root = std::filesystem::canonical(registry.Root(), error);
    if (error)
    {
        Add(diagnostics, registry.Root(), "contentRoot", "Could not canonicalize content root.");
        return false;
    }
    const auto requestedDestination = std::filesystem::absolute(destination, error).lexically_normal();
    if (error)
    {
        Add(diagnostics, destination, "destination", "Could not resolve package destination.");
        return false;
    }
    const auto parent = requestedDestination.parent_path();
    std::filesystem::create_directories(parent, error);
    if (error)
    {
        Add(diagnostics, parent, "destination", "Could not create package parent directory.");
        return false;
    }
    const auto canonicalParent = std::filesystem::canonical(parent, error);
    if (error)
    {
        Add(diagnostics, parent, "destination", "Could not canonicalize package parent directory.");
        return false;
    }
    const auto absoluteDestination = canonicalParent / requestedDestination.filename();
    if (IsWithin(root, absoluteDestination) || absoluteDestination == root)
    {
        Add(diagnostics, destination, "destination", "Package destination must be outside the source content root.");
        return false;
    }
    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto staging = parent / (absoluteDestination.filename().string() + ".staging-" + nonce);
    const auto backup = parent / (absoluteDestination.filename().string() + ".backup-" + nonce);
    std::filesystem::remove_all(staging, error);
    error.clear();
    std::filesystem::create_directories(staging, error);
    if (error)
    {
        Add(diagnostics, staging, "staging", "Could not create staging directory.");
        return false;
    }
    const auto cleanup = [&]() { std::error_code ignored; std::filesystem::remove_all(staging, ignored); };

    std::string catalogError;
    if (!WriteCatalogManifest(registry, staging / CatalogName, catalogError))
    {
        Add(diagnostics, staging / CatalogName, "catalog", catalogError);
        cleanup();
        return false;
    }
    std::string catalogDigest;
    std::uint64_t catalogSize = 0U;
    std::string catalogHashError;
    if (!HashFile(staging / CatalogName, catalogDigest, catalogSize, catalogHashError))
    {
        Add(diagnostics, staging / CatalogName, "catalogIntegrity", catalogHashError);
        cleanup();
        return false;
    }

    std::ostringstream inventory;
    inventory.imbue(std::locale::classic());
    inventory << PackageHeader << '\n'
              << "catalog\t" << catalogSize << '\t' << catalogDigest << '\n';
    AssetPackageSummary candidate;
    for (const std::size_t index : registry.LoadOrder())
    {
        const AssetRecord& asset = registry.Assets()[index];
        const auto source = std::filesystem::weakly_canonical(root / asset.relativePath, error);
        if (error)
        {
            Add(diagnostics, root / asset.relativePath, "source", "Could not resolve source asset.");
            cleanup();
            return false;
        }
        if (!std::filesystem::exists(source, error))
        {
            if (!asset.required && !error)
                continue;
            Add(diagnostics, source, "source", "Required source asset is missing.");
            cleanup();
            return false;
        }
        if (error || !IsWithin(root, source) || !std::filesystem::is_regular_file(source, error) || error)
        {
            Add(diagnostics, source, "source", "Source asset is not a regular file inside content root.");
            cleanup();
            return false;
        }
        const auto relative = asset.relativePath.lexically_normal();
        if (PathUtf8(relative) == InventoryName || PathUtf8(relative) == CatalogName)
        {
            Add(diagnostics, source, "path", "The reserved package inventory path cannot be used by a content asset.");
            cleanup();
            return false;
        }
        const auto target = staging / relative;
        std::filesystem::create_directories(target.parent_path(), error);
        if (error)
        {
            Add(diagnostics, target, "copy", "Could not create package asset directory.");
            cleanup();
            return false;
        }
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, error);
        if (error)
        {
            Add(diagnostics, source, "copy", "Could not copy source asset into package.");
            cleanup();
            return false;
        }
        std::string digest;
        std::uint64_t size = 0;
        std::string hashError;
        if (!HashFile(target, digest, size, hashError))
        {
            Add(diagnostics, target, "integrity", hashError);
            cleanup();
            return false;
        }
        inventory << "asset\t" << asset.id << '\t' << KindName(asset.kind) << '\t'
                  << PathUtf8(relative) << '\t' << size << '\t' << digest
                  << '\t' << asset.source << '\t' << asset.license << '\n';
        if (candidate.totalBytes > std::numeric_limits<std::uint64_t>::max() - size)
        {
            Add(diagnostics, target, "size", "Package total byte count overflows the supported range.");
            cleanup();
            return false;
        }
        ++candidate.assetCount;
        candidate.totalBytes += size;
    }
    {
        std::ofstream outputFile(staging / InventoryName, std::ios::binary | std::ios::trunc);
        const std::string text = inventory.str();
        outputFile.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!outputFile)
        {
            Add(diagnostics, staging / InventoryName, "inventory", "Could not write package inventory.");
            cleanup();
            return false;
        }
    }
    AssetPackageSummary verified;
    if (!ValidateAssetPackage(staging, verified, diagnostics))
    {
        cleanup();
        return false;
    }
    const bool destinationExists = std::filesystem::exists(absoluteDestination, error);
    if (error)
    {
        Add(diagnostics, absoluteDestination, "destination", "Could not inspect existing package destination.");
        cleanup();
        return false;
    }
    if (destinationExists)
    {
        if (!std::filesystem::is_directory(absoluteDestination, error) || error)
        {
            Add(diagnostics, absoluteDestination, "destination", "Existing package destination is not a directory.");
            cleanup();
            return false;
        }
        std::filesystem::rename(absoluteDestination, backup, error);
        if (error)
        {
            Add(diagnostics, absoluteDestination, "publish", "Could not stage existing package for replacement.");
            cleanup();
            return false;
        }
    }
    std::filesystem::rename(staging, absoluteDestination, error);
    if (error)
    {
        if (std::filesystem::exists(backup))
        {
            std::error_code rollbackError;
            std::filesystem::rename(backup, absoluteDestination, rollbackError);
        }
        Add(diagnostics, absoluteDestination, "publish", "Could not atomically publish validated package.");
        cleanup();
        return false;
    }
    if (std::filesystem::exists(backup))
        std::filesystem::remove_all(backup, error);
    output = verified;
    return true;
}
} // namespace talvisota::content
