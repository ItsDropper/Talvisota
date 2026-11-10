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

talvisota::missions::MissionDefinition MakeMission()
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
    using namespace talvisota::combat;
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

    Check(authoredMission.actors.size()==1U && authoredMission.objectives.size()==4U,
        "First Patrol opens with a Finnish corporal and a human check-in before its three farmstead duties");

    const GameplayRules rules{};
    const auto* defensiveAsset=registry.Find("mission.sakkola.defensive-line");
    Check(defensiveAsset!=nullptr,"Sakkola defensive battle is registered in the campaign manifest");
    if(defensiveAsset!=nullptr) {
        AuthoredDefinition defensiveMission;
        std::vector<AuthoredDefinitionDiagnostic> defensiveDiagnostics;
        const bool defensiveLoaded=LoadAuthoredDefinition(sourceRoot/defensiveAsset->relativePath,
            registry,defensiveMission,defensiveDiagnostics);
        Check(defensiveLoaded,"Sakkola defensive battle manifest parses");
        if(defensiveLoaded) {
            SimulationState defensiveState;
            Check(InitializeSimulationFromAuthoredMission(defensiveMission,region,church->position,
                rules,defensiveState,error),"Sakkola defensive battle initializes at the campaign church handoff");
            Check(defensiveMission.actors.size()==7U&&defensiveMission.objectives.size()==6U,
                "defensive battle contains four enemy actors, a three-soldier Finnish section, and an intermediate leader check-in");
            Check(defensiveState.combat.combatants.size()==8U,
                "defensive battle registers the player and all seven infantry combatants");
            const auto finnishActors=std::count_if(defensiveMission.actors.begin(),defensiveMission.actors.end(),
                [](const MissionActorDefinition& actor){return actor.factionId=="faction.finnish";});
            const auto sovietActors=std::count_if(defensiveMission.actors.begin(),defensiveMission.actors.end(),
                [](const MissionActorDefinition& actor){return actor.factionId=="faction.soviet";});
            Check(finnishActors==3&&sovietActors==4,
                "defensive battle has three Finnish section members and four opposing infantry");
            Check(!defensiveState.mission.objectives.empty()&&
                defensiveState.mission.objectives.front().interactionTargetId=="actor.soviet.advance.01",
                "battle begins with a concrete authored enemy objective rather than a disconnected demo");
        }
    }
    const auto* aftermathAsset=registry.Find("mission.sakkola.aftermath");
    Check(aftermathAsset!=nullptr,"Sakkola aftermath is registered as the defensive battle successor");
    if(aftermathAsset!=nullptr) {
        AuthoredDefinition aftermathMission;
        std::vector<AuthoredDefinitionDiagnostic> aftermathDiagnostics;
        const bool aftermathLoaded=LoadAuthoredDefinition(sourceRoot/aftermathAsset->relativePath,
            registry,aftermathMission,aftermathDiagnostics);
        Check(aftermathLoaded,"Sakkola aftermath manifest parses");
        if(aftermathLoaded) {
            SimulationState aftermathState;
            Check(InitializeSimulationFromAuthoredMission(aftermathMission,region,church->position,
                rules,aftermathState,error),"Sakkola aftermath initializes in the existing village");
            Check(aftermathMission.objectives.size()==3U&&aftermathMission.actors.empty()&&
                aftermathMission.nextMissionId.empty(),
                "aftermath is a finite non-combat recovery mission with three authored objectives");
        }
    }
    std::array<float, 3> playerSpawn = well->position;
    playerSpawn[0] += 10.0F;
    SimulationState state;
    Check(InitializeSimulationFromAuthoredMission(authoredMission, region, playerSpawn,
        rules, state, error),
        "authored mission and loaded region initialize a complete gameplay simulation");
    Check(state.playerEntityId == "player.character.sakkola_infantryman" &&
        state.entities.Size() == 5U && state.combat.combatants.size()==2U,
        "simulation contains the player, one friendly corporal, three mission targets, and both combatants");

    const entities::EntityState* const player = state.entities.Find(state.playerEntityId);
    const entities::EntityState* const wellEntity = state.entities.Find("prop.well.farmstead");
    const entities::EntityState* const woodpileEntity = state.entities.Find("prop.woodpile.west");
    const entities::EntityState* const churchEntity = state.entities.Find("church.sakkola");
    const entities::EntityState* const corporal = state.entities.Find("actor.finnish.farmstead.cpl");
    Check(player != nullptr && player->kind == entities::EntityKind::Player &&
        player->factionId == "faction.finnish",
        "player entity inherits the authored Finnish character/faction identity");
    Check(corporal!=nullptr && corporal->kind==entities::EntityKind::Infantry &&
        corporal->factionId=="faction.finnish" &&
        state.combat.combatants.contains("actor.finnish.farmstead.cpl"),
        "the opening corporal is a real friendly infantry character with a registered loadout");
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
    const std::array<InteractionCommand, 1> checkInOutOfRange{{
        {"command.first-patrol.check-in-out-of-range",
            state.playerEntityId, "actor.finnish.farmstead.cpl"}
    }};
    ActiveObjectiveTarget waypoint;
    Check(FindActiveObjectiveTarget(state, state.playerEntityId, waypoint, error) &&
        waypoint.entityId == "actor.finnish.farmstead.cpl" &&
        waypoint.position == corporal->position &&
        waypoint.distanceMeters > rules.interactionRadiusMeters,
        "First Patrol begins with a reachable waypoint to the farmstead corporal");

    ActiveObjectiveTarget preservedWaypoint;
    preservedWaypoint.entityId = "preserved.waypoint";
    preservedWaypoint.position = {9.0F, 8.0F, 7.0F};
    preservedWaypoint.distanceMeters = 6.0F;
    Check(!FindActiveObjectiveTarget(state, "entity.missing", preservedWaypoint, error) &&
        preservedWaypoint.entityId == "preserved.waypoint" &&
        preservedWaypoint.distanceMeters == 6.0F,
        "invalid actor query fails without mutating waypoint output");

    Check(AdvanceSimulation(state, rules, 1.0 / 60.0,
        checkInOutOfRange, results, error) &&
        results.size() == 1U && results[0].outcome == CommandOutcome::OutOfRange &&
        state.mission.completedObjectives == 0U,
        "corporal check-in outside the configured radius cannot advance First Patrol");

    Check(state.entities.SetPosition(state.playerEntityId, corporal->position, error),
        "player simulation entity moves into range of the farmstead corporal");
    const std::array<InteractionCommand,1> checkInCorporal{{
        {"command.first-patrol.check-in-corporal",
            state.playerEntityId,"actor.finnish.farmstead.cpl"}
    }};
    Check(AdvanceSimulation(state,rules,1.0/60.0,checkInCorporal,results,error) &&
        results.size()==1U && results[0].outcome==CommandOutcome::ObjectiveCompleted &&
        state.mission.completedObjectives==1U,
        "speaking with the corporal completes the opening human interaction objective");
    Check(FindActiveObjectiveTarget(state,state.playerEntityId,waypoint,error) &&
        waypoint.entityId=="prop.well.farmstead" && waypoint.position==well->position,
        "corporal check-in directs the player to the well");

    const std::array<InteractionCommand,1> inspectWellOutOfRange{{
        {"command.first-patrol.inspect-well-out-of-range",
            state.playerEntityId,"prop.well.farmstead"}
    }};
    Check(AdvanceSimulation(state,rules,1.0/60.0,inspectWellOutOfRange,results,error) &&
        results.size()==1U && results[0].outcome==CommandOutcome::OutOfRange &&
        state.mission.completedObjectives==1U,
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
        state.mission.completedObjectives == 2U,
        "a new in-range command completes the well objective after the corporal check-in");

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
        state.mission.completedObjectives == 3U,
        "interacting with the real woodpile placement completes the third objective");

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
        state.mission.status == missions::MissionStatus::Completed,
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

    AuthoredDefinition battleMission=authoredMission;
    battleMission.objectives={{"objective.defeat-first","actor.soviet.test"},
        {"objective.defeat-second","actor.soviet.test.02"}};
    battleMission.actors={{"actor.soviet.test","faction.soviet","weapon.suomi_kp31",
        {well->position[0]+12.0F,well->position[2]+12.0F},60U},
        {"actor.soviet.test.02","faction.soviet","weapon.suomi_kp31",
        {well->position[0]+14.0F,well->position[2]+14.0F},60U}};
    SimulationState battleState;
    Check(InitializeSimulationFromAuthoredMission(battleMission,region,playerSpawn,
        rules,battleState,error),
        "authored infantry and actor-target objectives initialize in the gameplay simulation");
    Check(battleState.combat.combatants.size()==3U &&
        battleState.combat.combatants.contains(battleState.playerEntityId) &&
        battleState.combat.combatants.contains("actor.soviet.test") &&
        battleState.combat.combatants.contains("actor.soviet.test.02"),
        "player and authored opposing infantry receive registered combat loadouts");
    auto& playerLoadout=battleState.combat.combatants.at(battleState.playerEntityId);
    const std::uint32_t mosinMagazine=playerLoadout.magazineRounds;
    Check(playerLoadout.weaponId=="weapon.mosin_m27" &&
        playerLoadout.storedWeapons.contains("weapon.suomi_kp31") &&
        playerLoadout.storedWeapons.contains("weapon.lahti_saloranta_m26"),
        "player receives Mosin, Suomi, and Lahti-Saloranta weapon inventory");
    Check(combat::EquipCombatantWeapon(battleState.combat,battleState.entities,
        battleState.playerEntityId,"weapon.suomi_kp31",error) &&
        battleState.combat.combatants.at(battleState.playerEntityId).weaponId=="weapon.suomi_kp31",
        "player can equip the Suomi submachine gun");
    Check(combat::EquipCombatantWeapon(battleState.combat,battleState.entities,
        battleState.playerEntityId,"weapon.mosin_m27",error) &&
        battleState.combat.combatants.at(battleState.playerEntityId).magazineRounds==mosinMagazine,
        "weapon switching preserves ammunition in the stored Mosin magazine");
    ActiveObjectiveTarget combatWaypoint;
    Check(FindActiveObjectiveTarget(battleState,battleState.playerEntityId,combatWaypoint,error) &&
        combatWaypoint.entityId=="actor.soviet.test",
        "active mission waypoint resolves to the authored opposing infantry entity");
    std::vector<CombatCommand> aiCommands;
    Check(BuildBattlefieldAiCommands(battleState,aiCommands,error) &&
        aiCommands.size()==2U &&
        std::all_of(aiCommands.begin(),aiCommands.end(),[&](const CombatCommand& command) {
            return command.targetEntityId==battleState.playerEntityId;
        }),
        "hostile battlefield AI acquires the nearest opposing combatant and emits bounded fire commands");
    Check(BuildBattlefieldAiCommands(battleState,aiCommands,error) && aiCommands.empty(),
        "battlefield AI respects its authored deterministic fire cadence");

    const auto* enemy=battleState.entities.Find("actor.soviet.test");
    Check(enemy!=nullptr,"authored opposing infantry entity is available for aim selection");
    if(enemy!=nullptr) {
        const std::array<float,3> rayOrigin{
            enemy->position[0],enemy->position[1]+1.62F,enemy->position[2]-1.0F};
        std::string selectedTarget;
        Check(FindNearestHostileCombatant(battleState,region,battleState.playerEntityId,
            rayOrigin,{0.0F,0.0F,1.0F},180.0F,0.95F,selectedTarget,error) &&
            selectedTarget=="actor.soviet.test",
            "first-person aim selection resolves the nearest hostile actor");
    }

    std::vector<CombatResult> battleCombatResults;
    const std::array<CombatCommand,1> outOfOrderShot{{
        {"combat.test.out-of-order-shot",battleState.playerEntityId,"actor.soviet.test.02",CombatAction::Fire}
    }};
    Check(AdvanceCombat(battleState.combat,battleState.entities,0.05,
        outOfOrderShot,battleCombatResults,error) &&
        battleCombatResults.size()==1U &&
        battleCombatResults.front().outcome==CombatOutcome::Fired,
        "player fire command damages a later authored hostile objective");
    for(int step=0;step<5;++step)
        Check(AdvanceCombat(battleState.combat,battleState.entities,0.25,
            std::span<const CombatCommand>{},battleCombatResults,error),
            "combat clock advances through weapon cooldown without commands");
    const std::array<CombatCommand,1> outOfOrderFinisher{{
        {"combat.test.out-of-order-finisher",battleState.playerEntityId,"actor.soviet.test.02",CombatAction::Fire}
    }};
    Check(AdvanceCombat(battleState.combat,battleState.entities,0.05,
        outOfOrderFinisher,battleCombatResults,error) &&
        battleCombatResults.size()==1U &&
        battleCombatResults.front().outcome==CombatOutcome::TargetDefeated,
        "subsequent player fire defeats the later authored hostile combatant");
    std::vector<CommandResult> battleMissionResults;
    Check(ApplyCombatMissionEvents(battleState,battleState.combat,
        outOfOrderFinisher,battleCombatResults,battleMissionResults,error) &&
        battleMissionResults.size()==1U &&
        battleMissionResults.front().outcome==CommandOutcome::Ignored &&
        battleState.mission.completedObjectives==0U &&
        battleState.defeatedObjectiveTargets.contains("actor.soviet.test.02"),
        "out-of-order defeat is retained without skipping the active authored objective");
    for(int step=0;step<5;++step)
        Check(AdvanceCombat(battleState.combat,battleState.entities,0.25,
            std::span<const CombatCommand>{},battleCombatResults,error),
            "player weapon cooldown advances before the next authored objective");

    const std::array<CombatCommand,1> firstBattleShot{{
        {"combat.test.first-shot",battleState.playerEntityId,"actor.soviet.test",CombatAction::Fire}
    }};
    Check(AdvanceCombat(battleState.combat,battleState.entities,0.05,
        firstBattleShot,battleCombatResults,error) &&
        battleCombatResults.front().outcome==CombatOutcome::Fired,
        "player can engage the currently active objective after an out-of-order defeat");
    for(int step=0;step<5;++step)
        Check(AdvanceCombat(battleState.combat,battleState.entities,0.25,
            std::span<const CombatCommand>{},battleCombatResults,error),
            "combat clock advances before the second authored objective shot");
    const std::array<CombatCommand,1> finishingBattleShot{{
        {"combat.test.finishing-shot",battleState.playerEntityId,"actor.soviet.test",CombatAction::Fire}
    }};
    Check(AdvanceCombat(battleState.combat,battleState.entities,0.05,
        finishingBattleShot,battleCombatResults,error) &&
        battleCombatResults.size()==1U &&
        battleCombatResults.front().outcome==CombatOutcome::TargetDefeated,
        "subsequent player fire defeats the active authored hostile combatant");
    Check(ApplyCombatMissionEvents(battleState,battleState.combat,
        finishingBattleShot,battleCombatResults,battleMissionResults,error) &&
        battleMissionResults.size()==1U &&
        battleMissionResults.front().outcome==CommandOutcome::MissionCompleted &&
        battleState.mission.status==missions::MissionStatus::Completed &&
        battleState.mission.completedObjectives==2U,
        "combat defeat completes the active objective and consumes the previously defeated later objective");
}


void TestTaipaleMissionInitializesAgainstItsAuthoredWorld()
{
    using namespace talvisota;
    using namespace talvisota::content;
    using namespace talvisota::gameplay;
    using namespace talvisota::world;

    const std::filesystem::path sourceRoot{TALVISOTA_SOURCE_DIR};
    std::string error;
    ContentRegistry registry;
    std::vector<ContentDiagnostic> contentDiagnostics;
    const bool manifestLoaded = LoadContentManifest(sourceRoot / "content/catalog.tmanifest",
        sourceRoot, registry, contentDiagnostics);
    Check(manifestLoaded, "Taipale mission content manifest loads");
    if (!manifestLoaded) return;

    const auto* missionAsset = registry.Find("mission.taipale.riverfront-watch");
    const auto* worldAsset = registry.Find("world.taipale");
    const auto* regionAsset = registry.Find("region.taipale.riverfront");
    const bool definitionsRegistered =
        missionAsset != nullptr && worldAsset != nullptr && regionAsset != nullptr;
    Check(definitionsRegistered, "Taipale mission, world and region are registered");
    if (!definitionsRegistered) return;

    AuthoredDefinition mission;
    std::vector<AuthoredDefinitionDiagnostic> missionDiagnostics;
    const bool missionLoaded = LoadAuthoredDefinition(sourceRoot / missionAsset->relativePath,
        registry, mission, missionDiagnostics);
    Check(missionLoaded, "Taipale mission definition loads");
    if (!missionLoaded) return;

    WorldDefinition world;
    std::vector<WorldDiagnostic> worldDiagnostics;
    const bool worldLoaded = LoadWorldDefinition(sourceRoot / worldAsset->relativePath,
        registry, world, worldDiagnostics);
    Check(worldLoaded, "Taipale world definition loads");
    if (!worldLoaded) return;

    WorldRegion region;
    std::vector<WorldRegionDiagnostic> regionDiagnostics;
    const bool regionLoaded = LoadWorldRegion(sourceRoot / regionAsset->relativePath,
        registry, world, region, regionDiagnostics);
    Check(regionLoaded, "Taipale world region loads");
    if (!regionLoaded) return;

    float spawnHeight = 0.0F;
    std::array<float, 3> spawnNormal{};
    const bool spawnSamples = SampleTerrainHeight(region, -90.0F, 70.0F,
        spawnHeight, spawnNormal, error);
    Check(spawnSamples, "Taipale player start samples valid terrain");
    if (!spawnSamples) return;

    const GameplayRules rules{};
    SimulationState simulation;
    Check(InitializeSimulationFromAuthoredMission(mission, region,
            {-90.0F, spawnHeight, 70.0F}, rules, simulation, error),
        "Taipale introductory mission initializes in its selected world and region");
    Check(simulation.mission.objectives.size() == 3U &&
            simulation.playerEntityId == "player.character.sakkola_infantryman",
        "Taipale mission publishes its three location objectives and player entity");
    for (const auto& objective : simulation.mission.objectives) {
        const auto* target = simulation.entities.Find(objective.interactionTargetId);
        Check(target != nullptr && target->kind == entities::EntityKind::WorldObject,
            "Each Taipale objective resolves to its authored world landmark");
    }
}

int main()
{
    using namespace talvisota;
    using namespace talvisota::gameplay;
    std::string error;

    TestAuthoredWorldSimulation();
    TestTaipaleMissionInitializesAgainstItsAuthoredWorld();

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
    Check(state.mission.status == missions::MissionStatus::Active &&
        state.mission.objectives.front().status == missions::ObjectiveStatus::Active,
        "simulation starts the first mission objective");

    InteractionTargetSelection selectedTarget;
    Check(FindNearestActiveObjectiveTarget(state, "entity.player",
        rules.interactionRadiusMeters, selectedTarget, error) &&
        selectedTarget.entityId == "entity.radio" &&
        selectedTarget.distanceMeters == 1.0F,
        "target query selects the active objective target and reports its distance");

    SimulationState malformedMissionState = state;
    malformedMissionState.mission.completedObjectives = 1U;
    selectedTarget.entityId = "preserved.selection";
    selectedTarget.distanceMeters = 9.0F;
    Check(!FindNearestActiveObjectiveTarget(malformedMissionState, "entity.player",
        rules.interactionRadiusMeters, selectedTarget, error) &&
        selectedTarget.entityId == "preserved.selection" &&
        selectedTarget.distanceMeters == 9.0F,
        "interaction target query rejects inconsistent mission progression without mutating output");

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
        state.mission.objectives[1].status == missions::ObjectiveStatus::Active,
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
        state.mission.status == missions::MissionStatus::Completed,
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
        state.mission.status == missions::MissionStatus::Completed,
        "failed tick leaves simulation state unchanged");
    Check(results.size() == 1U && results[0].commandId == "preserved.result",
        "failed tick leaves result output unchanged");

    Check(!AdvanceSimulation(state, rules, 1.0, {}, results, error),
        "timestep larger than the configured maximum is rejected");
    Check(state.tickIndex == tickBeforeInvalid, "invalid timestep does not advance tick");

    SimulationState unchanged;
    unchanged.tickIndex = 77U;
    const missions::MissionDefinition invalidMission;
    Check(!InitializeSimulation(invalidMission,
        std::span<const entities::EntityDefinition>{}, rules, unchanged, error),
        "invalid mission definition rejects initialization");
    Check(unchanged.tickIndex == 77U,
        "failed initialization preserves the caller's prior state");


    // Combat defeats can complete authored actor-target objectives without
    // routing the event through a world-object interaction.
    {
        const std::array<entities::EntityDefinition, 2> combatMissionEntities{{
            {"actor.mission_player", "faction.finnish", entities::EntityKind::Player,
                {0.0F, 0.0F, 0.0F}, {}},
            {"actor.mission_target", "faction.soviet", entities::EntityKind::Infantry,
                {10.0F, 0.0F, 0.0F}, "target.enemy-defeated"}
        }};
        const missions::MissionDefinition combatMission{
            "mission.combat-objective",
            {{"objective.defeat-target", "target.enemy-defeated"}}
        };
        SimulationState combatMissionState;
        Check(InitializeSimulation(combatMission, combatMissionEntities, rules,
            combatMissionState, error), "combat objective mission initializes");
        CombatSimulationState combatState;
        Check(RegisterCombatant(combatState, combatMissionState.entities,
            "actor.mission_player", "weapon.mosin_m27", 100.0F, 5U, error),
            "mission player registers as a combatant");
        Check(RegisterCombatant(combatState, combatMissionState.entities,
            "actor.mission_target", "weapon.suomi_kp31", 60.0F, 5U, error),
            "authored mission target registers as a combatant");
        const std::array<CombatCommand, 1> defeatCommand{{
            {"combat.mission.defeat-target", "actor.mission_player",
                "actor.mission_target", CombatAction::Fire}
        }};
        std::vector<CombatResult> combatResults;
        Check(AdvanceCombat(combatState, combatMissionState.entities, 0.05,
            defeatCommand, combatResults, error) &&
            combatResults.size() == 1U &&
            combatResults[0].outcome == CombatOutcome::TargetDefeated,
            "combat simulation reports a lethal mission-target event");
        std::vector<CommandResult> missionResults;
        Check(ApplyCombatMissionEvents(combatMissionState, combatState,
            defeatCommand, combatResults, missionResults, error) &&
            missionResults.size() == 1U &&
            missionResults[0].outcome == CommandOutcome::MissionCompleted &&
            combatMissionState.mission.status == missions::MissionStatus::Completed,
            "defeating the authored actor target completes its mission objective");
        Check(ApplyCombatMissionEvents(combatMissionState, combatState,
            defeatCommand, combatResults, missionResults, error) &&
            missionResults.size() == 1U &&
            missionResults[0].outcome == CommandOutcome::Duplicate &&
            combatMissionState.mission.completedObjectives == 1U,
            "replayed combat defeat cannot advance mission progression twice");

        SimulationState preservedCombatMission;
        Check(InitializeSimulation(combatMission, combatMissionEntities, rules,
            preservedCombatMission, error), "second combat objective fixture initializes");
        std::vector<CommandResult> preservedMissionResults{
            {"preserved.mission.result", CommandOutcome::Ignored}};
        const std::array<CombatResult, 1> mismatchedCombatResults{{
            {"combat.other-command", CombatOutcome::TargetDefeated, 60.0F, 0.0F}
        }};
        Check(!ApplyCombatMissionEvents(preservedCombatMission, combatState,
            defeatCommand, mismatchedCombatResults, preservedMissionResults, error) &&
            preservedCombatMission.mission.completedObjectives == 0U &&
            preservedMissionResults.size() == 1U &&
            preservedMissionResults[0].commandId == "preserved.mission.result",
            "mismatched combat event rejects atomically without changing mission state or output");
    }

    if (failures != 0)
    {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Talvisota gameplay-simulation tests passed\n";
    return EXIT_SUCCESS;
}
