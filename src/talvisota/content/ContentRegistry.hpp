#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace talvisota::content
{
enum class AssetKind
{
    Model,
    Texture,
    Audio,
    Definition,
    Material
};

struct AssetRecord final
{
    std::string id;
    AssetKind kind = AssetKind::Definition;
    std::filesystem::path relativePath;
    bool required = true;
    std::vector<std::string> dependencies;
    std::string source;
    std::string license;
};

struct ContentDiagnostic final
{
    std::size_t line = 0;
    std::string field;
    std::string message;
};

class ContentRegistry final
{
public:
    [[nodiscard]] const AssetRecord* Find(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<AssetRecord>& Assets() const noexcept;
    [[nodiscard]] const std::vector<std::size_t>& LoadOrder() const noexcept;
    [[nodiscard]] const std::filesystem::path& Root() const noexcept;

private:
    friend bool LoadContentManifest(const std::filesystem::path&, const std::filesystem::path&,
                                    ContentRegistry&, std::vector<ContentDiagnostic>&);

    std::filesystem::path root_;
    std::vector<AssetRecord> assets_;
    std::unordered_map<std::string, std::size_t> indices_;
    std::vector<std::size_t> loadOrder_;
};

// Loads a UTF-8 tab-separated manifest and publishes the registry only when every
// structural, dependency, provenance, and required-file check succeeds.
[[nodiscard]] bool LoadContentManifest(const std::filesystem::path& manifestPath,
                                       const std::filesystem::path& contentRoot,
                                       ContentRegistry& output,
                                       std::vector<ContentDiagnostic>& diagnostics);
} // namespace talvisota::content
