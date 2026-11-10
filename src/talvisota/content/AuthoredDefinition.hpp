#pragma once

#include "talvisota/content/ContentRegistry.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace talvisota::content
{
enum class AuthoredDefinitionKind
{
    Mission,
    Faction,
    Character,
    Equipment
};

struct MissionObjectiveDefinition final
{
    std::string id;
    // Must resolve to a placement ID in the mission's authored world region.
    std::string interactionTargetId;
};

struct AuthoredDefinition final
{
    AuthoredDefinitionKind kind = AuthoredDefinitionKind::Mission;
    std::string id;
    std::string name;
    std::string classification;
    std::string worldId;
    std::string regionId;
    std::vector<MissionObjectiveDefinition> objectives;
    std::string factionId;
    // Optional authored campaign successor; empty marks the end of the sequence.
    std::string nextMissionId;
    std::string characterId;
    std::string modelId;
    std::string textureId;
    std::string description;
};

struct AuthoredDefinitionDiagnostic final
{
    std::filesystem::path file;
    std::size_t line = 0;
    std::string field;
    std::string message;
};

// Loads strict v1 .tmission/.tfaction/.tcharacter/.tequipment definitions.
// Every stable-ID reference must resolve to the expected manifest asset kind
// and be declared as a dependency of the owning definition.
[[nodiscard]] bool LoadAuthoredDefinition(const std::filesystem::path& path,
                                          const ContentRegistry& registry,
                                          AuthoredDefinition& output,
                                          std::vector<AuthoredDefinitionDiagnostic>& diagnostics);
} // namespace talvisota::content
