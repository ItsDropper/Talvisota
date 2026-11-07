#include "talvisota/content/WorldDefinition.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace talvisota::content
{
namespace
{
constexpr std::string_view Header = "talvisota-world\t1";

std::string_view Trim(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}

void Add(std::vector<WorldDiagnostic>& diagnostics, const std::filesystem::path& path,
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

bool ParseVector2(std::string_view value, std::array<float, 2>& output)
{
    const std::size_t separator = value.find(',');
    if (separator == std::string_view::npos ||
        !ParseFloat(value.substr(0, separator), output[0]) ||
        !ParseFloat(value.substr(separator + 1U), output[1]))
        return false;
    return true;
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
} // namespace

bool LoadWorldDefinition(const std::filesystem::path& path, const ContentRegistry& registry,
                         WorldDefinition& output, std::vector<WorldDiagnostic>& diagnostics)
{
    diagnostics.clear();
    std::error_code error;
    const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
    if (error || !std::filesystem::is_regular_file(canonicalPath, error) || error)
    {
        Add(diagnostics, path, 0, "file", "World definition does not resolve to a regular file.");
        return false;
    }
    const auto relative = canonicalPath.lexically_relative(registry.Root());
    if (relative.empty() || relative.is_absolute())
    {
        Add(diagnostics, path, 0, "file", "World definition is outside the content root.");
        return false;
    }
    for (const auto& component : relative)
    {
        if (component == "..")
        {
            Add(diagnostics, path, 0, "file", "World definition is outside the content root.");
            return false;
        }
    }

    std::ifstream input(canonicalPath, std::ios::binary);
    if (!input)
    {
        Add(diagnostics, path, 0, "file", "World definition could not be opened.");
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
                Add(diagnostics, path, lineNumber, "header", "Expected 'talvisota-world<TAB>1'.");
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
        Add(diagnostics, path, lineNumber, "file", "I/O error while reading world definition.");
    if (!headerSeen)
        Add(diagnostics, path, 0, "header", "World definition is empty or has no version header.");

    static constexpr std::string_view allowed[] = {
        "id", "name", "unit", "up_axis", "north_axis", "handedness",
        "bounds_min", "bounds_max", "sea_level", "terrain_model_id",
        "sun_direction", "sun_color", "sun_intensity", "ambient_color", "ambient_intensity",
        "fog_color", "fog_density", "fog_start", "wind_direction", "wind_strength"
    };
    for (const auto& [key, value] : values)
    {
        bool known = false;
        for (const auto field : allowed)
            known = known || key == field;
        if (!known)
            Add(diagnostics, path, value.second, key, "Unknown world field.");
    }

    WorldDefinition candidate;
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
    if (const auto* value = required("name"))
        candidate.name = value->first;
    if (candidate.name.find_first_not_of(" \t") == std::string::npos)
        Add(diagnostics, path, get("name") ? get("name")->second : 0U, "name", "World name must not be blank.");

    const auto requireConvention = [&](std::string_view key, std::string_view expected)
    {
        if (const auto* value = required(key))
        {
            if (value->first != expected)
                Add(diagnostics, path, value->second, std::string(key),
                    "World v1 requires '" + std::string(expected) + "'.");
        }
    };
    requireConvention("unit", "meter");
    requireConvention("up_axis", "y");
    requireConvention("north_axis", "+z");
    requireConvention("handedness", "right");

    if (const auto* value = required("bounds_min"))
    {
        if (!ParseVector3(value->first, candidate.boundsMin))
            Add(diagnostics, path, value->second, "bounds_min", "Expected three finite comma-separated metre values.");
    }
    if (const auto* value = required("bounds_max"))
    {
        if (!ParseVector3(value->first, candidate.boundsMax))
            Add(diagnostics, path, value->second, "bounds_max", "Expected three finite comma-separated metre values.");
    }
    for (std::size_t axis = 0; axis < 3U; ++axis)
    {
        if (candidate.boundsMin[axis] >= candidate.boundsMax[axis])
            Add(diagnostics, path, 0, "bounds", "Each minimum bound must be strictly less than its maximum bound.");
    }
    if (const auto* value = get("terrain_model_id"))
        candidate.terrainModelId = value->first;

    if (const auto* value = get("sea_level"))
    {
        if (!ParseFloat(value->first, candidate.seaLevel))
            Add(diagnostics, path, value->second, "sea_level", "Expected a finite metre value.");
        else
        {
            candidate.hasSeaLevel = true;
            if (candidate.seaLevel < candidate.boundsMin[1] || candidate.seaLevel > candidate.boundsMax[1])
                Add(diagnostics, path, value->second, "sea_level", "Sea level must lie within the world's vertical bounds.");
        }
    }

    constexpr std::array<std::string_view, 10> environmentFields = {
        "sun_direction", "sun_color", "sun_intensity", "ambient_color", "ambient_intensity",
        "fog_color", "fog_density", "fog_start", "wind_direction", "wind_strength"
    };
    std::size_t environmentFieldCount = 0U;
    for (const auto field : environmentFields)
        environmentFieldCount += get(field) == nullptr ? 0U : 1U;
    if (environmentFieldCount != 0U && environmentFieldCount != environmentFields.size())
        Add(diagnostics, path, 0, "environment",
            "Environment settings must be supplied as a complete ten-field group.");

    if (environmentFieldCount == environmentFields.size()) {
        auto& environment = candidate.environment;
        const auto parseVector = [&](std::string_view key, std::array<float, 3>& destination) {
            const auto* value = get(key);
            if (!ParseVector3(value->first, destination))
                Add(diagnostics, path, value->second, std::string(key),
                    "Expected three finite comma-separated values.");
        };
        const auto parseScalar = [&](std::string_view key, float& destination) {
            const auto* value = get(key);
            if (!ParseFloat(value->first, destination))
                Add(diagnostics, path, value->second, std::string(key), "Expected a finite scalar value.");
        };
        parseVector("sun_direction", environment.sunDirection);
        parseVector("sun_color", environment.sunColor);
        parseScalar("sun_intensity", environment.sunIntensity);
        parseVector("ambient_color", environment.ambientColor);
        parseScalar("ambient_intensity", environment.ambientIntensity);
        parseVector("fog_color", environment.fogColor);
        parseScalar("fog_density", environment.fogDensity);
        parseScalar("fog_start", environment.fogStart);
        const auto* windDirection = get("wind_direction");
        if (!ParseVector2(windDirection->first, environment.windDirection))
            Add(diagnostics, path, windDirection->second, "wind_direction",
                "Expected two finite comma-separated values.");
        parseScalar("wind_strength", environment.windStrength);

        const auto length3 = [](const std::array<float, 3>& v) {
            return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        };
        const float sunLength = length3(environment.sunDirection);
        const float windLength = std::hypot(environment.windDirection[0], environment.windDirection[1]);
        if (std::abs(sunLength - 1.0F) > 0.02F)
            Add(diagnostics, path, get("sun_direction")->second, "sun_direction",
                "Sun direction must be normalized.");
        if (std::abs(windLength - 1.0F) > 0.02F)
            Add(diagnostics, path, get("wind_direction")->second, "wind_direction",
                "Wind direction must be normalized.");
        const auto validColor = [](const std::array<float, 3>& v) {
            return std::all_of(v.begin(), v.end(), [](float x) { return x >= 0.0F && x <= 16.0F; });
        };
        if (!validColor(environment.sunColor))
            Add(diagnostics, path, get("sun_color")->second, "sun_color", "Linear color channels must be within [0,16].");
        if (!validColor(environment.ambientColor))
            Add(diagnostics, path, get("ambient_color")->second, "ambient_color", "Linear color channels must be within [0,16].");
        if (!validColor(environment.fogColor))
            Add(diagnostics, path, get("fog_color")->second, "fog_color", "Linear color channels must be within [0,16].");
        if (environment.sunIntensity < 0.0F || environment.sunIntensity > 10.0F)
            Add(diagnostics, path, get("sun_intensity")->second, "sun_intensity", "Sun intensity must be within [0,10].");
        if (environment.ambientIntensity < 0.0F || environment.ambientIntensity > 4.0F)
            Add(diagnostics, path, get("ambient_intensity")->second, "ambient_intensity", "Ambient intensity must be within [0,4].");
        if (environment.fogDensity < 0.0F || environment.fogDensity > 0.1F)
            Add(diagnostics, path, get("fog_density")->second, "fog_density", "Fog density must be within [0,0.1].");
        if (environment.fogStart < 0.0F || environment.fogStart > 1000.0F)
            Add(diagnostics, path, get("fog_start")->second, "fog_start", "Fog start must be within [0,1000] metres.");
        if (environment.windStrength < 0.0F || environment.windStrength > 1.0F)
            Add(diagnostics, path, get("wind_strength")->second, "wind_strength", "Wind strength must be within [0,1].");
        candidate.hasEnvironment = true;
    }

    const AssetRecord* worldAsset = registry.Find(candidate.id);
    if (worldAsset == nullptr)
        Add(diagnostics, path, get("id") ? get("id")->second : 0U, "id", "World ID is not present in the content manifest.");
    else if (worldAsset->kind != AssetKind::Definition)
        Add(diagnostics, path, get("id") ? get("id")->second : 0U, "id", "World ID must identify a definition asset.");
    else
    {
        const auto registeredPath = std::filesystem::weakly_canonical(registry.Root() / worldAsset->relativePath, error);
        if (error || registeredPath != canonicalPath)
            Add(diagnostics, path, get("id") ? get("id")->second : 0U, "id", "World ID is registered to a different source path.");
        if (!candidate.terrainModelId.empty())
        {
            const AssetRecord* terrain = registry.Find(candidate.terrainModelId);
            if (terrain == nullptr || terrain->kind != AssetKind::Model)
                Add(diagnostics, path, get("terrain_model_id")->second, "terrain_model_id",
                    "Terrain model ID must resolve to a registered model asset.");
            else if (std::find(worldAsset->dependencies.begin(), worldAsset->dependencies.end(),
                               candidate.terrainModelId) == worldAsset->dependencies.end())
                Add(diagnostics, path, get("terrain_model_id")->second, "terrain_model_id",
                    "Terrain model must be declared as a world manifest dependency.");
            else
            {
                std::error_code terrainError;
                const auto terrainPath = std::filesystem::weakly_canonical(
                    registry.Root() / terrain->relativePath, terrainError);
                if (terrainError || !std::filesystem::is_regular_file(terrainPath, terrainError) || terrainError)
                    Add(diagnostics, path, get("terrain_model_id")->second, "terrain_model_id",
                        "World terrain model must exist as a regular file, even when its manifest record is optional.");
            }
        }
    }

    if (!diagnostics.empty())
        return false;
    output = std::move(candidate);
    return true;
}
} // namespace talvisota::content
