#include "talvisota/gameplay/CombatSimulation.hpp"
#include "talvisota/content/ContentRegistry.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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
}

int main()
{
    using namespace talvisota;
    using namespace talvisota::gameplay;

    const auto catalog = Finnish1939WeaponCatalog();
    Check(catalog.size() == 3U, "catalog exposes three Finnish 1939-era infantry weapon archetypes");
    Check(FindWeaponDefinition("weapon.mosin_m27") != nullptr &&
        FindWeaponDefinition("weapon.mosin_m27")->modelAssetId == "model.weapon.mosin_m27",
        "catalog resolves the Mosin-Nagant M/27 rifle");
    Check(FindWeaponDefinition("weapon.suomi_kp31") != nullptr &&
        FindWeaponDefinition("weapon.suomi_kp31")->modelAssetId == "model.weapon.suomi_kp31",
        "catalog resolves the Suomi KP/-31 submachine gun");
    Check(FindWeaponDefinition("weapon.lahti_saloranta_m26") != nullptr &&
        FindWeaponDefinition("weapon.lahti_saloranta_m26")->modelAssetId == "model.weapon.lahti_saloranta_m26",
        "catalog resolves the Lahti-Saloranta M/26 light machine gun");
    Check(FindWeaponDefinition("weapon.unknown") == nullptr,
        "unknown weapon IDs are rejected");

    const std::filesystem::path sourceRoot{TALVISOTA_SOURCE_DIR};
    content::ContentRegistry contentRegistry;
    std::vector<content::ContentDiagnostic> contentDiagnostics;
    Check(content::LoadContentManifest(sourceRoot / "content/catalog.tmanifest",
        sourceRoot, contentRegistry, contentDiagnostics),
        "shipped content manifest resolves the weapon model catalog");
    Check(contentRegistry.Find("model.weapon.mosin_m27") != nullptr &&
        contentRegistry.Find("model.weapon.suomi_kp31") != nullptr &&
        contentRegistry.Find("model.weapon.lahti_saloranta_m26") != nullptr,
        "all Finnish 1939 weapon models are registered content assets");

    entities::EntityRegistry registry;
    std::string error;
    Check(registry.Spawn({"actor.finn", "faction.finnish", entities::EntityKind::Player,
        {0.0F, 0.0F, 0.0F}, {}}, error), "Finnish player entity is registered");
    Check(registry.Spawn({"actor.enemy", "faction.soviet", entities::EntityKind::Infantry,
        {20.0F, 0.0F, 0.0F}, {}}, error), "opposing infantry entity is registered");
    Check(registry.Spawn({"actor.ally", "faction.finnish", entities::EntityKind::Infantry,
        {15.0F, 0.0F, 0.0F}, {}}, error), "friendly infantry entity is registered");
    Check(registry.Spawn({"actor.distant", "faction.soviet", entities::EntityKind::Infantry,
        {500.0F, 0.0F, 0.0F}, {}}, error), "distant opposing infantry entity is registered");

    CombatSimulationState state;
    Check(RegisterCombatant(state, registry, "actor.finn", "weapon.mosin_m27",
        100.0F, 10U, error), "player combatant receives the rifle loadout");
    Check(RegisterCombatant(state, registry, "actor.enemy", "weapon.suomi_kp31",
        100.0F, 40U, error), "enemy combatant receives the submachine-gun loadout");
    Check(RegisterCombatant(state, registry, "actor.ally", "weapon.lahti_saloranta_m26",
        100.0F, 60U, error), "friendly combatant receives the support-weapon loadout");
    Check(RegisterCombatant(state, registry, "actor.distant", "weapon.suomi_kp31",
        100.0F, 40U, error), "distant combatant receives a loadout");

    const std::array<CombatCommand, 1> firstShot{{
        {"combat.shot.001", "actor.finn", "actor.enemy", CombatAction::Fire}
    }};
    std::vector<CombatResult> results;
    Check(AdvanceCombat(state, registry, 0.05, firstShot, results, error),
        "valid fire command advances combat");
    Check(results.size() == 1U && results[0].outcome == CombatOutcome::Fired &&
        results[0].damageApplied == 68.0F && results[0].targetHealthRemaining == 32.0F,
        "fire applies deterministic game damage");
    Check(state.combatants.at("actor.finn").magazineRounds == 4U,
        "successful fire consumes exactly one round");

    const std::array<CombatCommand, 1> rapidShot{{
        {"combat.shot.002", "actor.finn", "actor.enemy", CombatAction::Fire}
    }};
    Check(AdvanceCombat(state, registry, 0.05, rapidShot, results, error) &&
        results[0].outcome == CombatOutcome::Cooldown &&
        state.combatants.at("actor.enemy").health == 32.0F,
        "fire-rate gate prevents a second shot before the configured cooldown");

    const std::array<CombatCommand, 1> friendlyShot{{
        {"combat.shot.003", "actor.finn", "actor.ally", CombatAction::Fire}
    }};
    Check(AdvanceCombat(state, registry, 0.25, friendlyShot, results, error) &&
        results[0].outcome == CombatOutcome::FriendlyTarget &&
        state.combatants.at("actor.ally").health == 100.0F,
        "same-faction damage is blocked");

    const std::array<CombatCommand, 1> fullHealthRecovery{{
        {"combat.recover.full", "actor.ally", {}, CombatAction::Recover}
    }};
    Check(AdvanceCombat(state, registry, 0.05, fullHealthRecovery, results, error) &&
        results[0].outcome == CombatOutcome::AlreadyAtFullHealth &&
        state.combatants.at("actor.ally").health == 100.0F,
        "recovery does not waste its cooldown at full health");

    const std::array<CombatCommand, 1> distantShot{{
        {"combat.shot.004", "actor.finn", "actor.distant", CombatAction::Fire}
    }};
    Check(AdvanceCombat(state, registry, 0.25, distantShot, results, error) &&
        results[0].outcome == CombatOutcome::OutOfRange &&
        state.combatants.at("actor.distant").health == 100.0F,
        "weapon range gate prevents distant damage");

    const std::array<CombatCommand, 1> reload{{
        {"combat.reload.001", "actor.finn", {}, CombatAction::Reload}
    }};
    Check(AdvanceCombat(state, registry, 0.25, reload, results, error) &&
        results[0].outcome == CombatOutcome::ReloadStarted,
        "reload begins as a timed action");
    Check(state.combatants.at("actor.finn").magazineRounds == 4U,
        "ammunition is not transferred before reload completion");

    const std::array<CombatCommand, 1> incomingOne{{
        {"combat.incoming.001", "actor.enemy", "actor.finn", CombatAction::Fire}
    }};
    Check(AdvanceCombat(state, registry, 0.25, incomingOne, results, error) &&
        results[0].outcome == CombatOutcome::Fired &&
        state.combatants.at("actor.finn").health == 76.0F,
        "opposing combatant can reduce player health");
    const std::array<CombatCommand, 1> incomingTwo{{
        {"combat.incoming.002", "actor.enemy", "actor.finn", CombatAction::Fire}
    }};
    Check(AdvanceCombat(state, registry, 0.25, incomingTwo, results, error) &&
        results[0].outcome == CombatOutcome::Fired &&
        state.combatants.at("actor.finn").health == 52.0F,
        "subsequent valid hit reduces health again");
    const std::array<CombatCommand, 1> recover{{
        {"combat.recover.001", "actor.finn", {}, CombatAction::Recover}
    }};
    Check(AdvanceCombat(state, registry, 0.25, recover, results, error) &&
        results[0].outcome == CombatOutcome::Recovered &&
        results[0].damageApplied == 25.0F &&
        results[0].targetHealthRemaining == 77.0F,
        "abstract recovery ability restores a bounded amount of health");
    const std::array<CombatCommand, 1> recoverAgain{{
        {"combat.recover.002", "actor.finn", {}, CombatAction::Recover}
    }};
    Check(AdvanceCombat(state, registry, 0.25, recoverAgain, results, error) &&
        results[0].outcome == CombatOutcome::RecoveryCooldown &&
        state.combatants.at("actor.finn").health == 57.0F,
        "recovery ability enforces its game-time cooldown");

    for (int step = 0; step < 12; ++step)
        Check(AdvanceCombat(state, registry, 0.25, {}, results, error),
            "combat clock advances while reload is pending");
    Check(state.combatants.at("actor.finn").magazineRounds == 5U &&
        state.combatants.at("actor.finn").reserveRounds == 9U,
        "completed reload transfers only the missing magazine rounds");

    const std::array<CombatCommand, 1> finishingShot{{
        {"combat.shot.005", "actor.finn", "actor.enemy", CombatAction::Fire}
    }};
    Check(AdvanceCombat(state, registry, 0.05, finishingShot, results, error) &&
        results[0].outcome == CombatOutcome::TargetDefeated &&
        state.combatants.at("actor.enemy").health == 0.0F,
        "lethal damage reports target defeat and clamps health at zero");

    Check(AdvanceCombat(state, registry, 0.05, firstShot, results, error) &&
        results[0].outcome == CombatOutcome::Duplicate,
        "replayed combat command cannot apply damage twice");

    entities::EntityRegistry automaticRegistry;
    Check(automaticRegistry.Spawn({"actor.auto", "faction.finnish",
        entities::EntityKind::Player, {0.0F, 0.0F, 0.0F}, {}}, error),
        "automatic-weapon actor is registered");
    Check(automaticRegistry.Spawn({"actor.auto_target", "faction.soviet",
        entities::EntityKind::Infantry, {10.0F, 0.0F, 0.0F}, {}}, error),
        "automatic-weapon target is registered");
    CombatSimulationState automaticState;
    Check(RegisterCombatant(automaticState, automaticRegistry, "actor.auto",
        "weapon.suomi_kp31", 100.0F, 0U, error),
        "Suomi game loadout initializes with a full magazine");
    Check(RegisterCombatant(automaticState, automaticRegistry, "actor.auto_target",
        "weapon.mosin_m27", 10000.0F, 0U, error),
        "automatic-weapon target receives a test health pool");
    for (int shot = 0; shot < 20; ++shot)
    {
        const std::string commandId = "combat.auto." + std::to_string(shot);
        const std::array<CombatCommand, 1> automaticShot{{
            {commandId, "actor.auto", "actor.auto_target", CombatAction::Fire}
        }};
        Check(AdvanceCombat(automaticState, automaticRegistry, 0.12,
            automaticShot, results, error) &&
            results[0].outcome == CombatOutcome::Fired,
            "automatic archetype fires on each elapsed game cooldown");
    }
    const std::array<CombatCommand, 1> emptyShot{{
        {"combat.auto.empty", "actor.auto", "actor.auto_target", CombatAction::Fire}
    }};
    Check(AdvanceCombat(automaticState, automaticRegistry, 0.12, emptyShot, results, error) &&
        results[0].outcome == CombatOutcome::EmptyMagazine &&
        automaticState.combatants.at("actor.auto").magazineRounds == 0U,
        "empty magazine blocks further firing");

    CombatSimulationState unchanged = state;
    results = {{"preserved.result", CombatOutcome::Fired, 1.0F, 1.0F}};
    const std::array<CombatCommand, 1> invalid{{
        {"", "actor.finn", "actor.enemy", CombatAction::Fire}
    }};
    Check(!AdvanceCombat(state, registry, 0.05, invalid, results, error),
        "invalid command rejects the complete combat step");
    Check(state.elapsedSeconds == unchanged.elapsedSeconds &&
        state.combatants.at("actor.enemy").health == unchanged.combatants.at("actor.enemy").health &&
        results.size() == 1U && results[0].commandId == "preserved.result",
        "failed combat step preserves state and caller output");

    if (failures != 0)
    {
        std::cerr << failures << " combat assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Talvisota combat-simulation tests passed\n";
    return EXIT_SUCCESS;
}
