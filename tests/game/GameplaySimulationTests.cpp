#include "talvisota/gameplay/GameplaySimulation.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::array<talvisota::entities::EntityDefinition, 3> MakeEntities()
{
    using talvisota::entities::EntityDefinition;
    using talvisota::entities::EntityKind;
    return {{
        {"entity.player", "faction.finland", EntityKind::Player, {0.0F, 0.0F, 0.0F}, {}},
        {"entity.radio", "faction.neutral", EntityKind::WorldObject, {1.0F, 0.0F, 0.0F},
            "target.radio"},
        {"entity.telephone", "faction.neutral", EntityKind::WorldObject, {2.0F, 0.0F, 0.0F},
            "target.telephone"}
    }};
}

talvisota::game::MissionDefinition MakeMission()
{
    return {
        "mission.first-patrol",
        {
            {"objective.inspect-radio", "target.radio"},
            {"objective.report-contact", "target.telephone"}
        }
    };
}
} // namespace

int main()
{
    using namespace talvisota;
    std::string error;

    // Registry validation and stable lookup.
    entities::EntityRegistry registry;
    const auto definitions = MakeEntities();
    Check(registry.Spawn(definitions[0], error), "valid player entity spawns");
    Check(registry.Spawn(definitions[1], error), "valid world entity spawns");
    Check(registry.Size() == 2U && registry.Find("entity.radio") != nullptr,
        "registry exposes stable IDs and count");
    Check(!registry.Spawn(definitions[1], error), "duplicate entity ID is rejected");
    Check(registry.Size() == 2U, "duplicate spawn does not mutate registry");
    Check(!registry.SetPosition("entity.player",
        {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, error),
        "non-finite entity movement is rejected");
    Check(registry.Find("entity.player")->position[0] == 0.0F,
        "invalid movement preserves the prior transform");
    Check(registry.SetActive("entity.radio", false, error),
        "entity can be deactivated without destroying its identity");
    Check(!registry.Find("entity.radio")->active, "deactivation is observable");
    Check(registry.Despawn("entity.radio", error), "registered entity can be despawned");
    Check(registry.Find("entity.radio") == nullptr, "despawn removes the entity");
    Check(!registry.Despawn("entity.missing", error), "unknown entity cannot be despawned");

    // End-to-end deterministic interaction -> existing mission progression.
    const GameplayRules rules{};
    SimulationState state;
    Check(InitializeSimulation(MakeMission(), definitions, rules, state, error),
        "simulation initializes mission and entities atomically");
    Check(state.mission.status == game::MissionStatus::Active &&
        state.mission.objectives.front().status == game::ObjectiveStatus::Active,
        "simulation starts the first mission objective");

    std::vector<CommandResult> results;
    const std::array<InteractionCommand, 1> outOfOrder{{
        {"command.telephone-first", "entity.player", "entity.telephone"}
    }};
    Check(AdvanceSimulation(state, rules, 1.0 / 60.0, outOfOrder, results, error),
        "out-of-order interaction is processed as a harmless no-op");
    Check(results.size() == 1U && results[0].outcome == CommandOutcome::Ignored &&
        state.mission.completedObjectives == 0U,
        "out-of-order target cannot skip the active objective");

    const std::array<InteractionCommand, 1> firstObjective{{
        {"command.inspect-radio", "entity.player", "entity.radio"}
    }};
    Check(AdvanceSimulation(state, rules, 1.0 / 60.0, firstObjective, results, error),
        "in-range interaction advances the mission");
    Check(results.size() == 1U &&
        results[0].outcome == CommandOutcome::ObjectiveCompleted &&
        state.mission.completedObjectives == 1U &&
        state.mission.objectives[1].status == game::ObjectiveStatus::Active,
        "interaction activates the next authored objective");

    Check(AdvanceSimulation(state, rules, 1.0 / 60.0, firstObjective, results, error),
        "replayed command is accepted without applying twice");
    Check(results.size() == 1U && results[0].outcome == CommandOutcome::Duplicate &&
        state.mission.completedObjectives == 1U,
        "command replay guard prevents duplicate progression");

    Check(state.entities.SetPosition("entity.telephone", {20.0F, 0.0F, 0.0F}, error),
        "target can be moved outside interaction range");
    const std::array<InteractionCommand, 1> secondObjective{{
        {"command.report-contact", "entity.player", "entity.telephone"}
    }};
    Check(AdvanceSimulation(state, rules, 1.0 / 60.0, secondObjective, results, error),
        "out-of-range interaction is handled without failing the tick");
    Check(results[0].outcome == CommandOutcome::OutOfRange &&
        state.mission.completedObjectives == 1U,
        "out-of-range interaction cannot progress the mission");

    Check(state.entities.SetPosition("entity.telephone", {2.0F, 0.0F, 0.0F}, error),
        "target returns to interaction range");
    Check(AdvanceSimulation(state, rules, 1.0 / 60.0, secondObjective, results, error),
        "second in-range interaction advances the mission");
    Check(results[0].outcome == CommandOutcome::MissionCompleted &&
        state.mission.status == game::MissionStatus::Completed,
        "final interaction completes the mission");

    // A malformed command rolls back the entire tick and preserves caller output.
    const std::uint64_t tickBeforeInvalid = state.tickIndex;
    const double timeBeforeInvalid = state.elapsedSeconds;
    const std::vector<CommandResult> preservedResults{{"preserved.result", CommandOutcome::Duplicate}};
    results = preservedResults;
    const std::array<InteractionCommand, 1> invalidCommand{{
        {"", "entity.player", "entity.radio"}
    }};
    Check(!AdvanceSimulation(state, rules, 1.0 / 60.0, invalidCommand, results, error),
        "malformed command rejects the tick");
    Check(state.tickIndex == tickBeforeInvalid &&
        state.elapsedSeconds == timeBeforeInvalid &&
        state.mission.status == game::MissionStatus::Completed,
        "failed tick leaves simulation state unchanged");
    Check(results.size() == 1U && results[0].commandId == "preserved.result",
        "failed tick leaves result output unchanged");

    // Invalid timestep is rejected without advancing deterministic time.
    Check(!AdvanceSimulation(state, rules, 1.0, {}, results, error),
        "timestep larger than the configured maximum is rejected");
    Check(state.tickIndex == tickBeforeInvalid, "invalid timestep does not advance tick");

    SimulationState unchanged;
    unchanged.tickIndex = 77U;
    Check(!InitializeSimulation(MakeMission(),
        std::span<const entities::EntityDefinition>{}, rules, unchanged, error),
        "empty entity set is valid only if mission itself is valid");
    Check(unchanged.tickIndex == 0U,
        "successful initialization publishes a fresh state");

    if (failures != 0)
    {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Talvisota gameplay-simulation tests passed\n";
    return EXIT_SUCCESS;
}
