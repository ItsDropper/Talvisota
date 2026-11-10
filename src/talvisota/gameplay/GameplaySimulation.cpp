#include "talvisota/gameplay/GameplaySimulation.hpp"

#include "talvisota/missions/MissionContentAdapter.hpp"
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

bool InitializeSimulation(const missions::MissionDefinition& mission,
    const std::span<const entities::EntityDefinition> entityDefinitions,
    const GameplayRules& rules, SimulationState& output, std::string& error)
{
    if (!ValidateGameplayRules(rules, error)) return false;

    SimulationState candidate;
    if (!missions::InitializeMission(mission, candidate.mission, error)) return false;
    for (const entities::EntityDefinition& definition : entityDefinitions)
    {
        if (!candidate.entities.Spawn(definition, error)) return false;
    }
    if (!missions::StartMission(candidate.mission, error)) return false;

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
    missions::MissionDefinition missionDefinition;
    if (!missions::BuildMissionDefinition(authoredMission, missionDefinition, error))
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
    definitions.reserve(missionDefinition.objectives.size() + authoredMission.actors.size() + 1U);
    definitions.push_back({
        playerId, authoredMission.factionId, entities::EntityKind::Player, playerSpawn, {}
    });

    for (const missions::ObjectiveDefinition& objective : missionDefinition.objectives)
    {
        const auto actorTarget = std::find_if(authoredMission.actors.begin(), authoredMission.actors.end(),
            [&](const content::MissionActorDefinition& actor) {
                return actor.id == objective.interactionTargetId;
            });
        if (actorTarget != authoredMission.actors.end())
            continue;

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

    for (const content::MissionActorDefinition& actor : authoredMission.actors)
    {
        if (!entities::IsValidStableId(actor.id) ||
            !entities::IsValidStableId(actor.factionId) ||
            !entities::IsValidStableId(actor.weaponId))
        {
            error = "Mission battlefield actors require unique IDs, a valid opposing faction, and a known weapon ID.";
            return false;
        }
        if (actor.positionXZ[0] < region.boundsMin[0] ||
            actor.positionXZ[0] > region.boundsMax[0] ||
            actor.positionXZ[1] < region.boundsMin[2] ||
            actor.positionXZ[1] > region.boundsMax[2])
        {
            error = "Mission battlefield actor is outside the authored world region: " + actor.id;
            return false;
        }
        float terrainHeight = 0.0F;
        std::array<float, 3> terrainNormal{};
        if (!world::SampleTerrainHeight(region, actor.positionXZ[0], actor.positionXZ[1],
                terrainHeight, terrainNormal, error))
            return false;
        definitions.push_back({actor.id, actor.factionId, entities::EntityKind::Infantry,
            {actor.positionXZ[0], terrainHeight, actor.positionXZ[1]}, actor.id});
    }

    SimulationState candidate;
    if (!InitializeSimulation(missionDefinition, definitions, rules, candidate, error))
        return false;

    candidate.playerEntityId = playerId;
    if (!combat::RegisterCombatant(candidate.combat, candidate.entities, playerId,
            "weapon.mosin_m27", 100.0F, 45U, error))
        return false;
    for (const content::MissionActorDefinition& actor : authoredMission.actors)
    {
        if (!combat::RegisterCombatant(candidate.combat, candidate.entities, actor.id,
                actor.weaponId, 100.0F, actor.reserveRounds, error))
            return false;
    }
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
    missions::MissionState validatedMission = state.mission;
    missions::InteractionResult ignoredResult = missions::InteractionResult::Ignored;
    if (!missions::ApplyMissionInteraction(validatedMission, {}, ignoredResult, error))
        return false;

    ActiveObjectiveTarget candidate;
    if (state.mission.status != missions::MissionStatus::Active)
    {
        output = std::move(candidate);
        error.clear();
        return true;
    }

    const missions::ObjectiveState* activeObjective = nullptr;
    for (const missions::ObjectiveState& objective : state.mission.objectives)
    {
        if (objective.status != missions::ObjectiveStatus::Active)
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
        if (entity.interactionTargetId != activeObjective->interactionTargetId)
            continue;
        if (target != nullptr)
        {
            error = "Active objective resolves to multiple gameplay entities: " +
                activeObjective->interactionTargetId;
            return false;
        }
        if (!entity.active || entity.id != entityId || !entities::IsValidStableId(entityId))
        {
            error = "Active objective target has an invalid or inactive gameplay entity.";
            return false;
        }
        target = &entity;
    }
    if (target == nullptr)
    {
        error = "Active objective target is not registered in gameplay simulation: " +
            activeObjective->interactionTargetId;
        return false;
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
    missions::MissionState validatedMission = state.mission;
    missions::InteractionResult ignoredResult = missions::InteractionResult::Ignored;
    if (!missions::ApplyMissionInteraction(validatedMission, {}, ignoredResult, error))
        return false;

    InteractionTargetSelection candidate;
    if (state.mission.status != missions::MissionStatus::Active)
    {
        output = std::move(candidate);
        error.clear();
        return true;
    }

    const missions::ObjectiveState* activeObjective = nullptr;
    for (const missions::ObjectiveState& objective : state.mission.objectives)
    {
        if (objective.status != missions::ObjectiveStatus::Active)
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


bool BuildBattlefieldAiCommands(SimulationState& state,
    std::vector<combat::CombatCommand>& output, std::string& error)
{
    if (state.combat.combatants.size() > 64U ||
        state.actorNextFireAtSeconds.size() > 64U ||
        !std::isfinite(state.combat.elapsedSeconds) || state.combat.elapsedSeconds < 0.0)
    {
        error = "Battlefield AI state exceeds its actor or clock limits.";
        return false;
    }
    const entities::EntityState* player = state.entities.Find(state.playerEntityId);
    if (player == nullptr || !player->active ||
        !state.combat.combatants.contains(state.playerEntityId))
    {
        error = "Battlefield AI requires the active player combatant.";
        return false;
    }

    std::string activeCombatObjectiveTarget;
    if (state.mission.status == missions::MissionStatus::Active)
    {
        const auto objective = std::find_if(state.mission.objectives.begin(),
            state.mission.objectives.end(), [](const missions::ObjectiveState& value) {
                return value.status == missions::ObjectiveStatus::Active;
            });
        if (objective != state.mission.objectives.end())
            activeCombatObjectiveTarget = objective->interactionTargetId;
    }

    auto nextFireAt = state.actorNextFireAtSeconds;
    std::vector<combat::CombatCommand> commands;
    commands.reserve(state.combat.combatants.size());
    std::size_t commandIndex = 0U;
    for (const auto& [actorId, combatant] : state.combat.combatants)
    {
        if (actorId == state.playerEntityId || combatant.health <= 0.0F)
            continue;
        const entities::EntityState* actor = state.entities.Find(actorId);
        const combat::WeaponDefinition* weapon = combat::FindWeaponDefinition(combatant.weaponId);
        if (actor == nullptr || !actor->active || weapon == nullptr)
        {
            error = "Battlefield AI actor has no active entity or valid weapon definition.";
            return false;
        }

        const entities::EntityState* selectedTarget = nullptr;
        double nearestDistance = static_cast<double>(weapon->effectiveRangeMeters) * 0.5;
        for (const auto& [targetId, targetCombatant] : state.combat.combatants)
        {
            if (targetId == actorId || targetCombatant.health <= 0.0F)
                continue;
            const entities::EntityState* target = state.entities.Find(targetId);
            if (target == nullptr || !target->active || target->factionId == actor->factionId)
                continue;
            // Do not let autonomous squad behavior defeat a later authored target
            // before the player reaches that objective in campaign order.
            if (!target->interactionTargetId.empty() &&
                target->kind != entities::EntityKind::WorldObject &&
                target->id != activeCombatObjectiveTarget)
                continue;
            const double dx = static_cast<double>(actor->position[0]) - target->position[0];
            const double dy = static_cast<double>(actor->position[1]) - target->position[1];
            const double dz = static_cast<double>(actor->position[2]) - target->position[2];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (!std::isfinite(distance))
            {
                error = "Battlefield AI encountered a non-finite actor separation.";
                return false;
            }
            if (distance <= nearestDistance)
            {
                if (selectedTarget == nullptr || distance < nearestDistance ||
                    targetId < selectedTarget->id)
                {
                    selectedTarget = target;
                    nearestDistance = distance;
                }
            }
        }
        if (selectedTarget == nullptr)
            continue;

        const auto cooldown = nextFireAt.find(actorId);
        if (cooldown != nextFireAt.end() && cooldown->second > state.combat.elapsedSeconds)
            continue;
        const std::string commandId = "ai." + std::to_string(state.tickIndex) + "." +
            std::to_string(commandIndex++);
        commands.push_back({commandId, actorId, selectedTarget->id, combat::CombatAction::Fire});
        nextFireAt[actorId] = state.combat.elapsedSeconds + 1.6;
    }
    state.actorNextFireAtSeconds = std::move(nextFireAt);
    output = std::move(commands);
    error.clear();
    return true;
}

bool FindNearestHostileCombatant(const SimulationState& state,
    const std::string_view actorEntityId, const std::array<float, 3>& rayOrigin,
    const std::array<float, 3>& rayDirection, const float maximumRangeMeters,
    const float targetRadiusMeters, std::string& outputEntityId, std::string& error)
{
    if (!entities::IsValidStableId(actorEntityId) ||
        !std::isfinite(maximumRangeMeters) || maximumRangeMeters <= 0.0F ||
        !std::isfinite(targetRadiusMeters) || targetRadiusMeters <= 0.0F ||
        targetRadiusMeters > 5.0F)
    {
        error = "Combat target query contains an invalid actor ID, range, or target radius.";
        return false;
    }
    double directionLengthSquared = 0.0;
    for (std::size_t axis = 0U; axis < 3U; ++axis)
    {
        if (!std::isfinite(rayOrigin[axis]) || !std::isfinite(rayDirection[axis]))
        {
            error = "Combat target ray must contain only finite coordinates.";
            return false;
        }
        directionLengthSquared += static_cast<double>(rayDirection[axis]) * rayDirection[axis];
    }
    if (!std::isfinite(directionLengthSquared) ||
        std::abs(directionLengthSquared - 1.0) > 0.02)
    {
        error = "Combat target ray direction must be normalized.";
        return false;
    }

    const entities::EntityState* actor = state.entities.Find(actorEntityId);
    const auto attacker = state.combat.combatants.find(actorEntityId);
    if (actor == nullptr || !actor->active || attacker == state.combat.combatants.end())
    {
        error = "Combat target query requires an active registered combatant.";
        return false;
    }

    std::string selected;
    double nearestAlongRay = static_cast<double>(maximumRangeMeters);
    const double radiusSquared = static_cast<double>(targetRadiusMeters) * targetRadiusMeters;
    for (const auto& [entityId, combatant] : state.combat.combatants)
    {
        if (entityId == actorEntityId || combatant.health <= 0.0F)
            continue;
        const entities::EntityState* target = state.entities.Find(entityId);
        if (target == nullptr || !target->active || target->factionId == actor->factionId)
            continue;

        const std::array<double, 3> offset{
            static_cast<double>(target->position[0]) - rayOrigin[0],
            static_cast<double>(target->position[1]) + 0.9 - rayOrigin[1],
            static_cast<double>(target->position[2]) - rayOrigin[2]
        };
        double along = 0.0;
        double distanceSquared = 0.0;
        for (std::size_t axis = 0U; axis < 3U; ++axis)
        {
            along += offset[axis] * rayDirection[axis];
            distanceSquared += offset[axis] * offset[axis];
        }
        if (along <= 0.0 || along > maximumRangeMeters || along > nearestAlongRay)
            continue;
        const double perpendicularSquared = std::max(0.0, distanceSquared - along * along);
        if (perpendicularSquared > radiusSquared)
            continue;
        if (selected.empty() || along < nearestAlongRay ||
            (along == nearestAlongRay && entityId < selected))
        {
            selected = entityId;
            nearestAlongRay = along;
        }
    }
    outputEntityId = std::move(selected);
    error.clear();
    return true;
}

bool ApplyCombatMissionEvents(SimulationState& missionState,
    const combat::CombatSimulationState& combatState,
    const std::span<const combat::CombatCommand> commands,
    const std::span<const combat::CombatResult> combatResults,
    std::vector<CommandResult>& outputResults, std::string& error)
{
    if (commands.size() != combatResults.size() || commands.size() > 4096U ||
        missionState.processedCommandIds.size() > MaximumProcessedCommands)
    {
        error = "Combat mission event batch has mismatched results or exceeds its configured limit.";
        return false;
    }

    SimulationState candidate = missionState;
    missions::InteractionResult validationResult = missions::InteractionResult::Ignored;
    if (!missions::ApplyMissionInteraction(candidate.mission, {}, validationResult, error))
        return false;

    std::vector<CommandResult> results;
    results.reserve(commands.size());
    for (std::size_t index = 0U; index < commands.size(); ++index)
    {
        const combat::CombatCommand& command = commands[index];
        const combat::CombatResult& combatResult = combatResults[index];
        if (!entities::IsValidStableId(command.commandId) ||
            !entities::IsValidStableId(command.actorEntityId) ||
            combatResult.commandId != command.commandId)
        {
            error = "Combat mission event does not match its originating stable command ID.";
            return false;
        }

        if (candidate.processedCommandIds.contains(command.commandId) ||
            combatResult.outcome == combat::CombatOutcome::Duplicate)
        {
            results.push_back({command.commandId, CommandOutcome::Duplicate});
            continue;
        }
        if (candidate.processedCommandIds.size() >= MaximumProcessedCommands)
        {
            error = "Mission combat-event replay guard reached its configured capacity.";
            return false;
        }
        candidate.processedCommandIds.insert(command.commandId);

        if (combatResult.outcome != combat::CombatOutcome::TargetDefeated)
        {
            results.push_back({command.commandId, CommandOutcome::Ignored});
            continue;
        }
        if (command.action != combat::CombatAction::Fire ||
            !entities::IsValidStableId(command.targetEntityId))
        {
            error = "Only a valid fire command can produce a defeated combat mission target.";
            return false;
        }

        const auto attacker = combatState.combatants.find(command.actorEntityId);
        const auto defeated = combatState.combatants.find(command.targetEntityId);
        if (attacker == combatState.combatants.end() ||
            defeated == combatState.combatants.end() || defeated->second.health != 0.0F ||
            combatResult.targetHealthRemaining != 0.0F || combatResult.damageApplied <= 0.0F)
        {
            error = "Combat defeat result does not match registered combatants and a zero-health target.";
            return false;
        }

        const entities::EntityState* const target =
            candidate.entities.Find(command.targetEntityId);
        if (target == nullptr || !target->active ||
            target->kind == entities::EntityKind::WorldObject ||
            target->interactionTargetId.empty())
        {
            results.push_back({command.commandId, CommandOutcome::Ignored});
            continue;
        }
        if (!entities::IsValidStableId(target->interactionTargetId))
        {
            error = "Defeated mission target contains an invalid interaction-target ID.";
            return false;
        }

        missions::InteractionResult missionResult = missions::InteractionResult::Ignored;
        if (!missions::ApplyMissionInteraction(candidate.mission,
                target->interactionTargetId, missionResult, error))
            return false;

        switch (missionResult)
        {
        case missions::InteractionResult::Ignored:
            results.push_back({command.commandId, CommandOutcome::Ignored});
            break;
        case missions::InteractionResult::ObjectiveCompleted:
            results.push_back({command.commandId, CommandOutcome::ObjectiveCompleted});
            break;
        case missions::InteractionResult::MissionCompleted:
            results.push_back({command.commandId, CommandOutcome::MissionCompleted});
            break;
        }
    }

    missionState = std::move(candidate);
    outputResults = std::move(results);
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
    missions::InteractionResult missionResult = missions::InteractionResult::Ignored;
    if (!missions::ApplyMissionInteraction(candidate.mission, {}, missionResult, error))
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

        if (!missions::ApplyMissionInteraction(candidate.mission, target->interactionTargetId,
                missionResult, error))
            return false;

        switch (missionResult)
        {
        case missions::InteractionResult::Ignored:
            results.push_back({command.commandId, CommandOutcome::Ignored});
            break;
        case missions::InteractionResult::ObjectiveCompleted:
            results.push_back({command.commandId, CommandOutcome::ObjectiveCompleted});
            break;
        case missions::InteractionResult::MissionCompleted:
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
