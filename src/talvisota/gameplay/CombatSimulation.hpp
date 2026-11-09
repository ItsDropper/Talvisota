#pragma once

#include "talvisota/entities/EntityRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace talvisota::gameplay
{
enum class WeaponArchetype
{
    BoltActionRifle,
    SubmachineGun,
    LightMachineGun
};

struct WeaponDefinition final
{
    std::string_view id;
    std::string_view displayName;
    std::string_view modelAssetId;
    WeaponArchetype archetype = WeaponArchetype::BoltActionRifle;
    std::uint32_t magazineCapacity = 0U;
    float damage = 0.0F;
    float effectiveRangeMeters = 0.0F;
    double secondsBetweenShots = 0.0;
    double reloadSeconds = 0.0;
};

// These are deliberately game-balance values, not real-world performance data.
[[nodiscard]] std::span<const WeaponDefinition> Finnish1939WeaponCatalog() noexcept;
[[nodiscard]] const WeaponDefinition* FindWeaponDefinition(std::string_view weaponId) noexcept;

struct CombatantState final
{
    std::string entityId;
    std::string weaponId;
    float health = 100.0F;
    float maximumHealth = 100.0F;
    std::uint32_t magazineRounds = 0U;
    std::uint32_t reserveRounds = 0U;
    double nextShotAtSeconds = 0.0;
    double reloadCompletesAtSeconds = 0.0;
    double nextRecoveryAtSeconds = 0.0;
};

struct CombatSimulationState final
{
    std::map<std::string, CombatantState, std::less<>> combatants;
    std::set<std::string, std::less<>> processedCommandIds;
    double elapsedSeconds = 0.0;
};

enum class CombatAction
{
    Fire,
    Reload,
    Recover
};

struct CombatCommand final
{
    std::string commandId;
    std::string actorEntityId;
    std::string targetEntityId;
    CombatAction action = CombatAction::Fire;
};

enum class CombatOutcome
{
    Fired,
    TargetDefeated,
    OutOfRange,
    FriendlyTarget,
    EmptyMagazine,
    Cooldown,
    ReloadStarted,
    Reloading,
    MagazineFull,
    NoReserveAmmo,
    Recovered,
    RecoveryCooldown,
    AlreadyAtFullHealth,
    Duplicate,
    Ignored
};

struct CombatResult final
{
    std::string commandId;
    CombatOutcome outcome = CombatOutcome::Ignored;
    float damageApplied = 0.0F;
    float targetHealthRemaining = 0.0F;
};

[[nodiscard]] bool RegisterCombatant(CombatSimulationState& state,
    const entities::EntityRegistry& entityRegistry, std::string_view entityId,
    std::string_view weaponId, float maximumHealth, std::uint32_t reserveRounds,
    std::string& error);

// Commands are applied in caller order after advancing the combat clock.
// Invalid input rejects the whole batch without changing state or output.
[[nodiscard]] bool AdvanceCombat(CombatSimulationState& state,
    const entities::EntityRegistry& entities, double deltaSeconds,
    std::span<const CombatCommand> commands, std::vector<CombatResult>& output,
    std::string& error);
} // namespace talvisota::gameplay
