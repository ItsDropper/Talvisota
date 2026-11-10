#pragma once

#include "talvisota/presentation/SceneRenderer.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace talvisota::presentation
{
struct DynamicWorldObject final
{
    std::string id;
    talvisota::world::PlacementCategory category = talvisota::world::PlacementCategory::Prop;
    std::array<float, 16> authoredTransform{};
    std::array<float, 3> position{};
    std::array<float, 3> velocity{};
    std::array<float, 3> angularVelocity{};
    std::array<float, 3> rotation{};
    float integrity = 1.0F;
    float mass = 1.0F;
    float sleepSeconds = 0.0F;
    bool anchored = true;
    bool sleeping = true;
    std::vector<std::size_t> instanceIndices;
};

class DynamicWorldSystem final
{
public:
    [[nodiscard]] bool Initialize(const talvisota::world::WorldRegion& region,
        SceneSnapshot& scene, std::string& error);
    [[nodiscard]] bool ApplyImpulse(std::string_view placementId,
        const std::array<float, 3>& impulse,
        const std::array<float, 3>& torque, std::string& error);
    [[nodiscard]] bool ApplyDamage(std::string_view placementId, float damage,
        std::string& error);
    [[nodiscard]] bool Update(const talvisota::world::WorldRegion& region,
        SceneSnapshot& scene, float deltaSeconds, double elapsedSeconds,
        const std::array<float, 2>& windDirection, float windStrength,
        std::string& error);
    [[nodiscard]] bool Save(const std::filesystem::path& path, std::string& error) const;
    [[nodiscard]] bool Load(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] const std::vector<DynamicWorldObject>& Objects() const noexcept { return objects_; }
    [[nodiscard]] bool IsInitialized() const noexcept { return !objects_.empty(); }

private:
    std::vector<DynamicWorldObject> objects_;
};
} // namespace talvisota::presentation
