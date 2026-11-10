#pragma once

#include "talvisota/presentation/SceneRenderer.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace talvisota::presentation
{
class ExplosionLightingSystem final
{
public:
    [[nodiscard]] bool Trigger(std::string_view eventId,
        const std::array<float, 3>& position, double startSeconds,
        float rangeMeters, float peakIntensity, float durationSeconds,
        std::string& error);
    [[nodiscard]] bool Update(SceneSnapshot& scene, double elapsedSeconds,
        std::string& error);
    void Clear() noexcept { m_events.clear(); }
    [[nodiscard]] std::size_t ActiveEventCount() const noexcept { return m_events.size(); }

private:
    struct Event final
    {
        std::string id;
        std::array<float, 3> position{};
        double startSeconds = 0.0;
        float rangeMeters = 0.0F;
        float peakIntensity = 0.0F;
        float durationSeconds = 0.0F;
    };
    std::vector<Event> m_events;
};
} // namespace talvisota::presentation
