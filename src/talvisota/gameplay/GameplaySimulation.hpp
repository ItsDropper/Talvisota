#pragma once

#include "talvisota/entities/EntityRegistry.hpp"
#include "talvisota/gameplay/CombatSimulation.hpp"
#include "talvisota/game/MissionRuntime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace talvisota::content { struct AuthoredDefinition; }
namespace talvisota::world { struct WorldRegion; }

namespace talvisota::gameplay
{
struct GameplayRules final
{
    float interactionRadiusMeters = 3.0F;
    float maximumStepSeconds = 0.25F;
};

struct InteractionCommand final
{
    std::string commandId;
    std::string actorEntityId;
    std::string targetEntityId;
};

enum class CommandOutcome
{
    Ignored,
    OutOfRange,
    ObjectiveCompleted,
    MissionCompleted,
    Duplicate
};

struct CommandResult final
{
    std::string commandId;
    CommandOutcome outcome = CommandOutcome::Ignored;
};

struct InteractionTargetSelection final
{
    std::string entityId;
    float distanceMeters = 0.0F;
};

// All-range waypoint snapshot for the currently active objective. This is
// deliberately separate from InteractionTargetSelection, which is range-limited.
struct ActiveObjectiveTarget final
{
    std::string entityId;
    std::array<float, 3> position{};
    float distanceMeters = 0.0F;
};

struct SimulationState final
{
    entities::EntityRegistry entities;
    // Mission-owned combat state advances alongside the same deterministic simulation.
    combat::CombatSimulationState combat;
    // Deterministic NPC fire cadence, keyed by stable actor ID.
    std::map<std::string, double, std::less<>> actorNextFireAtSeconds;
    // Defeated authored targets are retained until ordered mission progression reaches them.
    std::set<std::string, std::less<>> defeatedObjectiveTargets;
    // Set by authored-mission initialization; empty for generic test/simulation fixtures.
    std::string playerEntityId;
    missions::MissionState mission;
    std::uint64_t tickIndex = 0U;
    double elapsedSeconds = 0.0;
    // A bounded replay guard: command IDs are consumed once for this simulation.
    std::set<std::string, std::less<>> processedCommandIds;
};

[[nodiscard]] bool ValidateGameplayRules(const GameplayRules& rules,
    std::string& error);

// Creates the entity registry and starts the supplied mission atomically. Output is
// unchanged if any entity or mission definition is invalid.
[[nodiscard]] bool InitializeSimulation(const missions::MissionDefinition& mission,
    std::span<const entities::EntityDefinition> entityDefinitions,
    const GameplayRules& rules, SimulationState& output, std::string& error);


// Builds a playable entity set from authored mission targets and their actual region
 // placements, then publishes the mission and entities atomically.
[[nodiscard]] bool InitializeSimulationFromAuthoredMission(
    const content::AuthoredDefinition& authoredMission,
    const world::WorldRegion& region,
    const std::array<float, 3>& playerSpawn,
    const GameplayRules& rules, SimulationState& output, std::string& error);

// Selects the nearest active entity for the mission's currently active objective.
// Equal-distance candidates resolve by stable entity ID. A valid query with no
// eligible target succeeds and returns an empty entityId. Output is unchanged on error.
[[nodiscard]] bool FindNearestActiveObjectiveTarget(const SimulationState& state,
    std::string_view actorEntityId, float maximumDistanceMeters,
    InteractionTargetSelection& output, std::string& error);

// Resolves the active objective's world-object entity regardless of distance.
// Terminal/ready missions return an empty snapshot. Missing, inactive, ambiguous,
// or malformed active targets are errors; output is unchanged on error.
[[nodiscard]] bool FindActiveObjectiveTarget(const SimulationState& state,
    std::string_view actorEntityId, ActiveObjectiveTarget& output, std::string& error);

// Applies defeated combat-target events to matching active mission objectives.
// Commands and results must be the corresponding outputs from one AdvanceCombat call.
// The mission state and output remain unchanged on malformed or inconsistent input.
// Builds bounded, deterministic NPC fire commands for living non-player combatants.
[[nodiscard]] bool BuildBattlefieldAiCommands(SimulationState& state,
    std::vector<combat::CombatCommand>& output, std::string& error);

// Resolves a first-person aim ray to the nearest living hostile combatant. The
// supplied range is game-balance data from the equipped weapon definition.
[[nodiscard]] bool FindNearestHostileCombatant(const SimulationState& state,
    std::string_view actorEntityId, const std::array<float, 3>& rayOrigin,
    const std::array<float, 3>& rayDirection, float maximumRangeMeters,
    float targetRadiusMeters, std::string& outputEntityId, std::string& error);

[[nodiscard]] bool ApplyCombatMissionEvents(SimulationState& missionState,
    const combat::CombatSimulationState& combatState,
    std::span<const combat::CombatCommand> commands,
    std::span<const combat::CombatResult> combatResults,
    std::vector<CommandResult>& outputResults, std::string& error);

// Advances exactly one deterministic simulation tick. Commands are processed in
// caller-provided order. The state and result vector are unchanged on validation failure.
[[nodiscard]] bool AdvanceSimulation(SimulationState& state, const GameplayRules& rules,
    double deltaSeconds, std::span<const InteractionCommand> commands,
    std::vector<CommandResult>& outputResults, std::string& error);
} // namespace talvisota::gameplay
