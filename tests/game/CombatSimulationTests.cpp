#include "talvisota/gameplay/CombatSimulation.hpp"

#include <array>
#include <cstdlib>
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
    Check(FindWeaponDefinition("weapon.mosin_m27") != nullptr,
        "catalog resolves the Mosin-Nagant M/27 rifle");
    Check(FindWeaponDefinition("weapon.suomi_kp31") != nullptr,
        "catalog resolves the Suomi KP/-31 submachine gun");
    Check(FindWeaponDefinition("weapon.lahti_saloranta_m26") != nullptr,
        "catalog resolves the Lahti-Saloranta M/26 light machine gun");
    Check(FindWeaponDefinition("weapon.unknown") == nullptr,
        "unknown weapon IDs are rejected");

    entities::EntityRegistry entities;
    std::string error;
    Check(entities.Spawn({"actor.finn", "faction.finnish", entities::EntityKind::Player,
        {0.0F, 0.0F, 0.0F}, {}}, error), "Finnish player entity is registered");
    Check(entities.Spawn({"actor.enemy", "faction.soviet", entities::EntityKind::Infantry,
        {20.0F, 0.0F, 0.0F}, {}}, error), "opposing infantry entity is registered");
    Check(entities.Spawn({"actor.ally", "faction.finnish", entities::EntityKind::Infantry,
        {15.0F, 0.0F, 0.0F}, {}}, error), "friendly infantry entity is registered");
    Check(entities.Spawn({"actor.distant", "faction.soviet", entities::EntityKind::Infantry,
        {500.0F, 0.0F, 0.0F}, {}}, error), "distant opposing infantry entity is registered");

    CombatSimulationState state;
    Check(RegisterCombatant(state, entities, "actor.finn", "weapon.mosin_m27",
        100.0F, 10U, error), "player combatant receives the rifle loadout");
    Check(RegisterCombatant(state, entities, "actor.enemy", "weapon.suomi_kp31",
        100.0F, 40U, error), "enemy combatant receives the submachine-gun loadout");
    Check(RegisterCombatant(state, entities, "actor.ally", "weapon.lahti_saloranta_m26",
        100.0F, 60U, error), "friendly combatant receives the support-weapon loadout");
    Check(RegisterCombatant(state, entities, "actor.distant", "weapon.suomi_kp31",
        100.0F, 40U, error), "distant combatant receives a loadout");

    const std::array<CombatCommand, 1> firstShot{{
        {"combat.shot.001", "actor.finn", "actor.enemy", CombatAction::Fire}
    }};
    std::vector<CombatResult> results;
    Check(AdvanceCombat(state, entities, 0.05, firstShot, results, error),
        "valid fire command advances combat");
    Check(results.size() == 1U && results[0].outcome == CombatOutcome::Fired &&
        results[0].damageApplied == 68.0F && results[0].targetHealthRemaining == 32.0F,
        "fire applies deterministic game damage");
    Check(state.combatants.at("actor.finn").magazineRounds == 4U,
        "successful fire consumes exactly one round");

    const std::array<CombatCommand, 1> rapidShot{{
        {"combat.shot.002", "actor.finn", "actor.enemy", CombatAction::Fire}
    }};
    Check(AdvanceCombat(state, entities, 0.05, rapidShot, results, error) &&
        results[0].outcome == CombatOutcome::Ignored &&
        state.combatants.at("actor.enemy").health == 32.0F,
        "fire-rate gate prevents a second shot before the configured cooldown");

    const std::array<CombatCommand, 1> friendlyShot{{
        {"combat.shot.003", "actor.finn", "actor.ally", CombatAction::Fire}
    }};
    Check(AdvanceCombat(state, entities, 0.25, friendlyShot, results, error) &&
        results[0].outcome == CombatOutcome::FriendlyTarget &&
        state.combatants.at("actor.ally").health == 100.0F,
        "same-faction damage is blocked");

    const std::array<CombatCommand, 1> distantShot{{
        {"combat.shot.004", "actor.finn", "actor.distant", CombatAction::Fire}
    }};
    Check(AdvanceCombat(state, entities, 0.25, distantShot, results, error) &&
        results[0].outcome == CombatOutcome::OutOfRange &&
        state.combatants.at("actor.distant").health == 100.0F,
        "weapon range gate prevents distant damage");

    const std::array<CombatCommand, 1> reload{{
        {"combat.reload.001", "actor.finn", {}, CombatAction::Reload}
    }};
    Check(AdvanceCombat(state, entities, 0.25, reload, results, error) &&
        results[0].outcome == CombatOutcome::ReloadStarted,
        "reload begins as a timed action");
    Check(state.combatants.at("actor.finn").magazineRounds == 4U,
        "ammunition is not transferred before reload completion");

    for (int step = 0; step < 12; ++step)
        Check(AdvanceCombat(state, entities, 0.25, {}, results, error),
            "combat clock advances while reload is pending");
    Check(state.combatants.at("actor.finn").magazineRounds == 5U &&
        state.combatants.at("actor.finn").reserveRounds == 9U,
        "completed reload transfers only the missing magazine rounds");

    const std::array<CombatCommand, 1> finishingShot{{
        {"combat.shot.005", "actor.finn", "actor.enemy", CombatAction::Fire}
    }};
    Check(AdvanceCombat(state, entities, 0.05, finishingShot, results, error) &&
        results[0].outcome == CombatOutcome::TargetDefeated &&
        state.combatants.at("actor.enemy").health == 0.0F,
        "lethal damage reports target defeat and clamps health at zero");

    Check(AdvanceCombat(state, entities, 0.05, firstShot, results, error) &&
        results[0].outcome == CombatOutcome::Duplicate,
        "replayed combat command cannot apply damage twice");

    CombatSimulationState unchanged = state;
    results = {{"preserved.result", CombatOutcome::Fired, 1.0F, 1.0F}};
    const std::array<CombatCommand, 1> invalid{{
        {"", "actor.finn", "actor.enemy", CombatAction::Fire}
    }};
    Check(!AdvanceCombat(state, entities, 0.05, invalid, results, error),
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
