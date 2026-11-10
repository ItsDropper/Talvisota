#include "talvisota/content/AuthoredDefinition.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace talvisota::content
{
namespace
{
struct KindInfo final
{
    std::string_view extension;
    std::string_view header;
    AuthoredDefinitionKind kind;
};

constexpr KindInfo Kinds[] = {
    {".tmission", "talvisota-mission\t1", AuthoredDefinitionKind::Mission},
    {".tfaction", "talvisota-faction\t1", AuthoredDefinitionKind::Faction},
    {".tcharacter", "talvisota-character\t1", AuthoredDefinitionKind::Character},
    {".tequipment", "talvisota-equipment\t1", AuthoredDefinitionKind::Equipment}
};

[[nodiscard]] bool IsValidDefinitionId(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > 128U) return false;
    for (const unsigned char character : value)
    {
        const bool alphaNumeric = (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9');
        if (!alphaNumeric && character != '.' && character != '_' &&
            character != '-' && character != ':')
            return false;
    }
    return true;
}

[[nodiscard]] bool ParseFiniteFloat(std::string_view text, float& output) noexcept
{
    float value = 0.0F;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value))
        return false;
    output = value;
    return true;
}

[[nodiscard]] std::string_view Trim(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
        value.remove_prefix(1U);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1U);
    return value;
}

void Add(std::vector<AuthoredDefinitionDiagnostic>& diagnostics,
         const std::filesystem::path& path, std::size_t line,
         std::string field, std::string message)
{
    diagnostics.push_back({path, line, std::move(field), std::move(message)});
}

[[nodiscard]] const std::pair<std::string, std::size_t>* Get(
    const std::unordered_map<std::string, std::pair<std::string, std::size_t>>& values,
    std::string_view key)
{
    const auto found = values.find(std::string(key));
    return found == values.end() ? nullptr : &found->second;
}

bool ValidateReference(std::string_view id, AssetKind expectedKind,
                                     std::string_view expectedExtension,
                                     const ContentRegistry& registry,
                                     const AssetRecord* owner,
                                     const std::filesystem::path& path,
                                     std::size_t line, std::string_view field,
                                     std::vector<AuthoredDefinitionDiagnostic>& diagnostics)
{
    if (id.empty())
        return true;
    const AssetRecord* target = registry.Find(id);
    if (target == nullptr)
    {
        Add(diagnostics, path, line, std::string(field),
            "Reference does not exist in the content manifest: " + std::string(id));
        return false;
    }
    if (target->kind != expectedKind ||
        (!expectedExtension.empty() && target->relativePath.extension().string() != expectedExtension))
    {
        Add(diagnostics, path, line, std::string(field),
            "Reference resolves to an asset of the wrong kind or definition type: " + std::string(id));
        return false;
    }
    if (owner != nullptr &&
        std::find(owner->dependencies.begin(), owner->dependencies.end(), id) == owner->dependencies.end())
        Add(diagnostics, path, line, std::string(field),
            "Referenced asset must also be declared as a manifest dependency: " + std::string(id));
    std::error_code fileError;
    const auto resolved = std::filesystem::weakly_canonical(registry.Root() / target->relativePath, fileError);
    if (fileError || !std::filesystem::is_regular_file(resolved, fileError) || fileError)
        Add(diagnostics, path, line, std::string(field),
            "Referenced asset is missing or is not a regular file: " + std::string(id));
    return true;
}
} // namespace

bool LoadAuthoredDefinition(const std::filesystem::path& path, const ContentRegistry& registry,
                            AuthoredDefinition& output,
                            std::vector<AuthoredDefinitionDiagnostic>& diagnostics)
{
    diagnostics.clear();
    const std::string extension = path.extension().string();
    const KindInfo* kindInfo = nullptr;
    for (const KindInfo& candidate : Kinds)
    {
        if (extension == candidate.extension)
        {
            kindInfo = &candidate;
            break;
        }
    }
    if (kindInfo == nullptr)
    {
        Add(diagnostics, path, 0U, "extension", "Expected .tmission, .tfaction, .tcharacter, or .tequipment.");
        return false;
    }

    std::error_code error;
    const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
    if (error || !std::filesystem::is_regular_file(canonicalPath, error) || error)
    {
        Add(diagnostics, path, 0U, "file", "Definition does not resolve to a regular file.");
        return false;
    }
    const auto relative = canonicalPath.lexically_relative(registry.Root());
    if (relative.empty() || relative.is_absolute() ||
        std::find(relative.begin(), relative.end(), std::filesystem::path("..")) != relative.end())
    {
        Add(diagnostics, path, 0U, "file", "Definition is outside the content root.");
        return false;
    }

    std::ifstream input(canonicalPath, std::ios::binary);
    if (!input)
    {
        Add(diagnostics, path, 0U, "file", "Definition could not be opened.");
        return false;
    }
    std::unordered_map<std::string, std::pair<std::string, std::size_t>> values;
    std::vector<std::pair<MissionObjectiveDefinition, std::size_t>> missionObjectives;
    std::vector<std::pair<MissionActorDefinition, std::size_t>> missionActors;
    std::string line;
    std::size_t totalBytes = 0U;
    std::size_t lineNumber = 0U;
    bool headerSeen = false;
    while (std::getline(input, line))
    {
        ++lineNumber;
        totalBytes += line.size() + 1U;
        if (totalBytes > 4U * 1024U * 1024U)
        {
            Add(diagnostics, path, lineNumber, "fileSize", "Definition exceeds the 4 MiB input safety limit.");
            break;
        }
        if (line.size() > 65536U)
        {
            Add(diagnostics, path, lineNumber, "line", "Definition line exceeds the 64 KiB limit.");
            continue;
        }
        if (lineNumber == 1U && line.size() >= 3U &&
            static_cast<unsigned char>(line[0]) == 0xEFU &&
            static_cast<unsigned char>(line[1]) == 0xBBU &&
            static_cast<unsigned char>(line[2]) == 0xBFU)
            line.erase(0U, 3U);
        const std::string_view trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == '#')
            continue;
        if (!headerSeen)
        {
            if (trimmed != kindInfo->header)
            {
                Add(diagnostics, path, lineNumber, "header",
                    "Definition header does not match its extension or supported version 1.");
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
        const std::string_view key = Trim(trimmed.substr(0U, separator));
        const std::string_view value = Trim(trimmed.substr(separator + 1U));
        if (key.empty() || value.empty())
        {
            Add(diagnostics, path, lineNumber, "record", "Key and value must both be non-empty.");
            continue;
        }
        if (key == "actor" && kindInfo->kind == AuthoredDefinitionKind::Mission)
        {
            // actor = stable-id -> faction-id -> x,z -> weapon-id -> reserve-rounds
            std::array<std::string_view, 5> fields{};
            std::string_view remaining = value;
            bool valid = true;
            for (std::size_t field = 0U; field < fields.size(); ++field)
            {
                const std::size_t separator = remaining.find("->");
                if (field + 1U == fields.size())
                {
                    if (separator != std::string_view::npos) valid = false;
                    fields[field] = Trim(remaining);
                }
                else
                {
                    if (separator == std::string_view::npos) { valid = false; break; }
                    fields[field] = Trim(remaining.substr(0U, separator));
                    remaining.remove_prefix(separator + 2U);
                }
                if (fields[field].empty()) valid = false;
            }
            const std::size_t coordinateSeparator = fields[2].find(',');
            float x = 0.0F, z = 0.0F;
            std::uint32_t reserve = 0U;
            if (coordinateSeparator == std::string_view::npos ||
                fields[2].find(',', coordinateSeparator + 1U) != std::string_view::npos ||
                !ParseFiniteFloat(Trim(fields[2].substr(0U, coordinateSeparator)), x) ||
                !ParseFiniteFloat(Trim(fields[2].substr(coordinateSeparator + 1U)), z))
                valid = false;
            const auto reserveResult = std::from_chars(fields[4].data(),
                fields[4].data() + fields[4].size(), reserve);
            if (reserveResult.ec != std::errc{} ||
                reserveResult.ptr != fields[4].data() + fields[4].size() || reserve > 10000U)
                valid = false;
            if (!valid || !IsValidDefinitionId(fields[0]) ||
                !IsValidDefinitionId(fields[1]) || !IsValidDefinitionId(fields[3]))
            {
                Add(diagnostics, path, lineNumber, "actor",
                    "Expected actor = stable-id -> faction-id -> x,z -> weapon-id -> reserve-rounds.");
                continue;
            }
            missionActors.push_back({{std::string(fields[0]), std::string(fields[1]),
                std::string(fields[3]), {x, z}, reserve}, lineNumber});
            continue;
        }
        if (key == "objective" && kindInfo->kind == AuthoredDefinitionKind::Mission)
        {
            const std::size_t objectiveSeparator = value.find("->");
            if (objectiveSeparator == std::string_view::npos ||
                value.find("->", objectiveSeparator + 2U) != std::string_view::npos)
            {
                Add(diagnostics, path, lineNumber, "objective",
                    "Expected objective = objective-id -> interaction-target-id.");
                continue;
            }
            const std::string_view objectiveId = Trim(value.substr(0U, objectiveSeparator));
            const std::string_view targetId = Trim(value.substr(objectiveSeparator + 2U));
            if (objectiveId.empty() || targetId.empty())
            {
                Add(diagnostics, path, lineNumber, "objective",
                    "Objective ID and interaction target ID must both be non-empty.");
                continue;
            }
            missionObjectives.push_back({{std::string(objectiveId), std::string(targetId)}, lineNumber});
            continue;
        }
        if (!values.emplace(std::string(key), std::pair{std::string(value), lineNumber}).second)
            Add(diagnostics, path, lineNumber, std::string(key), "Duplicate field.");
    }
    if (!input.eof() && input.fail())
        Add(diagnostics, path, lineNumber, "file", "I/O error while reading definition.");
    if (!headerSeen)
        Add(diagnostics, path, 0U, "header", "Definition is empty or has no version header.");

    static constexpr std::string_view common[] = {"id", "name", "description"};
    static constexpr std::string_view mission[] = {"world_id", "region_id", "player_character_id", "faction_id", "next_mission_id", "environment_profile"};
    static constexpr std::string_view faction[] = {"side"};
    static constexpr std::string_view character[] = {"faction_id", "model_id", "portrait_texture_id"};
    static constexpr std::string_view equipment[] = {"category", "model_id", "icon_texture_id"};
    const auto allowedField = [&](std::string_view key)
    {
        for (const auto value : common) if (key == value) return true;
        const auto contains = [key](const auto& fields)
        {
            for (const auto value : fields) if (key == value) return true;
            return false;
        };
        switch (kindInfo->kind)
        {
        case AuthoredDefinitionKind::Mission: return key == "objective" || key == "actor" || contains(mission);
        case AuthoredDefinitionKind::Faction: return contains(faction);
        case AuthoredDefinitionKind::Character: return contains(character);
        case AuthoredDefinitionKind::Equipment: return contains(equipment);
        }
        return false;
    };
    for (const auto& [key, value] : values)
    {
        if (!allowedField(key))
            Add(diagnostics, path, value.second, key, "Unknown field for this definition type.");
    }

    AuthoredDefinition candidate;
    candidate.kind = kindInfo->kind;
    const auto required = [&](std::string_view key) -> const std::pair<std::string, std::size_t>*
    {
        const auto* value = Get(values, key);
        if (value == nullptr)
            Add(diagnostics, path, 0U, std::string(key), "Required field is missing.");
        return value;
    };
    if (const auto* value = required("id")) candidate.id = value->first;
    if (const auto* value = required("name")) candidate.name = value->first;
    if (const auto* value = Get(values, "description")) candidate.description = value->first;

    switch (candidate.kind)
    {
    case AuthoredDefinitionKind::Mission:
        if (const auto* value = required("world_id")) candidate.worldId = value->first;
        if (const auto* value = required("region_id")) candidate.regionId = value->first;
        if (const auto* value = required("player_character_id")) candidate.characterId = value->first;
        if (const auto* value = Get(values, "faction_id")) candidate.factionId = value->first;
        if (const auto* value = Get(values, "next_mission_id")) candidate.nextMissionId = value->first;
        if (const auto* value = Get(values, "environment_profile")) candidate.environmentProfile = value->first;
        if (candidate.environmentProfile != "default" && candidate.environmentProfile != "overcast" &&
            candidate.environmentProfile != "low_visibility")
            Add(diagnostics, path, Get(values, "environment_profile") ? Get(values, "environment_profile")->second : 0U,
                "environment_profile", "Expected default, overcast, or low_visibility.");
        if (missionObjectives.empty())
            Add(diagnostics, path, 0U, "objective", "Mission requires at least one ordered objective.");
        {
            std::unordered_set<std::string> objectiveIds;
            std::unordered_set<std::string> targetIds;
            for (const auto& [objective, objectiveLine] : missionObjectives)
            {
                if (!objectiveIds.insert(objective.id).second)
                    Add(diagnostics, path, objectiveLine, "objective", "Objective IDs must be unique: " + objective.id);
                if (!targetIds.insert(objective.interactionTargetId).second)
                    Add(diagnostics, path, objectiveLine, "objective",
                        "Interaction target IDs must be unique: " + objective.interactionTargetId);
                candidate.objectives.push_back(objective);
            }
        }
        {
            std::unordered_set<std::string> actorIds;
            if (missionActors.size() > 64U)
                Add(diagnostics, path, 0U, "actor", "A mission may author at most 64 battlefield actors.");
            for (const auto& [actor, actorLine] : missionActors)
            {
                if (!actorIds.insert(actor.id).second)
                    Add(diagnostics, path, actorLine, "actor", "Actor IDs must be unique: " + actor.id);
                candidate.actors.push_back(actor);
            }
        }
        break;
    case AuthoredDefinitionKind::Faction:
        if (const auto* value = required("side"))
        {
            candidate.classification = value->first;
            if (candidate.classification != "finnish" && candidate.classification != "soviet" &&
                candidate.classification != "neutral")
                Add(diagnostics, path, value->second, "side", "Expected finnish, soviet, or neutral.");
        }
        break;
    case AuthoredDefinitionKind::Character:
        if (const auto* value = required("faction_id")) candidate.factionId = value->first;
        if (const auto* value = Get(values, "model_id")) candidate.modelId = value->first;
        if (const auto* value = Get(values, "portrait_texture_id")) candidate.textureId = value->first;
        break;
    case AuthoredDefinitionKind::Equipment:
        if (const auto* value = required("category"))
        {
            candidate.classification = value->first;
            if (candidate.classification != "clothing" && candidate.classification != "tool" &&
                candidate.classification != "weapon" && candidate.classification != "medical" &&
                candidate.classification != "other")
                Add(diagnostics, path, value->second, "category",
                    "Expected clothing, tool, weapon, medical, or other.");
        }
        if (const auto* value = Get(values, "model_id")) candidate.modelId = value->first;
        if (const auto* value = Get(values, "icon_texture_id")) candidate.textureId = value->first;
        break;
    }

    const AssetRecord* owner = registry.Find(candidate.id);
    if (owner == nullptr || owner->kind != AssetKind::Definition ||
        owner->relativePath.extension().string() != kindInfo->extension)
        Add(diagnostics, path, Get(values, "id") ? Get(values, "id")->second : 0U, "id",
            "Definition ID must be registered to this definition type in the manifest.");
    else
    {
        const auto registeredPath = std::filesystem::weakly_canonical(registry.Root() / owner->relativePath, error);
        if (error || registeredPath != canonicalPath)
            Add(diagnostics, path, Get(values, "id") ? Get(values, "id")->second : 0U, "id",
                "Definition ID is registered to a different source path.");
    }

    switch (candidate.kind)
    {
    case AuthoredDefinitionKind::Mission:
        ValidateReference(candidate.worldId, AssetKind::Definition, ".tworld", registry, owner,
                          path, Get(values, "world_id") ? Get(values, "world_id")->second : 0U,
                          "world_id", diagnostics);
        ValidateReference(candidate.regionId, AssetKind::Definition, ".tregion", registry, owner,
                          path, Get(values, "region_id") ? Get(values, "region_id")->second : 0U,
                          "region_id", diagnostics);
        ValidateReference(candidate.characterId, AssetKind::Definition, ".tcharacter", registry, owner,
                          path, Get(values, "player_character_id") ? Get(values, "player_character_id")->second : 0U,
                          "player_character_id", diagnostics);
        ValidateReference(candidate.factionId, AssetKind::Definition, ".tfaction", registry, owner,
                          path, Get(values, "faction_id") ? Get(values, "faction_id")->second : 0U,
                          "faction_id", diagnostics);
        ValidateReference(candidate.nextMissionId, AssetKind::Definition, ".tmission", registry, owner,
                          path, Get(values, "next_mission_id") ? Get(values, "next_mission_id")->second : 0U,
                          "next_mission_id", diagnostics);
        for (const auto& [actor, actorLine] : missionActors)
            ValidateReference(actor.factionId, AssetKind::Definition, ".tfaction", registry, owner,
                path, actorLine, "actor", diagnostics);
        break;
    case AuthoredDefinitionKind::Character:
        ValidateReference(candidate.factionId, AssetKind::Definition, ".tfaction", registry, owner,
                          path, Get(values, "faction_id") ? Get(values, "faction_id")->second : 0U,
                          "faction_id", diagnostics);
        ValidateReference(candidate.modelId, AssetKind::Model, {}, registry, owner,
                          path, Get(values, "model_id") ? Get(values, "model_id")->second : 0U,
                          "model_id", diagnostics);
        ValidateReference(candidate.textureId, AssetKind::Texture, {}, registry, owner,
                          path, Get(values, "portrait_texture_id") ? Get(values, "portrait_texture_id")->second : 0U,
                          "portrait_texture_id", diagnostics);
        break;
    case AuthoredDefinitionKind::Equipment:
        ValidateReference(candidate.modelId, AssetKind::Model, {}, registry, owner,
                          path, Get(values, "model_id") ? Get(values, "model_id")->second : 0U,
                          "model_id", diagnostics);
        ValidateReference(candidate.textureId, AssetKind::Texture, {}, registry, owner,
                          path, Get(values, "icon_texture_id") ? Get(values, "icon_texture_id")->second : 0U,
                          "icon_texture_id", diagnostics);
        break;
    case AuthoredDefinitionKind::Faction:
        break;
    }

    if (!diagnostics.empty())
        return false;
    output = std::move(candidate);
    return true;
}
} // namespace talvisota::content
