#include "talvisota/gameplay/GameplaySimulation.hpp"

#include "talvisota/game/MissionContentAdapter.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <algorithm>
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

bool InitializeSimulationFromAuthoredMission(
    const content::AuthoredDefinition& authoredMission,
    const world::WorldRegion& region,
    const std::array<float, 3>& playerSpawn,
    const GameplayRules& rules, SimulationState& output, std::string& error)
{
    game::MissionDefinition missionDefinition;
    if (!game::BuildMissionDefinition(authoredMission, missionDefinition, error))
        return false;

    if (region.id != authoredMission.regionId || region.worldId != authoredMission.worldId)
    {
        error = "Loaded world region does not match the authored mission's world and region IDs.";
        return false;
    }
    if (!entities::IsValidStableId(authoredMission.characterId) ||
        !entities::IsValidStableId(authoredMission.factionId))
    {
        error = "Authored mission requires valid player-character and faction IDs.";
        return false;
    }

    for (std::size_t axis = 0; axis < playerSpawn.size(); ++axis)
    {
        if (!std::isfinite(region.boundsMin[axis]) ||
            !std::isfinite(region.boundsMax[axis]) ||
            region.boundsMin[axis] > region.boundsMax[axis] ||
            !std::isfinite(playerSpawn[axis]) ||
            playerSpawn[axis] < region.boundsMin[axis] ||
            playerSpawn[axis] > region.boundsMax[axis])
        {
            error = "Player spawn must be finite and contained by the loaded region bounds.";
            return false;
        }
    }

    const std::string playerId = "player." + authoredMission.characterId;
    if (!entities::IsValidStableId(playerId))
    {
        error = "Derived player entity ID exceeds the stable-ID constraints.";
        return false;
    }

    std::vector<entities::EntityDefinition> definitions;
    definitions.reserve(missionDefinition.objectives.size() + 1U);
    definitions.push_back({
        playerId, authoredMission.factionId, entities::EntityKind::Player, playerSpawn, {}
    });

    for (const game::ObjectiveDefinition& objective : missionDefinition.objectives)
    {
        const world::WorldPlacement* targetPlacement = nullptr;
        for (const world::WorldPlacement& placement : region.placements)
        {
            if (placement.id != objective.interactionTargetId)
                continue;
            if (targetPlacement != nullptr)
            {
                error = "Mission interaction target resolves to multiple region placements: " +
                    objective.interactionTargetId;
                return false;
            }
            targetPlacement = &placement;
        }

        if (targetPlacement == nullptr)
        {
            error = "Mission interaction target is not placed in the loaded region: " +
                objective.interactionTargetId;
            return false;
        }
        if (targetPlacement->category != world::PlacementCategory::Prop &&
            targetPlacement->category != world::PlacementCategory::Structure)
        {
            error = "Mission interaction target must resolve to a prop or structure placement: " +
                objective.interactionTargetId;
            return false;
        }

        // World props have no owning faction. Their placement ID is also their
        // entity ID, so mission events resolve to the exact authored world object.
        definitions.push_back({
            targetPlacement->id, {}, entities::EntityKind::WorldObject,
            targetPlacement->position, targetPlacement->id
        });
    }

    SimulationState candidate;
    if (!InitializeSimulation(missionDefinition, definitions, rules, candidate, error))
        return false;

    candidate.playerEntityId = playerId;
    output = std::move(candidate);
    error.clear();
    return true;
}

bool FindActiveObjectiveTarget(const SimulationState& state,
    const std::string_view actorEntityId, ActiveObjectiveTarget& output,
    std::string& error)
{
    if (!entities::IsValidStableId(actorEntityId))
    {
        error = "Active-objective query contains an invalid actor ID.";
        return false;
    }

    const entities::EntityState* const actor = state.entities.Find(actorEntityId);
    if (actor == nullptr || !actor->active || actor->kind == entities::EntityKind::WorldObject)
    {
        error = "Active-objective query requires an active actor entity.";
        return false;
    }
    for (const float coordinate : actor->position)
    {
        if (!std::isfinite(coordinate))
        {
            error = "Active-objective query actor position must be finite.";
            return false;
        }
    }

    // Reuse mission-state validation without changing the live simulation.
    game::MissionState validatedMission = state.mission;
    game::InteractionResult ignoredResult = game::InteractionResult::Ignored;
    if (!game::ApplyMissionInteraction(validatedMission, {}, ignoredResult, error))
        return false;

    ActiveObjectiveTarget candidate;
    if (state.mission.status != game::MissionStatus::Active)
    {
        output = std::move(candidate);
        error.clear();
        return true;
    }

    const game::ObjectiveState* activeObjective = nullptr;
    for (const game::ObjectiveState& objective : state.mission.objectives)
    {
        if (objective.status != game::ObjectiveStatus::Active)
            continue;
        if (activeObjective != nullptr)
        {
            error = "Active mission contains more than one active objective.";
            return false;
        }
        activeObjective = &objective;
    }
    if (activeObjective == nullptr ||
        !entities::IsValidStableId(activeObjective->interactionTargetId))
    {
        error = "Active mission does not resolve to one valid active objective.";
        return false;
    }

    const entities::EntityState* target = nullptr;
    for (const auto& [entityId, entity] : state.entities.All())
    {
        if (entity.interactionTargetId != activeObjective->interactionTargetId ||
            entity.kind != entities::EntityKind::WorldObject)
            continue;
        if (target != nullptr)
        {
            error = "Active objective resolves to multiple world-object entities: " +
                activeObjective->interactionTargetId;
            return false;
        }
        if (entity.id != entityId || !entities::IsValidStableId(entityId))
        {
            error = "Active objective target has an inconsistent entity identity.";
            return false;
        }
        target = &entity;
    }

    if (target == nullptr || !target->active)
    {
        error = "Active objective target is missing or inactive: " +
            activeObjective->interactionTargetId;
        return false;
    }
    for (const float coordinate : target->position)
    {
        if (!std::isfinite(coordinate))
        {
            error = "Active objective target position must be finite.";
            return false;
        }
    }

    const double x = static_cast<double>(actor->position[0]) - target->position[0];
    const double y = static_cast<double>(actor->position[1]) - target->position[1];
    const double z = static_cast<double>(actor->position[2]) - target->position[2];
    const double distance = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(distance) ||
        distance > static_cast<double>(std::numeric_limits<float>::max()))
    {
        error = "Active objective actor-to-target distance is not representable.";
        return false;
    }

    candidate.entityId = target->id;
    candidate.position = target->position;
    candidate.distanceMeters = static_cast<float>(distance);
    output = std::move(candidate);
    error.clear();
    return true;
}

bool FindNearestActiveObjectiveTarget(const SimulationState& state,
    const std::string_view actorEntityId, const float maximumDistanceMeters,
    InteractionTargetSelection& output, std::string& error)
{
    if (!entities::IsValidStableId(actorEntityId) ||
        !std::isfinite(maximumDistanceMeters) || maximumDistanceMeters <= 0.0F ||
        maximumDistanceMeters > 10.0F)
    {
        error = "Interaction-target query contains an invalid actor ID or search radius.";
        return false;
    }

    const entities::EntityState* const actor = state.entities.Find(actorEntityId);
    if (actor == nullptr || !actor->active || actor->kind == entities::EntityKind::WorldObject)
    {
        error = "Interaction-target query requires an active actor entity.";
        return false;
    }
    for (const float coordinate : actor->position)
    {
        if (!std::isfinite(coordinate))
        {
            error = "Interaction-target query actor position must be finite.";
            return false;
        }
    }

    // Validate the complete mission state before deriving a presentation target.
    // This keeps malformed progression from silently appearing as "no target".
    game::MissionState validatedMission = state.mission;
    game::InteractionResult ignoredResult = game::InteractionResult::Ignored;
    if (!game::ApplyMissionInteraction(validatedMission, {}, ignoredResult, error))
        return false;

    InteractionTargetSelection candidate;
    if (state.mission.status != game::MissionStatus::Active)
    {
        output = std::move(candidate);
        error.clear();
        return true;
    }

    const game::ObjectiveState* activeObjective = nullptr;
    for (const game::ObjectiveState& objective : state.mission.objectives)
    {
        if (objective.status != game::ObjectiveStatus::Active)
            continue;
        if (activeObjective != nullptr)
        {
            error = "Active mission contains more than one active objective.";
            return false;
        }
        activeObjective = &objective;
    }

    if (activeObjective == nullptr)
    {
        error = "Active mission does not contain an active objective.";
        return false;
    }
    if (!entities::IsValidStableId(activeObjective->interactionTargetId))
    {
        error = "Active mission objective contains an invalid interaction-target ID.";
        return false;
    }

    const double maximumDistanceSquared =
        static_cast<double>(maximumDistanceMeters) * maximumDistanceMeters;
    double bestDistanceSquared = maximumDistanceSquared;
    for (const auto& [entityId, target] : state.entities.All())
    {
        if (target.kind != entities::EntityKind::WorldObject ||
            target.interactionTargetId != activeObjective->interactionTargetId)
            continue;
        if (!target.active)
            continue;
        if (target.id != entityId || !entities::IsValidStableId(entityId))
        {
            error = "Active objective target has an inconsistent entity identity.";
            return false;
        }
        for (const float coordinate : target.position)
        {
            if (!std::isfinite(coordinate))
            {
                error = "Active objective target position must be finite.";
                return false;
            }
        }

        const double x = static_cast<double>(actor->position[0]) - target.position[0];
        const double y = static_cast<double>(actor->position[1]) - target.position[1];
        const double z = static_cast<double>(actor->position[2]) - target.position[2];
        const double distanceSquared = x * x + y * y + z * z;
        // EntityRegistry iteration is stable-ID ordered, so strict comparison gives
        // deterministic tie-breaking without depending on insertion order.
        if (distanceSquared <= bestDistanceSquared &&
            (candidate.entityId.empty() || distanceSquared < bestDistanceSquared))
        {
            candidate.entityId = entityId;
            bestDistanceSquared = distanceSquared;
        }
    }

    if (!candidate.entityId.empty())
        candidate.distanceMeters = static_cast<float>(std::sqrt(bestDistanceSquared));

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
