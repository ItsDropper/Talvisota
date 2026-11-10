#include "talvisota/presentation/ExplosionLighting.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace talvisota::presentation
{
namespace
{
[[nodiscard]] bool IsValidEventId(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > 128U)
        return false;
    for (const char rawCharacter : value)
    {
        const auto character = static_cast<unsigned char>(rawCharacter);
        const bool alphaNumeric = (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9');
        if (!alphaNumeric && character != '.' && character != '_' &&
            character != '-' && character != ':')
            return false;
    }
    return true;
}
} // namespace

bool ExplosionLightingSystem::Trigger(const std::string_view eventId,
    const std::array<float, 3>& position, const double startSeconds,
    const float rangeMeters, const float peakIntensity, const float durationSeconds,
    std::string& error)
{
    if (!IsValidEventId(eventId) || !std::isfinite(startSeconds) || startSeconds < 0.0 ||
        !std::isfinite(rangeMeters) || rangeMeters <= 0.0F || rangeMeters > 200.0F ||
        !std::isfinite(peakIntensity) || peakIntensity <= 0.0F || peakIntensity > 100000.0F ||
        !std::isfinite(durationSeconds) || durationSeconds < 0.05F || durationSeconds > 5.0F)
    {
        error = "Explosion light event contains an invalid ID, timing, range, or intensity.";
        return false;
    }
    if (!std::all_of(position.begin(), position.end(),
            [](const float value) { return std::isfinite(value); }))
    {
        error = "Explosion light position must contain finite world coordinates.";
        return false;
    }

    std::erase_if(m_events, [&](const Event& event) {
        return startSeconds - event.startSeconds > event.durationSeconds;
    });
    if (m_events.size() >= 8U ||
        std::any_of(m_events.begin(), m_events.end(), [&](const Event& event) {
            return event.id == eventId;
        }))
    {
        error = "Explosion light event IDs must be unique and no more than eight events may overlap.";
        return false;
    }

    m_events.push_back({std::string(eventId), position, startSeconds,
        rangeMeters, peakIntensity, durationSeconds});
    error.clear();
    return true;
}

bool ExplosionLightingSystem::Update(SceneSnapshot& scene, const double elapsedSeconds,
    std::string& error)
{
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0)
    {
        error = "Explosion lighting update requires a finite non-negative clock.";
        return false;
    }

    std::erase_if(m_events, [&](const Event& event) {
        return elapsedSeconds - event.startSeconds > event.durationSeconds;
    });
    std::vector<RenderPointLight> lights;
    lights.reserve(m_events.size());
    for (const Event& event : m_events)
    {
        const double age = elapsedSeconds - event.startSeconds;
        if (age < 0.0)
            continue;
        const float ageSeconds = static_cast<float>(age);
        const float attack = std::clamp(ageSeconds / 0.025F, 0.0F, 1.0F);
        const float decay = std::exp(-ageSeconds / 0.36F);
        const float pulse = attack * decay;
        if (pulse < 0.005F)
            continue;
        const float expansion = std::clamp(ageSeconds / 0.12F, 0.0F, 1.0F);
        lights.push_back({event.position,
            event.rangeMeters * (0.72F + 0.28F * expansion),
            {1.0F, 0.52F, 0.24F}, event.peakIntensity * pulse});
    }
    scene.pointLights = std::move(lights);
    error.clear();
    return true;
}
} // namespace talvisota::presentation
