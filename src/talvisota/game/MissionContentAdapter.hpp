#pragma once

#include "talvisota/content/AuthoredDefinition.hpp"
#include "talvisota/game/MissionRuntime.hpp"

#include <string>
#include <utility>

namespace talvisota::game
{
// Converts the validated content-layer mission into the gameplay-owned definition.
// Region and world IDs are required here even though only objective data enters the
// state machine: they prove this came from a mission authored for a concrete region.
[[nodiscard]] inline bool BuildMissionDefinition(
    const content::AuthoredDefinition& authored,
    MissionDefinition& output,
    std::string& error)
{
    if (authored.kind != content::AuthoredDefinitionKind::Mission)
    {
        error = "Only authored mission definitions can be converted to gameplay missions.";
        return false;
    }
    if (authored.id.empty() || authored.worldId.empty() || authored.regionId.empty() ||
        authored.objectives.empty())
    {
        error = "Authored mission requires an ID, world, region, and at least one objective.";
        return false;
    }

    MissionDefinition candidate;
    candidate.id = authored.id;
    candidate.objectives.reserve(authored.objectives.size());
    for (const content::MissionObjectiveDefinition& objective : authored.objectives)
        candidate.objectives.push_back({objective.id, objective.interactionTargetId});

    if (!ValidateMissionDefinition(candidate, error))
        return false;

    output = std::move(candidate);
    error.clear();
    return true;
}

// Publishes an Active mission only after conversion, initialization, and start all
// succeed. A failed conversion never partially replaces the caller's current state.
[[nodiscard]] inline bool InitializeMissionFromAuthoredDefinition(
    const content::AuthoredDefinition& authored,
    MissionState& output,
    std::string& error)
{
    MissionDefinition definition;
    if (!BuildMissionDefinition(authored, definition, error))
        return false;

    MissionState candidate;
    if (!InitializeMission(definition, candidate, error) ||
        !StartMission(candidate, error))
        return false;

    output = std::move(candidate);
    error.clear();
    return true;
}
} // namespace talvisota::game
