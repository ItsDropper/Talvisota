#include "talvisota/gameplay/GameplaySimulation.hpp"

#include "talvisota/content/AuthoredDefinition.hpp"
#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
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


void TestAuthoredWorldSimulation()
{
    using namespace talvisota;
    using namespace talvisota::content;
    using namespace talvisota::gameplay;
    using namespace talvisota::world;

    const std::filesystem::path sourceRoot{TALVISOTA_SOURCE_DIR};
    std::string error;
    ContentRegistry registry;
    std::vector<ContentDiagnostic> contentDiagnostics;
    Check(LoadContentManifest(sourceRoot / "content/catalog.tmanifest", sourceRoot,
        registry, contentDiagnostics), "shipped content manifest loads for gameplay integration");

    const auto* missionAsset = registry.Find("mission.sakkola.first-patrol");
    const auto* worldAsset = registry.Find("world.sakkola");
    const auto* regionAsset = registry.Find("region.sakkola.church_village");
    Check(missionAsset != nullptr && worldAsset != nullptr && regionAsset != nullptr,
        "First Patrol mission, Sakkola world, and church-village region are registered");
    if (missionAsset == nullptr || worldAsset == nullptr || regionAsset == nullptr)
        return;

    AuthoredDefinition authoredMission;
    std::vector<AuthoredDefinitionDiagnostic> missionDiagnostics;
    Check(LoadAuthoredDefinition(sourceRoot / missionAsset->relativePath, registry,
        authoredMission, missionDiagnostics),
        "shipped First Patrol mission parses through the authored content loader");

    WorldDefinition worldDefinition;
    std::vector<WorldDiagnostic> worldDiagnostics;
    Check(LoadWorldDefinition(sourceRoot / worldAsset->relativePath, registry,
        worldDefinition, worldDiagnostics),
        "shipped Sakkola world definition loads");

    WorldRegion region;
    std::vector<WorldRegionDiagnostic> regionDiagnostics;
    Check(LoadWorldRegion(sourceRoot / regionAsset->relativePath, registry,
        worldDefinition, region, regionDiagnostics),
        "shipped Sakkola church-village region and placements load");
    if (authoredMission.id.empty() || region.id.empty())
        return;

    const auto findPlacement = [&](const std::string_view id) -> const WorldPlacement*
    {
        const auto found = std::find_if(region.placements.begin(), region.placements.end(),
            [id](const WorldPlacement& placement) { return placement.id == id; });
        return found == region.placements.end() ? nullptr : &*found;
    };
    const WorldPlacement* const well = findPlacement("prop.well.farmstead");
    const WorldPlacement* const woodpile = findPlacement("prop.woodpile.west");
    const WorldPlacement* const church = findPlacement("church.sakkola");
    Check(well != nullptr && woodpile != nullptr && church != nullptr,
        "all three authored mission targets resolve to real Sakkola region placements");
    if (well == nullptr || woodpile == nullptr || church == nullptr)
        return;

    const GameplayRules rules{};
    std::array<float, 3> playerSpawn = well->position;
    playerSpawn[0] += 10.0F;
    SimulationState state;
    Check(InitializeSimulationFromAuthoredMission(authoredMission, region, playerSpawn,
        rules, state, error),
        "authored mission and loaded region initialize a complete gameplay simulation");
    Check(state.playerEntityId == "player.character.sakkola_infantryman" &&
        state.entities.Size() == 4U,
        "simulation contains one authored player and exactly three mission target entities");

    const entities::EntityState* const player = state.entities.Find(state.playerEntityId);
    const entities::EntityState* const wellEntity = state.entities.Find("prop.well.farmstead");
    const entities::EntityState* const woodpileEntity = state.entities.Find("prop.woodpile.west");
    const entities::EntityState* const churchEntity = state.entities.Find("church.sakkola");
    Check(player != nullptr && player->kind == entities::EntityKind::Player &&
        player->factionId == "faction.finnish",
        "player entity inherits the authored Finnish character/faction identity");
    Check(wellEntity != nullptr && woodpileEntity != nullptr && churchEntity != nullptr &&
        wellEntity->factionId.empty() && woodpileEntity->factionId.empty() &&
        churchEntity->factionId.empty(),
        "world-object entities remain unaligned rather than being assigned the player's faction");
    Check(wellEntity != nullptr && wellEntity->position == well->position &&
        woodpileEntity != nullptr && woodpileEntity->position == woodpile->position &&
        churchEntity != nullptr && churchEntity->position == church->position,
        "mission entities use exact authored placement transforms from the loaded region");

    Check(player != nullptr && player->position == playerSpawn,
        "player entity starts at the supplied in-region spawn position");

    std::vector<CommandResult> results;
    const std::array<InteractionCommand, 1> inspectWellOutOfRange{{
        {"command.first-patrol.inspect-well-out-of-range",
            state.playerEntityId, "prop.well.farmstead"}
    }};
    ActiveObjectiveTarget waypoint;
    Check(FindActiveObjectiveTarget(state, state.playerEntityId, waypoint, error) &&
        waypoint.entityId == "prop.well.farmstead" &&
        waypoint.position == well->position &&
        waypoint.distanceMeters > rules.interactionRadiusMeters,
        "all-range objective query resolves the authored well waypoint from player spawn");

    ActiveObjectiveTarget preservedWaypoint;
    preservedWaypoint.entityId = "preserved.waypoint";
    preservedWaypoint.position = {9.0F, 8.0F, 7.0F};
    preservedWaypoint.distanceMeters = 6.0F;
    Check(!FindActiveObjectiveTarget(state, "entity.missing", preservedWaypoint, error) &&
        preservedWaypoint.entityId == "preserved.waypoint" &&
        preservedWaypoint.distanceMeters == 6.0F,
        "invalid actor query fails without mutating waypoint output");

    Check(AdvanceSimulation(state, rules, 1.0 / 60.0,
        inspectWellOutOfRange, results, error) &&
        results.size() == 1U && results[0].outcome == CommandOutcome::OutOfRange &&
        state.mission.completedObjectives == 0U,
        "well interaction outside the configured radius cannot advance First Patrol");

    Check(state.entities.SetPosition(state.playerEntityId, well->position, error),
        "player simulation entity moves into range of the authored well placement");
    InteractionTargetSelection authoredSelection;
    Check(FindNearestActiveObjectiveTarget(state, state.playerEntityId,
        rules.interactionRadiusMeters, authoredSelection, error) &&
        authoredSelection.entityId == "prop.well.farmstead" &&
        authoredSelection.distanceMeters == 0.0F,
        "active First Patrol objective resolves to the nearest in-range authored well entity");
    const std::array<InteractionCommand, 1> inspectWellInRange{{
        {"command.first-patrol.inspect-well-in-range",
            state.playerEntityId, "prop.well.farmstead"}
    }};
    Check(AdvanceSimulation(state, rules, 1.0 / 60.0,
        inspectWellInRange, results, error) &&
        results.size() == 1U && results[0].outcome == CommandOutcome::ObjectiveCompleted &&
        state.mission.completedObjectives == 1U,
        "a new in-range command completes the first authored objective after the out-of-range attempt");

    Check(FindActiveObjectiveTarget(state, state.playerEntityId, waypoint, error) &&
        waypoint.entityId == "prop.woodpile.west" &&
        waypoint.position == woodpile->position,
        "all-range waypoint advances to the authored woodpile after well completion");

    SimulationState missingTargetState = state;
    Check(missingTargetState.entities.Despawn("prop.woodpile.west", error),
        "active waypoint target can be removed for missing-target regression");
    preservedWaypoint.entityId = "preserved.missing";
    Check(!FindActiveObjectiveTarget(missingTargetState, state.playerEntityId,
        preservedWaypoint, error) && preservedWaypoint.entityId == "preserved.missing",
        "missing active objective entity is rejected without changing waypoint output");

    SimulationState inactiveTargetState = state;
    Check(inactiveTargetState.entities.SetActive("prop.woodpile.west", false, error),
        "active waypoint target can be deactivated for regression");
    preservedWaypoint.entityId = "preserved.inactive";
    Check(!FindActiveObjectiveTarget(inactiveTargetState, state.playerEntityId,
        preservedWaypoint, error) && preservedWaypoint.entityId == "preserved.inactive",
        "inactive active-objective target is rejected without changing waypoint output");


    Check(state.entities.SetPosition(state.playerEntityId, woodpile->position, error),
        "player simulation entity moves to the authored woodpile placement");
    const std::array<InteractionCommand, 1> inspectWoodpile{{
        {"command.first-patrol.inspect-woodpile", state.playerEntityId, "prop.woodpile.west"}
    }};
    Check(AdvanceSimulation(state, rules, 1.0 / 60.0, inspectWoodpile, results, error) &&
        results.size() == 1U && results[0].outcome == CommandOutcome::ObjectiveCompleted &&
        state.mission.completedObjectives == 2U,
        "interacting with the real woodpile placement completes the second objective");

    Check(FindActiveObjectiveTarget(state, state.playerEntityId, waypoint, error) &&
        waypoint.entityId == "church.sakkola" &&
        waypoint.position == church->position,
        "all-range waypoint advances to the authored church after woodpile completion");


    Check(state.entities.SetPosition(state.playerEntityId, church->position, error),
        "player simulation entity moves to the authored church placement");
    const std::array<InteractionCommand, 1> reportChurch{{
        {"command.first-patrol.report-church", state.playerEntityId, "church.sakkola"}
    }};
    Check(AdvanceSimulation(state, rules, 1.0 / 60.0, reportChurch, results, error) &&
        results.size() == 1U && results[0].outcome == CommandOutcome::MissionCompleted &&
        state.mission.status == game::MissionStatus::Completed,
        "interacting with the real church placement completes First Patrol");

    Check(FindActiveObjectiveTarget(state, state.playerEntityId, waypoint, error) &&
        waypoint.entityId.empty(),
        "completed First Patrol produces no active-objective waypoint");


    SimulationState preserved;
    preserved.tickIndex = 91U;
    auto mismatchedRegion = region;
    mismatchedRegion.id = "region.other";
    Check(!InitializeSimulationFromAuthoredMission(authoredMission, mismatchedRegion,
        well->position, rules, preserved, error),
        "authored mission cannot initialize against a different region");
    Check(preserved.tickIndex == 91U,
        "region mismatch preserves the caller's existing simulation state");

    auto incompleteRegion = region;
    std::erase_if(incompleteRegion.placements,
        [](const WorldPlacement& placement) { return placement.id == "prop.woodpile.west"; });
    Check(!InitializeSimulationFromAuthoredMission(authoredMission, incompleteRegion,
        well->position, rules, preserved, error),
        "mission cannot initialize when an authored target has no world placement");
    Check(preserved.tickIndex == 91U,
        "missing mission placement preserves the caller's existing simulation state");
}

int main()
{
    using namespace talvisota;
    using namespace talvisota::gameplay;
    std::string error;

    TestAuthoredWorldSimulation();

    // Registry validation and stable lookup.
    entities::EntityRegistry registry;
    const auto definitions = MakeEntities();
    Check(registry.Spawn(definitions[0], error), "valid player entity spawns");
    Check(registry.Spawn(definitions[1], error), "valid world entity spawns");
    Check(registry.Size() == 2U && registry.Find("entity.radio") != nullptr,
        "registry exposes stable IDs and count");
    Check(!registry.Spawn(definitions[1], error), "duplicate entity ID is rejected");
    auto duplicateTarget = definitions[2];
    duplicateTarget.interactionTargetId = definitions[1].interactionTargetId;
    Check(!registry.Spawn(duplicateTarget, error),
        "duplicate mission interaction target ID is rejected");
    Check(registry.Size() == 2U, "rejected spawns do not mutate registry");
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

    entities::EntityRegistry factionValidation;
    const entities::EntityDefinition neutralWorldObject{
        "entity.neutral-world-object", {}, entities::EntityKind::WorldObject,
        {0.0F, 0.0F, 0.0F}, "target.neutral-world-object"
    };
    Check(factionValidation.Spawn(neutralWorldObject, error),
        "unaligned world objects may omit faction ownership");
    const entities::EntityDefinition factionlessPlayer{
        "entity.factionless-player", {}, entities::EntityKind::Player,
        {0.0F, 0.0F, 0.0F}, {}
    };
    Check(!factionValidation.Spawn(factionlessPlayer, error),
        "player entities must retain an authored faction");

    const GameplayRules rules{};
    const auto missionDefinition = MakeMission();

    // Identical inputs produce identical state transitions.
    SimulationState deterministicA;
    SimulationState deterministicB;
    Check(InitializeSimulation(missionDefinition, definitions, rules, deterministicA, error),
        "first deterministic simulation initializes");
    Check(InitializeSimulation(missionDefinition, definitions, rules, deterministicB, error),
        "second deterministic simulation initializes");
    const std::array<InteractionCommand, 1> deterministicCommand{{
        {"command.determinism", "entity.player", "entity.radio"}
    }};
    std::vector<CommandResult> deterministicResultsA;
    std::vector<CommandResult> deterministicResultsB;
    Check(AdvanceSimulation(deterministicA, rules, 1.0 / 60.0,
        deterministicCommand, deterministicResultsA, error),
        "first deterministic simulation advances");
    Check(AdvanceSimulation(deterministicB, rules, 1.0 / 60.0,
        deterministicCommand, deterministicResultsB, error),
        "second deterministic simulation advances");
    Check(deterministicA.tickIndex == deterministicB.tickIndex &&
        deterministicA.elapsedSeconds == deterministicB.elapsedSeconds &&
        deterministicA.mission.completedObjectives == deterministicB.mission.completedObjectives &&
        deterministicResultsA.size() == deterministicResultsB.size() &&
        deterministicResultsA[0].outcome == deterministicResultsB[0].outcome,
        "same ordered commands and timestep produce identical simulation results");

    // End-to-end interaction -> existing mission progression.
    SimulationState state;
    Check(InitializeSimulation(missionDefinition, definitions, rules, state, error),
        "simulation initializes mission and entities atomically");
    Check(state.mission.status == game::MissionStatus::Active &&
        state.mission.objectives.front().status == game::ObjectiveStatus::Active,
        "simulation starts the first mission objective");

    InteractionTargetSelection selectedTarget;
    Check(FindNearestActiveObjectiveTarget(state, "entity.player",
        rules.interactionRadiusMeters, selectedTarget, error) &&
        selectedTarget.entityId == "entity.radio" &&
        selectedTarget.distanceMeters == 1.0F,
        "target query selects the active objective target and reports its distance");

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

    Check(FindNearestActiveObjectiveTarget(state, "entity.player",
        rules.interactionRadiusMeters, selectedTarget, error) &&
        selectedTarget.entityId == "entity.telephone" &&
        selectedTarget.distanceMeters == 2.0F,
        "target query follows mission progression to the newly active objective");

    Check(AdvanceSimulation(state, rules, 1.0 / 60.0, firstObjective, results, error),
        "replayed command is accepted without applying twice");
    Check(results.size() == 1U && results[0].outcome == CommandOutcome::Duplicate &&
        state.mission.completedObjectives == 1U,
        "command replay guard prevents duplicate progression");

    Check(state.entities.SetPosition("entity.telephone", {20.0F, 0.0F, 0.0F}, error),
        "target can be moved outside interaction range");
    const std::array<InteractionCommand, 1> secondObjectiveOutOfRange{{
        {"command.report-contact-out-of-range", "entity.player", "entity.telephone"}
    }};
    Check(AdvanceSimulation(state, rules, 1.0 / 60.0,
        secondObjectiveOutOfRange, results, error),
        "out-of-range interaction is handled without failing the tick");
    Check(results[0].outcome == CommandOutcome::OutOfRange &&
        state.mission.completedObjectives == 1U,
        "out-of-range interaction cannot progress the mission");

    Check(FindNearestActiveObjectiveTarget(state, "entity.player",
        rules.interactionRadiusMeters, selectedTarget, error) &&
        selectedTarget.entityId.empty(),
        "target query returns no selection when the active objective is outside search range");

    Check(state.entities.SetPosition("entity.telephone", {2.0F, 0.0F, 0.0F}, error),
        "target returns to interaction range");
    const std::array<InteractionCommand, 1> secondObjectiveInRange{{
        {"command.report-contact-in-range", "entity.player", "entity.telephone"}
    }};
    Check(AdvanceSimulation(state, rules, 1.0 / 60.0,
        secondObjectiveInRange, results, error),
        "second in-range interaction advances the mission");
    Check(results[0].outcome == CommandOutcome::MissionCompleted &&
        state.mission.status == game::MissionStatus::Completed,
        "final interaction completes the mission");

    // A malformed command rolls back the entire tick and preserves caller output.
    const std::uint64_t tickBeforeInvalid = state.tickIndex;
    const double timeBeforeInvalid = state.elapsedSeconds;
    results = {{"preserved.result", CommandOutcome::Duplicate}};
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

    Check(!AdvanceSimulation(state, rules, 1.0, {}, results, error),
        "timestep larger than the configured maximum is rejected");
    Check(state.tickIndex == tickBeforeInvalid, "invalid timestep does not advance tick");

    SimulationState unchanged;
    unchanged.tickIndex = 77U;
    const game::MissionDefinition invalidMission;
    Check(!InitializeSimulation(invalidMission,
        std::span<const entities::EntityDefinition>{}, rules, unchanged, error),
        "invalid mission definition rejects initialization");
    Check(unchanged.tickIndex == 77U,
        "failed initialization preserves the caller's prior state");

    if (failures != 0)
    {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Talvisota gameplay-simulation tests passed\n";
    return EXIT_SUCCESS;
}
