#include "talvisota/content/ContentRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
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
               extension == ".bmp" || extension == ".tif" || extension == ".tiff";
    case AssetKind::Audio:
        return extension == ".wav" || extension == ".ogg" || extension == ".flac";
    case AssetKind::Definition:
        return extension == ".tworld" || extension == ".tmission" || extension == ".tfaction" ||
               extension == ".tcharacter" || extension == ".tequipment" || extension == ".json" ||
               extension == ".toml" || extension == ".yaml" || extension == ".yml";
    case AssetKind::Material:
        return extension == ".tmat";
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

void AddDiagnostic(std::vector<ContentDiagnostic>& diagnostics, const std::filesystem::path& file,
                   std::size_t line, std::string field, std::string message)
{
    diagnostics.push_back({file, line, std::move(field), std::move(message)});
}
} // namespace

const AssetRecord* ContentRegistry::Find(std::string_view id) const
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
        AddDiagnostic(diagnostics, manifestPath, 0, "contentRoot", "Content root does not exist or is not a directory.");
        return false;
    }

    std::ifstream input(manifestPath, std::ios::binary);
    if (!input)
    {
        AddDiagnostic(diagnostics, manifestPath, 0, "manifest", "Manifest could not be opened.");
        return false;
    }

    ContentRegistry candidate;
    candidate.root_ = root;
    std::unordered_set<std::string> seenPaths;
    std::vector<std::size_t> recordLines;
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
                AddDiagnostic(diagnostics, manifestPath, lineNumber, "header",
                              "Expected 'talvisota-content-manifest<TAB>1'.");
            headerSeen = true;
            if (!diagnostics.empty())
                return false;
            continue;
        }

        const std::vector<std::string_view> fields = SplitTabs(line);
        if (fields.size() != 8)
        {
            AddDiagnostic(diagnostics, manifestPath, lineNumber, "record",
                          "Asset records require exactly 8 tab-separated fields.");
            continue;
        }
        if (fields[0] != "asset")
        {
            AddDiagnostic(diagnostics, manifestPath, lineNumber, "record", "Expected an 'asset' record.");
            continue;
        }

        AssetRecord asset;
        asset.id = std::string(fields[1]);
        if (!IsValidId(fields[1]))
            AddDiagnostic(diagnostics, manifestPath, lineNumber, "id",
                          "ID must start with a lowercase letter or digit and contain only lowercase ASCII letters, digits, '.', '_', '-', or ':'.");
        if (!ParseKind(fields[2], asset.kind))
            AddDiagnostic(diagnostics, manifestPath, lineNumber, "kind",
                          "Kind must be model, texture, audio, definition, or material.");

        std::u8string utf8Path;
        utf8Path.reserve(fields[3].size());
        for (const char value : fields[3])
            utf8Path.push_back(static_cast<char8_t>(static_cast<unsigned char>(value)));
        const std::filesystem::path relativePath(utf8Path);
        if (fields[3].empty() || relativePath.is_absolute() || relativePath.has_root_name())
            AddDiagnostic(diagnostics, manifestPath, lineNumber, "path", "Path must be non-empty and relative to the content root.");
        for (const auto& component : relativePath)
        {
            if (component == "..")
            {
                AddDiagnostic(diagnostics, manifestPath, lineNumber, "path", "Parent-directory traversal is not allowed.");
                break;
            }
        }
        asset.relativePath = relativePath.lexically_normal();
        if (!HasSupportedExtension(asset.kind, asset.relativePath))
            AddDiagnostic(diagnostics, manifestPath, lineNumber, "path", "File extension is not supported for the declared asset kind.");

        if (fields[4] == "required") asset.required = true;
        else if (fields[4] == "optional") asset.required = false;
        else AddDiagnostic(diagnostics, manifestPath, lineNumber, "required", "Value must be required or optional.");

        if (fields[5] != "-")
        {
            std::size_t begin = 0;
            while (begin <= fields[5].size())
            {
                const std::size_t separator = fields[5].find(',', begin);
                const std::size_t end = separator == std::string_view::npos ? fields[5].size() : separator;
                const std::string_view dependency = Trim(fields[5].substr(begin, end - begin));
                if (!IsValidId(dependency))
                    AddDiagnostic(diagnostics, manifestPath, lineNumber, "dependencies", "Dependency list contains an invalid stable ID.");
                else
                    asset.dependencies.emplace_back(dependency);
                if (separator == std::string_view::npos)
                    break;
                begin = separator + 1;
            }
            std::sort(asset.dependencies.begin(), asset.dependencies.end());
            if (std::adjacent_find(asset.dependencies.begin(), asset.dependencies.end()) != asset.dependencies.end())
                AddDiagnostic(diagnostics, manifestPath, lineNumber, "dependencies", "Dependency list contains a duplicate ID.");
        }

        asset.source = std::string(fields[6]);
        asset.license = std::string(fields[7]);
        if (asset.source.empty() || asset.license.empty() || asset.source == "-" || asset.license == "-")
            AddDiagnostic(diagnostics, manifestPath, lineNumber, "provenance",
                          "Source and license are mandatory; use 'project' for original project assets.");

        const std::string pathKey = LowerAscii(asset.relativePath.generic_string());
        if (!seenPaths.insert(pathKey).second)
            AddDiagnostic(diagnostics, manifestPath, lineNumber, "path", "Two asset IDs may not point to the same path.");
        if (candidate.indices_.contains(asset.id))
            AddDiagnostic(diagnostics, manifestPath, lineNumber, "id", "Duplicate asset ID.");
        else
        {
            candidate.indices_.emplace(asset.id, candidate.assets_.size());
            candidate.assets_.push_back(std::move(asset));
            recordLines.push_back(lineNumber);
        }
    }

    if (!input.eof() && input.fail())
        AddDiagnostic(diagnostics, manifestPath, lineNumber, "manifest", "An I/O error occurred while reading the manifest.");
    if (!headerSeen)
        AddDiagnostic(diagnostics, manifestPath, 0, "header", "Manifest is empty or has no version header.");
    if (!diagnostics.empty())
        return false;

    for (std::size_t index = 0; index < candidate.assets_.size(); ++index)
    {
        const AssetRecord& asset = candidate.assets_[index];
        const std::filesystem::path resolved = std::filesystem::weakly_canonical(root / asset.relativePath, filesystemError);
        if (filesystemError || !IsWithinRoot(root, resolved))
        {
            AddDiagnostic(diagnostics, manifestPath, recordLines[index], "path", "Resolved asset path escapes the content root: " + asset.id);
            filesystemError.clear();
            continue;
        }
        bool exists = std::filesystem::exists(resolved, filesystemError);
        if (filesystemError == std::errc::no_such_file_or_directory ||
            filesystemError == std::errc::not_a_directory)
        {
            filesystemError.clear();
            exists = false;
        }
        if (filesystemError)
        {
            AddDiagnostic(diagnostics, manifestPath, recordLines[index], "path", "Could not inspect asset file: " + asset.id);
            filesystemError.clear();
            continue;
        }
        if (exists)
        {
            const bool regularFile = std::filesystem::is_regular_file(resolved, filesystemError);
            if (filesystemError || !regularFile)
                AddDiagnostic(diagnostics, manifestPath, recordLines[index], "path",
                              "Asset path does not resolve to a regular file: " + asset.id);
            filesystemError.clear();
        }
        else if (asset.required)
            AddDiagnostic(diagnostics, manifestPath, recordLines[index], "path", "Required asset file is missing: " + asset.id);
    }

    for (std::size_t index = 0; index < candidate.assets_.size(); ++index)
    {
        const AssetRecord& asset = candidate.assets_[index];
        for (const std::string& dependency : asset.dependencies)
        {
            if (!candidate.indices_.contains(dependency))
                AddDiagnostic(diagnostics, manifestPath, recordLines[index], "dependencies",
                              "Asset '" + asset.id + "' references missing dependency '" + dependency + "'.");
        }
    }
    if (!diagnostics.empty())
        return false;

    std::vector<std::size_t> indegree(candidate.assets_.size(), 0U);
    std::vector<std::vector<std::size_t>> dependents(candidate.assets_.size());
    for (std::size_t index = 0; index < candidate.assets_.size(); ++index)
    {
        indegree[index] = candidate.assets_[index].dependencies.size();
        for (const std::string& dependency : candidate.assets_[index].dependencies)
            dependents[candidate.indices_.at(dependency)].push_back(index);
    }

    std::set<std::pair<std::string, std::size_t>> ready;
    for (std::size_t index = 0; index < candidate.assets_.size(); ++index)
    {
        if (indegree[index] == 0U)
            ready.emplace(candidate.assets_[index].id, index);
    }

    std::vector<std::size_t> order;
    order.reserve(candidate.assets_.size());
    while (!ready.empty())
    {
        const std::size_t index = ready.begin()->second;
        ready.erase(ready.begin());
        order.push_back(index);
        for (const std::size_t dependent : dependents[index])
        {
            --indegree[dependent];
            if (indegree[dependent] == 0U)
                ready.emplace(candidate.assets_[dependent].id, dependent);
        }
    }
    if (order.size() != candidate.assets_.size())
    {
        for (std::size_t index = 0; index < indegree.size(); ++index)
        {
            if (indegree[index] != 0U)
            {
                AddDiagnostic(diagnostics, manifestPath, recordLines[index], "dependencies",
                              "Dependency cycle detected for asset '" + candidate.assets_[index].id + "'.");
            }
        }
        return false;
    }

    candidate.loadOrder_ = std::move(order);
    output = std::move(candidate);
    return true;
}
} // namespace talvisota::content
