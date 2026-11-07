#include "talvisota/content/ContentRegistry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace talvisota::content
{
namespace
{
constexpr std::string_view Header = "talvisota-content-manifest\t1";

[[nodiscard]] std::string_view Trim(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return value;
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
            fields.push_back(Trim(remaining));
            return fields;
        }
        fields.push_back(Trim(remaining.substr(0, separator)));
        remaining.remove_prefix(separator + 1);
    }
}

[[nodiscard]] bool IsValidId(std::string_view id) noexcept
{
    if (id.empty() || !((id.front() >= 'a' && id.front() <= 'z') ||
                        (id.front() >= '0' && id.front() <= '9')))
        return false;
    return std::all_of(id.begin(), id.end(), [](char value)
    {
        return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
               value == '.' || value == '_' || value == '-' || value == ':';
    });
}

[[nodiscard]] bool ParseKind(std::string_view value, AssetKind& output) noexcept
{
    if (value == "model") output = AssetKind::Model;
    else if (value == "texture") output = AssetKind::Texture;
    else if (value == "audio") output = AssetKind::Audio;
    else if (value == "definition") output = AssetKind::Definition;
    else if (value == "material") output = AssetKind::Material;
    else return false;
    return true;
}

[[nodiscard]] std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
    {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] bool HasSupportedExtension(AssetKind kind, const std::filesystem::path& path)
{
    const std::string extension = LowerAscii(path.extension().string());
    switch (kind)
    {
    case AssetKind::Model:
        return extension == ".gltf" || extension == ".glb" || extension == ".obj";
    case AssetKind::Texture:
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
               extension == ".tga" || extension == ".dds" || extension == ".ktx2";
    case AssetKind::Audio:
        return extension == ".wav" || extension == ".ogg" || extension == ".flac";
    case AssetKind::Definition:
    case AssetKind::Material:
        return extension == ".json" || extension == ".toml" || extension == ".yaml" ||
               extension == ".yml";
    }
    return false;
}

[[nodiscard]] bool IsWithinRoot(const std::filesystem::path& root,
                                const std::filesystem::path& candidate) noexcept
{
    const std::filesystem::path relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute())
        return false;
    for (const auto& component : relative)
    {
        if (component == "..")
            return false;
    }
    return true;
}

void AddDiagnostic(std::vector<ContentDiagnostic>& diagnostics, std::size_t line,
                   std::string field, std::string message)
{
    diagnostics.push_back({line, std::move(field), std::move(message)});
}
} // namespace

const AssetRecord* ContentRegistry::Find(std::string_view id) const noexcept
{
    const auto found = indices_.find(std::string(id));
    return found == indices_.end() ? nullptr : &assets_[found->second];
}

const std::vector<AssetRecord>& ContentRegistry::Assets() const noexcept { return assets_; }
const std::vector<std::size_t>& ContentRegistry::LoadOrder() const noexcept { return loadOrder_; }
const std::filesystem::path& ContentRegistry::Root() const noexcept { return root_; }

bool LoadContentManifest(const std::filesystem::path& manifestPath,
                         const std::filesystem::path& contentRoot,
                         ContentRegistry& output,
                         std::vector<ContentDiagnostic>& diagnostics)
{
    diagnostics.clear();
    std::error_code filesystemError;
    const std::filesystem::path root = std::filesystem::weakly_canonical(contentRoot, filesystemError);
    if (filesystemError || !std::filesystem::is_directory(root, filesystemError) || filesystemError)
    {
        AddDiagnostic(diagnostics, 0, "contentRoot", "Content root does not exist or is not a directory.");
        return false;
    }

    std::ifstream input(manifestPath, std::ios::binary);
    if (!input)
    {
        AddDiagnostic(diagnostics, 0, "manifest", "Manifest could not be opened.");
        return false;
    }

    ContentRegistry candidate;
    candidate.root_ = root;
    std::unordered_set<std::string> seenPaths;
    std::string line;
    std::size_t lineNumber = 0;
    bool headerSeen = false;

    while (std::getline(input, line))
    {
        ++lineNumber;
        if (lineNumber == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEFU &&
            static_cast<unsigned char>(line[1]) == 0xBBU &&
            static_cast<unsigned char>(line[2]) == 0xBFU)
            line.erase(0, 3);

        const std::string_view trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == '#')
            continue;

        if (!headerSeen)
        {
            if (trimmed != Header)
                AddDiagnostic(diagnostics, lineNumber, "header",
                              "Expected 'talvisota-content-manifest<TAB>1'.");
            headerSeen = true;
            if (!diagnostics.empty())
                return false;
            continue;
        }

        const std::vector<std::string_view> fields = SplitTabs(line);
        if (fields.size() != 8)
        {
            AddDiagnostic(diagnostics, lineNumber, "record",
                          "Asset records require exactly 8 tab-separated fields.");
            continue;
        }
        if (fields[0] != "asset")
        {
            AddDiagnostic(diagnostics, lineNumber, "record", "Expected an 'asset' record.");
            continue;
        }

        AssetRecord asset;
        asset.id = std::string(fields[1]);
        if (!IsValidId(fields[1]))
            AddDiagnostic(diagnostics, lineNumber, "id",
                          "ID must start with a lowercase letter or digit and contain only lowercase ASCII letters, digits, '.', '_', '-', or ':'.");
        if (!ParseKind(fields[2], asset.kind))
            AddDiagnostic(diagnostics, lineNumber, "kind",
                          "Kind must be model, texture, audio, definition, or material.");

        const std::filesystem::path relativePath = std::filesystem::u8path(fields[3]);
        if (fields[3].empty() || relativePath.is_absolute() || relativePath.has_root_name())
            AddDiagnostic(diagnostics, lineNumber, "path", "Path must be non-empty and relative to the content root.");
        for (const auto& component : relativePath)
        {
            if (component == "..")
            {
                AddDiagnostic(diagnostics, lineNumber, "path", "Parent-directory traversal is not allowed.");
                break;
            }
        }
        asset.relativePath = relativePath.lexically_normal();
        if (!HasSupportedExtension(asset.kind, asset.relativePath))
            AddDiagnostic(diagnostics, lineNumber, "path", "File extension is not supported for the declared asset kind.");

        if (fields[4] == "required") asset.required = true;
        else if (fields[4] == "optional") asset.required = false;
        else AddDiagnostic(diagnostics, lineNumber, "required", "Value must be required or optional.");

        if (fields[5] != "-")
        {
            std::size_t begin = 0;
            while (begin <= fields[5].size())
            {
                const std::size_t separator = fields[5].find(',', begin);
                const std::size_t end = separator == std::string_view::npos ? fields[5].size() : separator;
                const std::string_view dependency = Trim(fields[5].substr(begin, end - begin));
                if (!IsValidId(dependency))
                    AddDiagnostic(diagnostics, lineNumber, "dependencies", "Dependency list contains an invalid stable ID.");
                else
                    asset.dependencies.emplace_back(dependency);
                if (separator == std::string_view::npos)
                    break;
                begin = separator + 1;
            }
            std::sort(asset.dependencies.begin(), asset.dependencies.end());
            if (std::adjacent_find(asset.dependencies.begin(), asset.dependencies.end()) != asset.dependencies.end())
                AddDiagnostic(diagnostics, lineNumber, "dependencies", "Dependency list contains a duplicate ID.");
        }

        asset.source = std::string(fields[6]);
        asset.license = std::string(fields[7]);
        if (asset.source.empty() || asset.license.empty() || asset.source == "-" || asset.license == "-")
            AddDiagnostic(diagnostics, lineNumber, "provenance",
                          "Source and license are mandatory; use 'project' for original project assets.");

        const std::string pathKey = LowerAscii(asset.relativePath.generic_string());
        if (!seenPaths.insert(pathKey).second)
            AddDiagnostic(diagnostics, lineNumber, "path", "Two asset IDs may not point to the same path.");
        if (candidate.indices_.contains(asset.id))
            AddDiagnostic(diagnostics, lineNumber, "id", "Duplicate asset ID.");
        else
        {
            candidate.indices_.emplace(asset.id, candidate.assets_.size());
            candidate.assets_.push_back(std::move(asset));
        }
    }

    if (!input.eof() && input.fail())
        AddDiagnostic(diagnostics, lineNumber, "manifest", "An I/O error occurred while reading the manifest.");
    if (!headerSeen)
        AddDiagnostic(diagnostics, 0, "header", "Manifest is empty or has no version header.");
    if (!diagnostics.empty())
        return false;

    for (std::size_t index = 0; index < candidate.assets_.size(); ++index)
    {
        const AssetRecord& asset = candidate.assets_[index];
        const std::filesystem::path resolved = std::filesystem::weakly_canonical(root / asset.relativePath, filesystemError);
        if (filesystemError || !IsWithinRoot(root, resolved))
        {
            AddDiagnostic(diagnostics, 0, "path", "Resolved asset path escapes the content root: " + asset.id);
            filesystemError.clear();
            continue;
        }
        const bool exists = std::filesystem::is_regular_file(resolved, filesystemError);
        if (filesystemError)
        {
            AddDiagnostic(diagnostics, 0, "path", "Could not inspect asset file: " + asset.id);
            filesystemError.clear();
        }
        else if (asset.required && !exists)
            AddDiagnostic(diagnostics, 0, "path", "Required asset file is missing: " + asset.id);
    }

    for (std::size_t index = 0; index < candidate.assets_.size(); ++index)
    {
        const AssetRecord& asset = candidate.assets_[index];
        for (const std::string& dependency : asset.dependencies)
        {
            if (!candidate.indices_.contains(dependency))
                AddDiagnostic(diagnostics, 0, "dependencies",
                              "Asset '" + asset.id + "' references missing dependency '" + dependency + "'.");
        }
    }
    if (!diagnostics.empty())
        return false;

    std::vector<unsigned char> visitState(candidate.assets_.size(), 0U);
    std::vector<std::size_t> order;
    order.reserve(candidate.assets_.size());
    std::function<bool(std::size_t)> visit = [&](std::size_t index)
    {
        if (visitState[index] == 2U)
            return true;
        if (visitState[index] == 1U)
            return false;
        visitState[index] = 1U;
        for (const std::string& dependency : candidate.assets_[index].dependencies)
        {
            const std::size_t dependencyIndex = candidate.indices_.at(dependency);
            if (!visit(dependencyIndex))
                return false;
        }
        visitState[index] = 2U;
        order.push_back(index);
        return true;
    };

    std::vector<std::size_t> stableIndices(candidate.assets_.size());
    for (std::size_t index = 0; index < stableIndices.size(); ++index)
        stableIndices[index] = index;
    std::sort(stableIndices.begin(), stableIndices.end(), [&](std::size_t left, std::size_t right)
    {
        return candidate.assets_[left].id < candidate.assets_[right].id;
    });
    for (const std::size_t index : stableIndices)
    {
        if (!visit(index))
        {
            AddDiagnostic(diagnostics, 0, "dependencies",
                          "Dependency cycle detected while resolving asset '" + candidate.assets_[index].id + "'.");
            return false;
        }
    }

    candidate.loadOrder_ = std::move(order);
    output = std::move(candidate);
    return true;
}
} // namespace talvisota::content
