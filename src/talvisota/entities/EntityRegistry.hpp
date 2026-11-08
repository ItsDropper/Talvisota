#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace talvisota::entities
{
enum class EntityKind
{
    Player,
    Infantry,
    Civilian,
    Vehicle,
    WorldObject
};

struct EntityDefinition final
{
    std::string id;
    // Empty only for unaligned world objects; actors and vehicles require a faction.
    std::string factionId;
    EntityKind kind = EntityKind::WorldObject;
    std::array<float, 3> position{};
    // Optional stable placement/interaction ID consumed by MissionRuntime.
    std::string interactionTargetId;
};

struct EntityState final
{
    std::string id;
    std::string factionId;
    EntityKind kind = EntityKind::WorldObject;
    std::array<float, 3> position{};
    std::string interactionTargetId;
    bool active = true;
};

[[nodiscard]] bool IsValidStableId(std::string_view value) noexcept;

// Owns game-side entity state. The ordered map makes iteration and test output
// deterministic; returned pointers remain stable until that entity is removed.
class EntityRegistry final
{
public:
    [[nodiscard]] bool Spawn(const EntityDefinition& definition, std::string& error);
    [[nodiscard]] bool Despawn(std::string_view entityId, std::string& error);
    [[nodiscard]] bool SetPosition(std::string_view entityId,
        const std::array<float, 3>& position, std::string& error);
    [[nodiscard]] bool SetActive(std::string_view entityId, bool active, std::string& error);

    [[nodiscard]] const EntityState* Find(std::string_view entityId) const noexcept;
    [[nodiscard]] EntityState* Find(std::string_view entityId) noexcept;
    [[nodiscard]] std::size_t Size() const noexcept { return entities_.size(); }
    [[nodiscard]] const std::map<std::string, EntityState, std::less<>>& All() const noexcept
    {
        return entities_;
    }

private:
    std::map<std::string, EntityState, std::less<>> entities_;
};
} // namespace talvisota::entities
