#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace talvisota::vegetation
{
using Float3 = std::array<float, 3>;

inline Float3 operator+(const Float3& a, const Float3& b) noexcept
{
    return { a[0] + b[0], a[1] + b[1], a[2] + b[2] };
}
inline Float3 operator-(const Float3& a, const Float3& b) noexcept
{
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}
inline Float3 operator*(const Float3& a, const float s) noexcept
{
    return { a[0] * s, a[1] * s, a[2] * s };
}
[[nodiscard]] inline float Dot(const Float3& a, const Float3& b) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
[[nodiscard]] inline Float3 Cross(const Float3& a, const Float3& b) noexcept
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
[[nodiscard]] inline float Length(const Float3& value) noexcept
{
    return std::sqrt(std::max(0.0F, Dot(value, value)));
}
[[nodiscard]] inline Float3 Normalized(const Float3& value) noexcept
{
    const float length = Length(value);
    if (length <= 1.0e-8F) return { 0.0F, 1.0F, 0.0F };
    return { value[0] / length, value[1] / length, value[2] / length };
}
[[nodiscard]] inline Float3 Lerp(const Float3& a, const Float3& b, const float t) noexcept
{
    return { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t };
}
[[nodiscard]] inline float Clamp01(const float value) noexcept
{
    return value < 0.0F ? 0.0F : (value > 1.0F ? 1.0F : value);
}

// Snow retention decision attributes for a shoot; evaluated when the branch
// chain is built so the snow shell generator can be orientation/exposure
// driven (branch angle, thickness, crown position, aspect) exactly as the
// winter materials require.
struct SnowCapture final
{
    float fitness = 0.0F;        // 0..1 accumulation suitability
    float crownRatio = 0.0F;     // 0 at crown base, 1 at crown top
    float directionality = 0.0F; // 0 isotropic, 1 fully wind/backside shielded
    float carryingRadius = 0.0F;
};

// One straight tube piece of the tree skeleton. Segments chain through their
// parent segment: a chain is stored contiguously so children can spawn from
// any segment without pointer chasing, and connectivity invariants stay
// machine-checkable.
struct SkeletonSegment final
{
    std::int32_t parentIndex = -1;         // -1 or index into segments (parent chain piece or trunk anchor)
    Float3 start = { 0.0F, 0.0F, 0.0F };
    Float3 end = { 0.0F, 0.0F, 0.0F };
    float startRadius = 0.0F;
    float endRadius = 0.0F;
    // Material group the swept tube emits into: 0 bark (trunk), 1 bark (branch
    // primary/secondary), 2 twigs, 3 bark lower regions, 4 needle fascicles, 5 snow.
    // Per-species material allocation happens in the mesher so submeshes stay
    // consolidated (few material groups => few draw instances per placement).
    std::int32_t materialGroup = 0;
    std::int32_t order = 0;                // 0 trunk, 1 primary, 2 secondary, 3 tertiary/twig
    std::int32_t chainStart = 0;           // chain bookkeeping for LOD pruning
    std::int32_t generation = 0;           // growth generation along the chain
    // True when this segment may spawn child chains (used by LOD generators).
    bool allowChildren = true;
    // Growth-site info (ring fraction along the parent where this attaches).
    float attachFraction = 0.0F;
};

struct SkeletonChain final
{
    std::int32_t parentSegment = -1;       // segment index this chain grows from
    std::int32_t firstSegment = 0;
    std::int32_t segmentCount = 0;
    std::int32_t order = 1;
    std::int32_t bearingKind = 0;          // 0 bare, 1 spruce shoots, 2 pine tufts
    float ageFactor = 1.0F;                // 1 = young/vigorous, 0 = old
    SnowCapture snowCapture{};             // snow-shell accumulation parameters
};

struct TreeSkeleton final
{
    std::vector<SkeletonSegment> segments;
    std::vector<SkeletonChain> chains;
    float height = 0.0F;                   // total tree height in metres
    float trunkRadius = 0.0F;              // radius at ground/breast height
    Float3 basePosition = { 0.0F, 0.0F, 0.0F };

    // Adds the segment and returns its index; helper for chain builders.
    [[nodiscard]] std::int32_t AddSegment(const SkeletonSegment& segment)
    {
        segments.push_back(segment);
        return static_cast<std::int32_t>(segments.size() - 1);
    }
};

// Grows one chain (the main axis of a branch) in small segments, returning the
// index of the first added segment. Each step bends the current direction:
// a species-specific constant straightening pull toward the branch's own
// up-quaternion, weight-driven droop and deterministic per-step noise.
struct ChainBuild final
{
    Float3 origin = { 0.0F, 0.0F, 0.0F };
    Float3 direction = { 0.0F, 1.0F, 0.0F };
    float startRadius = 0.05F;
    float length = 1.0F;
    int stepCount = 4;
    float endRadius = 0.01F;              // radius at the very tip
    float droop = 0.0F;                   // radians per metre of gravity sag
    std::int32_t order = 1;               // skeleton hierarchy order
    std::int32_t materialGroup = 1;
    float noiseScale = 0.35F;             // deterministic lateral wiggle amplitude
    float taperPower = 0.55F;             // how fast radius shrinks toward the tip
    bool endSegment = true;
    std::int32_t bearingKind = 0;         // 0 bare, species-specific bearer codes
    SnowCapture snowCapture{};            // shell accumulation decision data
};

[[nodiscard]] std::int32_t BuildChain(TreeSkeleton& skeleton, const std::uint64_t seed,
    const std::int32_t parentSegment, const ChainBuild& build);

// Recomputes the ring-phase-correct trunk chain shared by every species:
// vertical stems with butt-flare, mid-height bend recovery, deterministic
// bark-noise lean and species-specific straightness. Segments emit into
// materialGroup 0 (trunk bark) and one main trunk chain entry.
struct TrunkBuild final
{
    Float3 base = { 0.0F, 0.0F, 0.0F };
    float height = 16.0F;
    float buttRadius = 0.38F;              // ground radius (butt swell included)
    float breastRadius = 0.28F;            // radius at 1.3 m => DBH 0.56 m
    float tipRadius = 0.045F;
    float taperPower = 2.2F;               // conifer stems shed radius quickly
    float noiseScale = 0.02F;              // lateral bend amplitude, fraction of height
    float bendRecovery = 0.28F;            // height fraction where lean relaxes
    float straightness = 0.5F;             // 0 = strongly curved, 1 = straight leader
    float windLean = 0.0F;                  // normalized exposure-driven lean strength
    std::uint64_t seed = 0;
};

[[nodiscard]] std::int32_t BuildTrunkChain(TreeSkeleton& skeleton, const TrunkBuild& build);

// Radius along the physiological taper shared by every species. Public so the
// species generators can place branch roots with correct trunk radii.
[[nodiscard]] float TrunkRadiusAtHeight(const TrunkBuild& build, float height) noexcept;

// Ondes: for the parent surface ring fraction placement. Returns a world-space
// point at (fraction * parent length) along the parent and its local frame for
// relative child spawning.
struct GrowSite final
{
    Float3 position = { 0.0F, 0.0F, 0.0F };
    float radius = 0.03F;
    Float3 branchUp = { 0.0F, 1.0F, 0.0F };
    Float3 branchForward = { 1.0F, 0.0F, 0.0F };
};

// Site at fraction u (0..1) along a whole chain (firstSegment..count pieces).
// Used to plant secondary/tertiary chains on parents at arc-length positions.
[[nodiscard]] GrowSite ChainSite(const TreeSkeleton& skeleton, const std::int32_t firstSegment,
    const std::int32_t segmentCount, const float fraction) noexcept;

[[nodiscard]] GrowSite SegmentSite(const TreeSkeleton& skeleton, const std::int32_t segmentIndex,
    const float fraction) noexcept;
} // namespace talvisota::vegetation
