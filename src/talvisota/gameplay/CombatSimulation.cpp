#include "talvisota/gameplay/CombatSimulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace talvisota::gameplay
{
namespace
{
constexpr std::array<WeaponDefinition, 3> Weapons{{
    {"weapon.mosin_m27", "Mosin-Nagant M/27", "model.weapon.mosin_m27", WeaponArchetype::BoltActionRifle,
        5U, 68.0F, 180.0F, 1.05, 2.8},
    {"weapon.suomi_kp31", "Suomi KP/-31", "model.weapon.suomi_kp31", WeaponArchetype::SubmachineGun,
        20U, 24.0F, 58.0F, 0.12, 2.6},
    {"weapon.lahti_saloranta_m26", "Lahti-Saloranta M/26", "model.weapon.lahti_saloranta_m26",
        WeaponArchetype::LightMachineGun, 20U, 31.0F, 105.0F, 0.16, 3.2}
}};
constexpr std::size_t MaximumCommandsPerStep = 4096U;
constexpr std::size_t MaximumProcessedCommands = 65536U;

bool IsFinitePositive(const float value) noexcept
{
    return std::isfinite(value) && value > 0.0F;
}

double DistanceBetween(const entities::EntityState& first,
    const entities::EntityState& second) noexcept
{
    const double x = static_cast<double>(first.position[0]) - second.position[0];
    const double y = static_cast<double>(first.position[1]) - second.position[1];
    const double z = static_cast<double>(first.position[2]) - second.position[2];
    return std::sqrt(x * x + y * y + z * z);
}
} // namespace

std::span<const WeaponDefinition> Finnish1939WeaponCatalog() noexcept
{
    return Weapons;
}

const WeaponDefinition* FindWeaponDefinition(const std::string_view weaponId) noexcept
{
    const auto found = std::find_if(Weapons.begin(), Weapons.end(),
        [weaponId](const WeaponDefinition& weapon) { return weapon.id == weaponId; });
    return found == Weapons.end() ? nullptr : &*found;
}

bool RegisterCombatant(CombatSimulationState& state,
    const entities::EntityRegistry& entityRegistry, const std::string_view entityId,
    const std::string_view weaponId, const float maximumHealth,
    const std::uint32_t reserveRounds, std::string& error)
{
    if (!entities::IsValidStableId(entityId) ||
        !entities::IsValidStableId(weaponId) ||
        !IsFinitePositive(maximumHealth) || maximumHealth > 10000.0F)
    {
        error = "Combatant registration contains an invalid ID or health value.";
        return false;
    }
    const entities::EntityState* const entity = entityRegistry.Find(entityId);
    if (entity == nullptr || !entity->active ||
        entity->kind == entities::EntityKind::WorldObject ||
        !entities::IsValidStableId(entity->factionId) ||
        !std::isfinite(entity->position[0]) || !std::isfinite(entity->position[1]) ||
        !std::isfinite(entity->position[2]))
    {
        error = "Combatants must reference an active faction-aligned actor entity.";
        return false;
    }
    const WeaponDefinition* const weapon = FindWeaponDefinition(weaponId);
    if (weapon == nullptr)
    {
        error = "Combatant references an unknown weapon definition: " + std::string(weaponId);
        return false;
    }
    if (reserveRounds > 10000U || state.combatants.contains(entityId))
    {
        error = "Combatant reserve ammunition is invalid or the entity is already registered.";
        return false;
    }

    CombatantState candidate;
    candidate.entityId = entityId;
    candidate.weaponId = weapon->id;
    candidate.health = maximumHealth;
    candidate.maximumHealth = maximumHealth;
    candidate.magazineRounds = weapon->magazineCapacity;
    candidate.reserveRounds = reserveRounds;
    const std::string stableEntityId = candidate.entityId;
    state.combatants.emplace(stableEntityId, std::move(candidate));
    error.clear();
    return true;
}

bool AdvanceCombat(CombatSimulationState& state,
    const entities::EntityRegistry& entityRegistry, const double deltaSeconds,
    const std::span<const CombatCommand> commands, std::vector<CombatResult>& output,
    std::string& error)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0 ||
        deltaSeconds > 0.25 || commands.size() > MaximumCommandsPerStep ||
        state.processedCommandIds.size() > MaximumProcessedCommands ||
        !std::isfinite(state.elapsedSeconds) || state.elapsedSeconds < 0.0 ||
        state.elapsedSeconds > std::numeric_limits<double>::max() - deltaSeconds)
    {
        error = "Combat step contains an invalid timestep, clock, or command count.";
        return false;
    }

    CombatSimulationState candidate = state;
    std::vector<CombatResult> results;
    results.reserve(commands.size());
    candidate.elapsedSeconds += deltaSeconds;

    for (auto& [entityId, combatant] : candidate.combatants)
    {
        const entities::EntityState* const entity = entityRegistry.Find(entityId);
        const WeaponDefinition* const registeredWeapon = FindWeaponDefinition(combatant.weaponId);
        if (combatant.entityId != entityId || !entities::IsValidStableId(entityId) ||
            entity == nullptr || !entity->active ||
            entity->kind == entities::EntityKind::WorldObject ||
            !entities::IsValidStableId(entity->factionId) ||
            !std::isfinite(entity->position[0]) || !std::isfinite(entity->position[1]) ||
            !std::isfinite(entity->position[2]) ||
            !std::isfinite(combatant.health) || !std::isfinite(combatant.maximumHealth) ||
            combatant.maximumHealth <= 0.0F || combatant.health < 0.0F ||
            combatant.health > combatant.maximumHealth || registeredWeapon == nullptr ||
            combatant.magazineRounds > registeredWeapon->magazineCapacity ||
            combatant.reserveRounds > 10000U ||
            !std::isfinite(combatant.nextShotAtSeconds) ||
            !std::isfinite(combatant.reloadCompletesAtSeconds) ||
            combatant.nextShotAtSeconds < 0.0 || combatant.reloadCompletesAtSeconds < 0.0)
        {
            error = "Combat state contains a missing entity or invalid combatant record: " + entityId;
            return false;
        }

        if (combatant.reloadCompletesAtSeconds > 0.0 &&
            candidate.elapsedSeconds >= combatant.reloadCompletesAtSeconds)
        {
            const WeaponDefinition* const weapon = registeredWeapon;
            const std::uint32_t missing = weapon->magazineCapacity - combatant.magazineRounds;
            const std::uint32_t transferred = std::min(missing, combatant.reserveRounds);
            combatant.magazineRounds += transferred;
            combatant.reserveRounds -= transferred;
            combatant.reloadCompletesAtSeconds = 0.0;
        }
    }

    for (const CombatCommand& command : commands)
    {
        if (!entities::IsValidStableId(command.commandId) ||
            !entities::IsValidStableId(command.actorEntityId) ||
            (command.action == CombatAction::Fire &&
                !entities::IsValidStableId(command.targetEntityId)) ||
            (command.action == CombatAction::Reload && !command.targetEntityId.empty()) ||
            (command.action != CombatAction::Fire && command.action != CombatAction::Reload))
        {
            error = "Combat command contains an invalid ID or action.";
            return false;
        }

        if (candidate.processedCommandIds.contains(command.commandId))
        {
            results.push_back({command.commandId, CombatOutcome::Duplicate, 0.0F, 0.0F});
            continue;
        }
        if (candidate.processedCommandIds.size() >= MaximumProcessedCommands)
        {
            error = "Combat replay guard reached its configured capacity.";
            return false;
        }
        candidate.processedCommandIds.insert(command.commandId);

        auto actorCombatantIt = candidate.combatants.find(command.actorEntityId);
        const entities::EntityState* const actorEntity = entityRegistry.Find(command.actorEntityId);
        if (actorCombatantIt == candidate.combatants.end() || actorEntity == nullptr ||
            !actorEntity->active || actorCombatantIt->second.health <= 0.0F)
        {
            results.push_back({command.commandId, CombatOutcome::Ignored, 0.0F, 0.0F});
            continue;
        }

        CombatantState& actor = actorCombatantIt->second;
        const WeaponDefinition* const weapon = FindWeaponDefinition(actor.weaponId);
        if (command.action == CombatAction::Reload)
        {
            if (actor.reloadCompletesAtSeconds > candidate.elapsedSeconds)
                results.push_back({command.commandId, CombatOutcome::Reloading, 0.0F, 0.0F});
            else if (actor.magazineRounds >= weapon->magazineCapacity)
                results.push_back({command.commandId, CombatOutcome::MagazineFull, 0.0F, 0.0F});
            else if (actor.reserveRounds == 0U)
                results.push_back({command.commandId, CombatOutcome::NoReserveAmmo, 0.0F, 0.0F});
            else
            {
                if (candidate.elapsedSeconds > std::numeric_limits<double>::max() - weapon->reloadSeconds)
                {
                    error = "Combat reload completion time is not representable.";
                    return false;
                }
                actor.reloadCompletesAtSeconds = candidate.elapsedSeconds + weapon->reloadSeconds;
                results.push_back({command.commandId, CombatOutcome::ReloadStarted, 0.0F, 0.0F});
            }
            continue;
        }

        const auto targetCombatantIt = candidate.combatants.find(command.targetEntityId);
        const entities::EntityState* const targetEntity = entityRegistry.Find(command.targetEntityId);
        if (targetCombatantIt == candidate.combatants.end() || targetEntity == nullptr ||
            !targetEntity->active || targetCombatantIt->second.health <= 0.0F)
        {
            results.push_back({command.commandId, CombatOutcome::Ignored, 0.0F, 0.0F});
            continue;
        }
        if (actorEntity->factionId == targetEntity->factionId)
        {
            results.push_back({command.commandId, CombatOutcome::FriendlyTarget, 0.0F,
                targetCombatantIt->second.health});
            continue;
        }
        const double distance = DistanceBetween(*actorEntity, *targetEntity);
        if (!std::isfinite(distance) || distance > weapon->effectiveRangeMeters)
        {
            results.push_back({command.commandId, CombatOutcome::OutOfRange, 0.0F,
                targetCombatantIt->second.health});
            continue;
        }
        if (actor.reloadCompletesAtSeconds > candidate.elapsedSeconds)
        {
            results.push_back({command.commandId, CombatOutcome::Reloading, 0.0F,
                targetCombatantIt->second.health});
            continue;
        }
        if (actor.magazineRounds == 0U)
        {
            results.push_back({command.commandId, CombatOutcome::EmptyMagazine, 0.0F,
                targetCombatantIt->second.health});
            continue;
        }
        if (candidate.elapsedSeconds < actor.nextShotAtSeconds)
        {
            results.push_back({command.commandId, CombatOutcome::Cooldown, 0.0F,
                targetCombatantIt->second.health});
            continue;
        }

        --actor.magazineRounds;
        if (candidate.elapsedSeconds > std::numeric_limits<double>::max() - weapon->secondsBetweenShots)
        {
            error = "Combat fire cooldown time is not representable.";
            return false;
        }
        actor.nextShotAtSeconds = candidate.elapsedSeconds + weapon->secondsBetweenShots;
        CombatantState& target = targetCombatantIt->second;
        const float damage = std::min(weapon->damage, target.health);
        target.health -= damage;
        results.push_back({command.commandId,
            target.health <= 0.0F ? CombatOutcome::TargetDefeated : CombatOutcome::Fired,
            damage, target.health});
    }

    state = std::move(candidate);
    output = std::move(results);
    error.clear();
    return true;
}
} // namespace talvisota::gameplay
