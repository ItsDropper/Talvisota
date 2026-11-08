#include "talvisota/gameplay/GameplaySimulation.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace talvisota::gameplay
{
namespace
{
constexpr std::size_t MaximumProcessedCommands = 65536U;

bool IsWithinInteractionRange(const entities::EntityState& actor,
    const entities::EntityState& target, const float radius) noexcept
{
    const double x = static_cast<double>(actor.position[0]) - target.position[0];
    const double y = static_cast<double>(actor.position[1]) - target.position[1];
    const double z = static_cast<double>(actor.position[2]) - target.position[2];
    const double radiusSquared = static_cast<double>(radius) * radius;
    return x * x + y * y + z * z <= radiusSquared;
}
} // namespace

bool ValidateGameplayRules(const GameplayRules& rules, std::string& error)
{
    if (!std::isfinite(rules.interactionRadiusMeters) ||
        rules.interactionRadiusMeters <= 0.0F ||
        rules.interactionRadiusMeters > 10.0F ||
        !std::isfinite(rules.maximumStepSeconds) ||
        rules.maximumStepSeconds <= 0.0F ||
        rules.maximumStepSeconds > 0.25F)
    {
        error = "Gameplay rules contain an invalid interaction radius or simulation timestep.";
        return false;
    }
    error.clear();
    return true;
}

bool InitializeSimulation(const game::MissionDefinition& mission,
    const std::span<const entities::EntityDefinition> entityDefinitions,
    const GameplayRules& rules, SimulationState& output, std::string& error)
{
    if (!ValidateGameplayRules(rules, error)) return false;

    SimulationState candidate;
    if (!game::InitializeMission(mission, candidate.mission, error)) return false;
    for (const entities::EntityDefinition& definition : entityDefinitions)
    {
        if (!candidate.entities.Spawn(definition, error)) return false;
    }
    if (!game::StartMission(candidate.mission, error)) return false;

    output = std::move(candidate);
    error.clear();
    return true;
}

bool AdvanceSimulation(SimulationState& state, const GameplayRules& rules,
    const double deltaSeconds, const std::span<const InteractionCommand> commands,
    std::vector<CommandResult>& outputResults, std::string& error)
{
    if (!ValidateGameplayRules(rules, error)) return false;
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0 ||
        deltaSeconds > static_cast<double>(rules.maximumStepSeconds))
    {
        error = "Simulation timestep must be finite, positive, and within the configured fixed-step limit.";
        return false;
    }
    if (state.tickIndex == std::numeric_limits<std::uint64_t>::max())
    {
        error = "Simulation tick counter is exhausted.";
        return false;
    }
    if (!std::isfinite(state.elapsedSeconds) || state.elapsedSeconds < 0.0 ||
        !std::isfinite(state.elapsedSeconds + deltaSeconds))
    {
        error = "Simulation clock is invalid or would overflow.";
        return false;
    }

    SimulationState candidate = state;
    std::vector<CommandResult> results;
    results.reserve(commands.size());

    // Validate the mission state even on a tick with no interaction commands.
    game::InteractionResult missionResult = game::InteractionResult::Ignored;
    if (!game::ApplyMissionInteraction(candidate.mission, {}, missionResult, error))
        return false;

    for (const InteractionCommand& command : commands)
    {
        if (!entities::IsValidStableId(command.commandId) ||
            !entities::IsValidStableId(command.actorEntityId) ||
            !entities::IsValidStableId(command.targetEntityId))
        {
            error = "Interaction command contains an invalid stable command or entity ID.";
            return false;
        }

        if (candidate.processedCommandIds.contains(command.commandId))
        {
            results.push_back({command.commandId, CommandOutcome::Duplicate});
            continue;
        }
        if (candidate.processedCommandIds.size() >= MaximumProcessedCommands)
        {
            error = "Simulation command replay guard reached its configured capacity.";
            return false;
        }
        candidate.processedCommandIds.insert(command.commandId);

        const entities::EntityState* const actor =
            candidate.entities.Find(command.actorEntityId);
        const entities::EntityState* const target =
            candidate.entities.Find(command.targetEntityId);
        if (actor == nullptr || target == nullptr || !actor->active || !target->active ||
            actor->id == target->id || target->interactionTargetId.empty())
        {
            results.push_back({command.commandId, CommandOutcome::Ignored});
            continue;
        }
        if (!IsWithinInteractionRange(*actor, *target, rules.interactionRadiusMeters))
        {
            results.push_back({command.commandId, CommandOutcome::OutOfRange});
            continue;
        }

        if (!game::ApplyMissionInteraction(candidate.mission, target->interactionTargetId,
                missionResult, error))
            return false;

        switch (missionResult)
        {
        case game::InteractionResult::Ignored:
            results.push_back({command.commandId, CommandOutcome::Ignored});
            break;
        case game::InteractionResult::ObjectiveCompleted:
            results.push_back({command.commandId, CommandOutcome::ObjectiveCompleted});
            break;
        case game::InteractionResult::MissionCompleted:
            results.push_back({command.commandId, CommandOutcome::MissionCompleted});
            break;
        }
    }

    ++candidate.tickIndex;
    candidate.elapsedSeconds += deltaSeconds;
    state = std::move(candidate);
    outputResults = std::move(results);
    error.clear();
    return true;
}
} // namespace talvisota::gameplay
