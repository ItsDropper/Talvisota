#pragma once

#include <array>

namespace talvisota::presentation
{
// Minimum projected bound-radius coverage for the four authored Scots pine LODs.
// The final zero threshold is required by SelectRenderLod as a complete fallback.
// Values are calibrated for the dense mature-pine mesh chain: full-detail geometry
// stays close to the camera while distant stands use silhouette-focused meshes.
inline constexpr std::array<float, 4> ScotsPineMinimumScreenCoverage{
    0.75F, 0.40F, 0.18F, 0.0F
};
} // namespace talvisota::presentation
