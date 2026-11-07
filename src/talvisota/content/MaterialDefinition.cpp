#include "talvisota/content/MaterialDefinition.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace talvisota::content
{
namespace
{
constexpr std::string_view Header = "talvisota-material\t1";

std::string_view Trim(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}

void Add(std::vector<MaterialDiagnostic>& diagnostics, const std::filesystem::path& path,
         std::size_t line, std::string field, std::string message)
{
    diagnostics.push_back({path, line, std::move(field), std::move(message)});
}

bool ParseFloat(std::string_view value, float& output)
{
    value = Trim(value);
    if (value.empty())
        return false;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output,
                                        std::chars_format::general);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() && std::isfinite(output);
}

bool ParseVector3(std::string_view value, std::array<float, 3>& output)
{
    for (std::size_t index = 0; index < output.size(); ++index)
    {
        const std::size_t separator = value.find(',');
        const std::string_view component = separator == std::string_view::npos ? value : value.substr(0, separator);
        if (!ParseFloat(component, output[index]))
            return false;
        if (index + 1U == output.size())
            return separator == std::string_view::npos;
        if (separator == std::string_view::npos)
            return false;
        value.remove_prefix(separator + 1U);
    }
    return false;
}

bool ParseBool(std::string_view value, bool& output)
{
    value = Trim(value);
    if (value == "true") output = true;
    else if (value == "false") output = false;
    else return false;
    return true;
}

bool IsUnit(float value) noexcept
{
    return value >= 0.0F && value <= 1.0F;
}

bool ValidateTexture(const std::string& id, const ContentRegistry& registry,
                     const std::filesystem::path& path, std::size_t line,
                     std::string_view field, std::vector<MaterialDiagnostic>& diagnostics)
{
    if (id.empty())
        return true;
    const AssetRecord* asset = registry.Find(id);
    if (asset == nullptr)
    {
        Add(diagnostics, path, line, std::string(field), "Texture reference does not exist in the content manifest: " + id);
        return false;
    }
    if (asset->kind != AssetKind::Texture)
    {
        Add(diagnostics, path, line, std::string(field), "Referenced asset is not a texture: " + id);
        return false;
    }
    return true;
}
} // namespace

bool LoadMaterialDefinition(const std::filesystem::path& path, const ContentRegistry& registry,
                            MaterialDefinition& output, std::vector<MaterialDiagnostic>& diagnostics)
{
    diagnostics.clear();
    std::error_code error;
    const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
    if (error || !std::filesystem::is_regular_file(canonicalPath, error) || error)
    {
        Add(diagnostics, path, 0, "file", "Material definition does not resolve to a regular file.");
        return false;
    }
    const auto relative = canonicalPath.lexically_relative(registry.Root());
    if (relative.empty() || relative.is_absolute())
    {
        Add(diagnostics, path, 0, "file", "Material definition is outside the content root.");
        return false;
    }
    for (const auto& component : relative)
    {
        if (component == "..")
        {
            Add(diagnostics, path, 0, "file", "Material definition is outside the content root.");
            return false;
        }
    }

    std::ifstream input(canonicalPath, std::ios::binary);
    if (!input)
    {
        Add(diagnostics, path, 0, "file", "Material definition could not be opened.");
        return false;
    }

    std::unordered_map<std::string, std::pair<std::string, std::size_t>> values;
    std::string line;
    std::size_t lineNumber = 0;
    bool headerSeen = false;
    while (std::getline(input, line))
    {
        ++lineNumber;
        if (lineNumber == 1U && line.size() >= 3U &&
            static_cast<unsigned char>(line[0]) == 0xEFU &&
            static_cast<unsigned char>(line[1]) == 0xBBU &&
            static_cast<unsigned char>(line[2]) == 0xBFU)
            line.erase(0, 3U);

        const std::string_view trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == '#')
            continue;
        if (!headerSeen)
        {
            if (trimmed != Header)
            {
                Add(diagnostics, path, lineNumber, "header", "Expected 'talvisota-material<TAB>1'.");
                return false;
            }
            headerSeen = true;
            continue;
        }

        const std::size_t separator = trimmed.find('=');
        if (separator == std::string_view::npos)
        {
            Add(diagnostics, path, lineNumber, "record", "Expected key = value.");
            continue;
        }
        const std::string_view key = Trim(trimmed.substr(0, separator));
        const std::string_view value = Trim(trimmed.substr(separator + 1U));
        if (key.empty() || value.empty())
        {
            Add(diagnostics, path, lineNumber, "record", "Key and value must both be non-empty.");
            continue;
        }
        if (!values.emplace(std::string(key), std::pair{std::string(value), lineNumber}).second)
            Add(diagnostics, path, lineNumber, std::string(key), "Duplicate field.");
    }
    if (!input.eof() && input.fail())
        Add(diagnostics, path, lineNumber, "file", "I/O error while reading material definition.");
    if (!headerSeen)
        Add(diagnostics, path, 0, "header", "Material definition is empty or has no version header.");

    static constexpr std::string_view allowed[] = {
        "id", "base_color", "roughness", "metallic", "emissive", "normal_scale",
        "occlusion_strength", "alpha_mode", "alpha_cutoff", "double_sided",
        "base_color_texture", "normal_texture", "orm_texture", "emissive_texture"
    };
    for (const auto& [key, value] : values)
    {
        (void)value;
        bool known = false;
        for (const auto field : allowed)
            known = known || key == field;
        if (!known)
            Add(diagnostics, path, value.second, key, "Unknown material field.");
    }

    MaterialDefinition candidate;
    const auto get = [&values](std::string_view key) -> const std::pair<std::string, std::size_t>*
    {
        const auto found = values.find(std::string(key));
        return found == values.end() ? nullptr : &found->second;
    };
    const auto required = [&](std::string_view key) -> const std::pair<std::string, std::size_t>*
    {
        const auto* value = get(key);
        if (value == nullptr)
            Add(diagnostics, path, 0, std::string(key), "Required field is missing.");
        return value;
    };

    if (const auto* value = required("id"))
        candidate.id = value->first;
    if (const auto* value = required("base_color"))
    {
        if (!ParseVector3(value->first, candidate.baseColor))
            Add(diagnostics, path, value->second, "base_color", "Expected three finite comma-separated linear RGB values.");
        else if (!IsUnit(candidate.baseColor[0]) || !IsUnit(candidate.baseColor[1]) || !IsUnit(candidate.baseColor[2]))
            Add(diagnostics, path, value->second, "base_color", "Each linear RGB component must be in [0, 1].");
    }
    if (const auto* value = required("roughness"))
    {
        if (!ParseFloat(value->first, candidate.roughness) || !IsUnit(candidate.roughness))
            Add(diagnostics, path, value->second, "roughness", "Expected a finite value in [0, 1].");
    }
    if (const auto* value = required("metallic"))
    {
        if (!ParseFloat(value->first, candidate.metallic) || !IsUnit(candidate.metallic))
            Add(diagnostics, path, value->second, "metallic", "Expected a finite value in [0, 1].");
    }

    if (const auto* value = get("emissive"))
    {
        if (!ParseVector3(value->first, candidate.emissive) ||
            candidate.emissive[0] < 0.0F || candidate.emissive[1] < 0.0F || candidate.emissive[2] < 0.0F)
            Add(diagnostics, path, value->second, "emissive", "Expected three finite, non-negative linear RGB values.");
    }
    if (const auto* value = get("normal_scale"))
    {
        if (!ParseFloat(value->first, candidate.normalScale) || candidate.normalScale < 0.0F)
            Add(diagnostics, path, value->second, "normal_scale", "Expected a finite non-negative value.");
    }
    if (const auto* value = get("occlusion_strength"))
    {
        if (!ParseFloat(value->first, candidate.occlusionStrength) || !IsUnit(candidate.occlusionStrength))
            Add(diagnostics, path, value->second, "occlusion_strength", "Expected a finite value in [0, 1].");
    }
    if (const auto* value = get("alpha_cutoff"))
    {
        if (!ParseFloat(value->first, candidate.alphaCutoff) || !IsUnit(candidate.alphaCutoff))
            Add(diagnostics, path, value->second, "alpha_cutoff", "Expected a finite value in [0, 1].");
    }
    if (const auto* value = get("alpha_mode"))
    {
        if (value->first == "opaque") candidate.alphaMode = MaterialAlphaMode::Opaque;
        else if (value->first == "mask") candidate.alphaMode = MaterialAlphaMode::Mask;
        else if (value->first == "blend") candidate.alphaMode = MaterialAlphaMode::Blend;
        else Add(diagnostics, path, value->second, "alpha_mode", "Expected opaque, mask, or blend.");
    }
    if (const auto* value = get("double_sided"))
    {
        if (!ParseBool(value->first, candidate.doubleSided))
            Add(diagnostics, path, value->second, "double_sided", "Expected true or false.");
    }
    const auto optionalTexture = [&](std::string_view key, std::string& destination)
    {
        if (const auto* value = get(key))
            destination = value->first;
    };
    optionalTexture("base_color_texture", candidate.baseColorTexture);
    optionalTexture("normal_texture", candidate.normalTexture);
    optionalTexture("orm_texture", candidate.ormTexture);
    optionalTexture("emissive_texture", candidate.emissiveTexture);

    if (const AssetRecord* materialAsset = registry.Find(candidate.id))
    {
        if (materialAsset->kind != AssetKind::Material)
            Add(diagnostics, path, get("id") ? get("id")->second : 0U, "id", "Definition ID resolves to an asset that is not a material.");
        else
        {
            const auto registeredPath = std::filesystem::weakly_canonical(registry.Root() / materialAsset->relativePath, error);
            if (error || registeredPath != canonicalPath)
                Add(diagnostics, path, get("id") ? get("id")->second : 0U, "id", "Material ID is registered to a different source path.");
        }
    }
    else
        Add(diagnostics, path, get("id") ? get("id")->second : 0U, "id", "Material ID is not present in the content manifest.");

    ValidateTexture(candidate.baseColorTexture, registry, path, get("base_color_texture") ? get("base_color_texture")->second : 0U, "base_color_texture", diagnostics);
    ValidateTexture(candidate.normalTexture, registry, path, get("normal_texture") ? get("normal_texture")->second : 0U, "normal_texture", diagnostics);
    ValidateTexture(candidate.ormTexture, registry, path, get("orm_texture") ? get("orm_texture")->second : 0U, "orm_texture", diagnostics);
    ValidateTexture(candidate.emissiveTexture, registry, path, get("emissive_texture") ? get("emissive_texture")->second : 0U, "emissive_texture", diagnostics);

    if (!diagnostics.empty())
        return false;
    output = std::move(candidate);
    return true;
}
} // namespace talvisota::content
