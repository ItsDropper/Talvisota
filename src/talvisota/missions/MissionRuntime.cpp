#include "talvisota/missions/MissionRuntime.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace talvisota::missions
{
namespace
{
bool ValidateState(const MissionState& state, std::string& error)
{
    if (state.missionId.empty() || state.objectives.empty())
    {
        error = "Mission state has no mission ID or objectives.";
        return false;
    }

    std::size_t activeCount = 0U;
    std::size_t completedCount = 0U;
    for (const ObjectiveState& objective : state.objectives)
    {
        if (objective.id.empty() || objective.interactionTargetId.empty())
        {
            error = "Mission state contains an objective without stable identifiers.";
            return false;
        }
        if (objective.status == ObjectiveStatus::Active) ++activeCount;
        if (objective.status == ObjectiveStatus::Completed) ++completedCount;
    }
    if (completedCount != state.completedObjectives)
    {
        error = "Mission completed-objective count does not match objective states.";
        return false;
    }

    switch (state.status)
    {
    case MissionStatus::Ready:
        if (activeCount != 0U || completedCount != 0U)
        {
            error = "Ready mission state must not contain active or completed objectives.";
            return false;
        }
        break;
    case MissionStatus::Active:
        if (activeCount != 1U || completedCount >= state.objectives.size())
        {
            error = "Active mission state must have exactly one active objective and remaining work.";
            return false;
        }
        break;
    case MissionStatus::Completed:
        if (activeCount != 0U || completedCount != state.objectives.size())
        {
            error = "Completed mission state must have every objective completed.";
            return false;
        }
        break;
    case MissionStatus::Failed:
        if (activeCount > 1U || completedCount >= state.objectives.size())
        {
            error = "Failed mission state has an invalid objective progression.";
            return false;
        }
        break;
    }
    error.clear();
    return true;
}

void ResetObjectives(MissionState& state, const bool activateFirst)
{
    state.completedObjectives = 0U;
    for (std::size_t index = 0U; index < state.objectives.size(); ++index)
        state.objectives[index].status =
            activateFirst && index == 0U ? ObjectiveStatus::Active : ObjectiveStatus::Pending;
}
} // namespace

bool ValidateMissionDefinition(const MissionDefinition& definition, std::string& error)
{
    if (definition.id.empty())
    {
        error = "Mission definition requires a stable ID.";
        return false;
    }
    if (definition.objectives.empty())
    {
        error = "Mission definition requires at least one objective.";
        return false;
    }

    std::unordered_set<std::string> objectiveIds;
    std::unordered_set<std::string> targetIds;
    objectiveIds.reserve(definition.objectives.size());
    targetIds.reserve(definition.objectives.size());
    for (const ObjectiveDefinition& objective : definition.objectives)
    {
        if (objective.id.empty() || objective.interactionTargetId.empty())
        {
            error = "Every mission objective requires a stable ID and interaction target ID.";
            return false;
        }
        if (!objectiveIds.insert(objective.id).second)
        {
            error = "Mission objective IDs must be unique: " + objective.id;
            return false;
        }
        if (!targetIds.insert(objective.interactionTargetId).second)
        {
            error = "Mission interaction target IDs must be unique: " + objective.interactionTargetId;
            return false;
        }
    }
    error.clear();
    return true;
}

bool InitializeMission(const MissionDefinition& definition, MissionState& output, std::string& error)
{
    if (!ValidateMissionDefinition(definition, error)) return false;

    MissionState candidate;
    candidate.missionId = definition.id;
    candidate.objectives.reserve(definition.objectives.size());
    for (const ObjectiveDefinition& objective : definition.objectives)
        candidate.objectives.push_back({objective.id, objective.interactionTargetId, ObjectiveStatus::Pending});
    output = std::move(candidate);
    error.clear();
    return true;
}

bool StartMission(MissionState& state, std::string& error)
{
    if (!ValidateState(state, error)) return false;
    if (state.status != MissionStatus::Ready)
    {
        error = "Only a Ready mission can be started.";
        return false;
    }
    state.status = MissionStatus::Active;
    state.objectives.front().status = ObjectiveStatus::Active;
    error.clear();
    return true;
}

bool ApplyMissionInteraction(MissionState& state, const std::string_view interactionTargetId,
    InteractionResult& result, std::string& error)
{
    if (!ValidateState(state, error)) return false;
    result = InteractionResult::Ignored;
    if (state.status != MissionStatus::Active || interactionTargetId.empty())
    {
        error.clear();
        return true;
    }

    const auto active = std::find_if(state.objectives.begin(), state.objectives.end(),
        [](const ObjectiveState& objective) { return objective.status == ObjectiveStatus::Active; });
    if (active == state.objectives.end())
    {
        error = "Active mission has no active objective.";
        return false;
    }
    if (active->interactionTargetId != interactionTargetId)
    {
        error.clear();
        return true;
    }

    MissionState candidate = state;
    const std::size_t index = static_cast<std::size_t>(active - state.objectives.begin());
    candidate.objectives[index].status = ObjectiveStatus::Completed;
    ++candidate.completedObjectives;
    if (candidate.completedObjectives == candidate.objectives.size())
    {
        candidate.status = MissionStatus::Completed;
        result = InteractionResult::MissionCompleted;
    }
    else
    {
        candidate.objectives[index + 1U].status = ObjectiveStatus::Active;
        result = InteractionResult::ObjectiveCompleted;
    }
    if (!ValidateState(candidate, error)) return false;
    state = std::move(candidate);
    error.clear();
    return true;
}

bool FailMission(MissionState& state, std::string& error)
{
    if (!ValidateState(state, error)) return false;
    if (state.status != MissionStatus::Active)
    {
        error = "Only an Active mission can fail.";
        return false;
    }
    state.status = MissionStatus::Failed;
    error.clear();
    return true;
}

bool RestartMission(MissionState& state, std::string& error)
{
    if (!ValidateState(state, error)) return false;
    if (state.status != MissionStatus::Completed && state.status != MissionStatus::Failed)
    {
        error = "Only a Completed or Failed mission can be restarted.";
        return false;
    }
    ResetObjectives(state, true);
    state.status = MissionStatus::Active;
    error.clear();
    return true;
}
} // namespace talvisota::missions
