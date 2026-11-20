#pragma once

// Internal shared helpers for the species generators. Not part of the public
// vegetation API; only the three species generators include this.

#include "talvisota/vegetation/Skeleton.hpp"

#include <cmath>

namespace talvisota::vegetation::detail
{
[[nodiscard]] inline float Lerp(const float a, const float b, const float t) noexcept
{
    return a + (b - a) * Clamp01(t);
}

[[nodiscard]] inline Float3 DirectionFromAzimuthIncline(const float azimuth,
    const float inclineRad) noexcept
{
    const float cosine = std::cos(inclineRad);
    return Normalized(Float3{
        std::cos(azimuth) * cosine,
        std::sin(inclineRad),
        std::sin(azimuth) * cosine });
}

// Snow fitness for a bearing chain: conifers catch snow on the convex upper
// surface of horizontal shoots; lower crowns accumulate more before wind
// strips them, upper crowns hold the coarse plume shaping the silhouette.
[[nodiscard]] inline SnowCapture SnowCaptureFor(const float crownRatio, const float branchRadius,
    const float lowBias, const float directionBias, const float jitter01) noexcept
{
    SnowCapture capture;
    capture.crownRatio = Clamp01(crownRatio);
    capture.fitness = std::clamp(lowBias + (1.0F - crownRatio) * 0.45F + 0.20F * jitter01 +
        0.16F * capture.crownRatio, 0.04F, 1.0F);
    capture.directionality = std::clamp(directionBias + 0.34F * jitter01, 0.0F, 1.0F);
    capture.carryingRadius = std::max(0.006F, branchRadius);
    return capture;
}

// Wind reference direction shared with the renderer's fallback drift vector
// (geometry lean and snow drift shading stay consistent without environment
// data at generation time).
[[nodiscard]] inline Float3 WindDirectionXZ() noexcept
{
    return Normalized({ 0.914F, 0.0F, 0.406F });
}
} // namespace talvisota::vegetation::detail
