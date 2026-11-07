#include "talvisota/content/VantaAssetPackage.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace talvisota::content
{
namespace
{
constexpr std::string_view PackageHeader = "talvisota-content-package\t1";
constexpr std::string_view InventoryName = "package.tmanifest";

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
    std::set<std::string> ids;
    std::set<std::string> paths;
    while (std::getline(inventory, line))
    {
        if (line.empty())
            continue;
        const auto fields = SplitTabs(line);
        if (fields.size() != 6U || fields[0] != "asset" || fields[1].empty() ||
            fields[2].empty() || fields[3].empty() || !IsHexDigest(fields[5]) ||
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
        ++candidate.assetCount;
        candidate.totalBytes += actualSize;
    }
    if (!inventory.eof())
    {
        Add(diagnostics, root / InventoryName, "inventory", "I/O error reading package inventory.");
        return false;
    }

    std::set<std::string> actualFiles{std::string(InventoryName)};
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
            const auto relative = it->path().lexically_relative(root).generic_string();
            if (!actualFiles.insert(relative).second)
                continue;
        }
    }
    if (error)
    {
        Add(diagnostics, root, "enumeration", "Could not enumerate package files.");
        return false;
    }
    if (actualFiles.size() != paths.size() + 1U)
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
    const auto absoluteDestination = std::filesystem::absolute(destination, error).lexically_normal();
    if (error)
    {
        Add(diagnostics, destination, "destination", "Could not resolve package destination.");
        return false;
    }
    if (IsWithin(root, absoluteDestination) || absoluteDestination == root)
    {
        Add(diagnostics, destination, "destination", "Package destination must be outside the source content root.");
        return false;
    }
    const auto parent = absoluteDestination.parent_path();
    std::filesystem::create_directories(parent, error);
    if (error)
    {
        Add(diagnostics, parent, "destination", "Could not create package parent directory.");
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

    std::ostringstream inventory;
    inventory << PackageHeader << '\n';
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
                  << relative.generic_string() << '\t' << size << '\t' << digest << '\n';
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
    if (std::filesystem::exists(absoluteDestination, error))
    {
        if (error || !std::filesystem::is_directory(absoluteDestination, error) || error)
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
