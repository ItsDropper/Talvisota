#include "talvisota/entities/EntityRegistry.hpp"

#include <cmath>
#include <utility>

namespace talvisota::entities
{
namespace
{
bool IsFinitePosition(const std::array<float, 3>& position) noexcept
{
    return std::isfinite(position[0]) && std::isfinite(position[1]) &&
        std::isfinite(position[2]);
}

bool IsValidEntityKind(const EntityKind kind) noexcept
{
    switch (kind)
    {
    case EntityKind::Player:
    case EntityKind::Infantry:
    case EntityKind::Civilian:
    case EntityKind::Vehicle:
    case EntityKind::WorldObject:
        return true;
    }
    return false;
}
} // namespace

bool IsValidStableId(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > 128U) return false;
    for (const unsigned char character : value)
    {
        const bool alphaNumeric = (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9');
        if (!alphaNumeric && character != '.' && character != '_' &&
            character != '-' && character != ':')
            return false;
    }
    return true;
}

bool EntityRegistry::Spawn(const EntityDefinition& definition, std::string& error)
{
    if (!IsValidStableId(definition.id) || !IsValidStableId(definition.factionId) ||
        (!definition.interactionTargetId.empty() &&
            !IsValidStableId(definition.interactionTargetId)) ||
        !IsValidEntityKind(definition.kind) || !IsFinitePosition(definition.position))
    {
        error = "Entity definition contains an invalid ID, kind, or non-finite position.";
        return false;
    }
    if (entities_.contains(definition.id))
    {
        error = "Entity ID is already registered: " + definition.id;
        return false;
    }
    if (!definition.interactionTargetId.empty())
    {
        for (const auto& entry : entities_)
        {
            if (entry.second.interactionTargetId == definition.interactionTargetId)
            {
                error = "Interaction target ID is already registered: " +
                    definition.interactionTargetId;
                return false;
            }
        }
    }

    EntityState candidate;
    candidate.id = definition.id;
    candidate.factionId = definition.factionId;
    candidate.kind = definition.kind;
    candidate.position = definition.position;
    candidate.interactionTargetId = definition.interactionTargetId;
    entities_.emplace(definition.id, std::move(candidate));
    error.clear();
    return true;
}

bool EntityRegistry::Despawn(const std::string_view entityId, std::string& error)
{
    const auto found = entities_.find(entityId);
    if (found == entities_.end())
    {
        error = "Cannot despawn an entity that is not registered: " + std::string(entityId);
        return false;
    }
    entities_.erase(found);
    error.clear();
    return true;
}

bool EntityRegistry::SetPosition(const std::string_view entityId,
    const std::array<float, 3>& position, std::string& error)
{
    if (!IsFinitePosition(position))
    {
        error = "Entity position must contain only finite coordinates.";
        return false;
    }
    EntityState* const entity = Find(entityId);
    if (entity == nullptr)
    {
        error = "Cannot move an entity that is not registered: " + std::string(entityId);
        return false;
    }
    entity->position = position;
    error.clear();
    return true;
}

bool EntityRegistry::SetActive(const std::string_view entityId, const bool active,
    std::string& error)
{
    EntityState* const entity = Find(entityId);
    if (entity == nullptr)
    {
        error = "Cannot change the activity of an entity that is not registered: " +
            std::string(entityId);
        return false;
    }
    entity->active = active;
    error.clear();
    return true;
}

const EntityState* EntityRegistry::Find(const std::string_view entityId) const noexcept
{
    const auto found = entities_.find(entityId);
    return found == entities_.end() ? nullptr : &found->second;
}

EntityState* EntityRegistry::Find(const std::string_view entityId) noexcept
{
    const auto found = entities_.find(entityId);
    return found == entities_.end() ? nullptr : &found->second;
}
} // namespace talvisota::entities
