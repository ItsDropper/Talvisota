#pragma once

#include "talvisota/entities/EntityRegistry.hpp"
#include "talvisota/game/MissionRuntime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

struct SimulationState final
{
    entities::EntityRegistry entities;
    // Set by authored-mission initialization; empty for generic test/simulation fixtures.
    std::string playerEntityId;
    game::MissionState mission;
    std::uint64_t tickIndex = 0U;
    double elapsedSeconds = 0.0;
    // A bounded replay guard: command IDs are consumed once for this simulation.
    std::set<std::string, std::less<>> processedCommandIds;
};

[[nodiscard]] bool ValidateGameplayRules(const GameplayRules& rules,
    std::string& error);

// Creates the entity registry and starts the supplied mission atomically. Output is
// unchanged if any entity or mission definition is invalid.
[[nodiscard]] bool InitializeSimulation(const game::MissionDefinition& mission,
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

// Advances exactly one deterministic simulation tick. Commands are processed in
// caller-provided order. The state and result vector are unchanged on validation failure.
[[nodiscard]] bool AdvanceSimulation(SimulationState& state, const GameplayRules& rules,
    double deltaSeconds, std::span<const InteractionCommand> commands,
    std::vector<CommandResult>& outputResults, std::string& error);
} // namespace talvisota::gameplay
