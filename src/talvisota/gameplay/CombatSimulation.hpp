#pragma once

// Compatibility include for code that used the pre-domain-split path.
#include "talvisota/combat/CombatSimulation.hpp"

namespace talvisota::gameplay
{
using combat::WeaponArchetype;
using combat::WeaponDefinition;
using combat::WeaponAmmoState;
using combat::CombatantState;
using combat::CombatSimulationState;
using combat::CombatAction;
using combat::CombatCommand;
using combat::CombatOutcome;
using combat::CombatResult;
using combat::Finnish1939WeaponCatalog;
using combat::FindWeaponDefinition;
using combat::RegisterCombatant;
using combat::AddCombatantWeapon;
using combat::EquipCombatantWeapon;
using combat::AdvanceCombat;
} // namespace talvisota::gameplay
