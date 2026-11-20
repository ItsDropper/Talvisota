#include "talvisota/vegetation/Skeleton.hpp"

#include "talvisota/vegetation/Random.hpp"

#include <algorithm>
#include <cmath>

namespace talvisota::vegetation
{
namespace
{
// Rotates `direction` toward a bend around a perpendicular axis; keeps chain
// curvature bounded and, more importantly, fully deterministic.
void BendDirection(Float3& direction, const Float3& axis, const float angle) noexcept
{
    if (std::abs(angle) < 1.0e-6F) return;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const Float3 rotated = direction * cosine +
        Cross(axis, direction) * sine +
        axis * (Dot(axis, direction) * (1.0F - cosine));
    direction = Normalized(rotated);
}

// Any unit vector perpendicular to `direction`, deterministic per seed.
[[nodiscard]] Float3 PerpendicularOne(const Float3& direction, const std::uint64_t seed) noexcept
{
    const Float3 reference = std::abs(Dot(direction, Float3{ 0.0F, 1.0F, 0.0F })) > 0.92F
        ? Float3{ 1.0F, 0.0F, 0.0F }
        : Float3{ 0.0F, 1.0F, 0.0F };
    return Normalized(Cross(direction, reference));
}

// Smooth bell-shaped exposure loading over [0, 1].
[[nodiscard]] float ExposureBell(const float unit) noexcept
{
    return 1.0F - std::pow(2.0F * Clamp01(unit) - 1.0F, 2.0F);
}
} // namespace

float TrunkRadiusAtHeight(const TrunkBuild& build, const float height) noexcept
{
    if (height <= 0.0F) return build.buttRadius;
    constexpr float breastHeight = 1.3F;
    if (height <= breastHeight)
    {
        const float t = height / breastHeight;
        return build.buttRadius + (build.breastRadius - build.buttRadius) * t;
    }
    const float span = std::max(0.001F, build.height - breastHeight);
    const float t = Clamp01((height - breastHeight) / span);
    return build.breastRadius + (build.tipRadius - build.breastRadius) * std::pow(t, build.taperPower);
}

std::int32_t BuildChain(TreeSkeleton& skeleton, const std::uint64_t seed,
    const std::int32_t parentSegment, const ChainBuild& build)
{
    if (build.stepCount < 1 || !(build.length > 1.0e-4F) ||
        !(build.startRadius > 0.0F) || !(build.endRadius > 0.0F) ||
        build.endRadius > build.startRadius)
        return -1;

    const int steps = build.stepCount;
    const float stepLength = build.length / static_cast<float>(steps);
    const Float3 downward = { 0.0F, -1.0F, 0.0F };
    const Float3 lateralA = PerpendicularOne(build.direction, seed);
    const Float3 lateralB = Normalized(Cross(build.direction, lateralA));

    Float3 position = build.origin;
    Float3 direction = Normalized(build.direction);
    float radiusTop = build.startRadius;

    const std::int32_t chainStart = static_cast<std::int32_t>(skeleton.segments.size());
    for (int step = 0; step < steps; ++step)
    {
        const float stepOffset = static_cast<float>(step) / static_cast<float>(steps);
        const float endOffset = static_cast<float>(step + 1) / static_cast<float>(steps);
        const float startRadius = radiusTop;
        const float endRadius = build.startRadius + (build.endRadius - build.startRadius) *
            std::pow(endOffset, build.taperPower);
        radiusTop = endRadius;

        // Deterministic curvature: Dfrost pull toward gravity plus smooth
        // noise wiggle across growth progress, giving branches a natural arc
        // without the uniform-circle look of pure sine loops.
        Float3 progressDirection = direction;
        const float sagAngle = build.droop * stepLength;
        if (sagAngle > 0.0F)
            BendDirection(progressDirection, downward, sagAngle);
        const float wiggleA = (FbmNoise(seed + 11, build.noiseScale * build.length * stepOffset + 0.5F) - 0.5F) *
            build.noiseScale;
        const float wiggleB = (FbmNoise(seed + 37, build.noiseScale * build.length * stepOffset + 9.5F) - 0.5F) *
            build.noiseScale;
        BendDirection(progressDirection, lateralA, wiggleA * stepLength);
        BendDirection(progressDirection, lateralB, wiggleB * stepLength);
        progressDirection = Normalized(progressDirection);

        const Float3 nextPosition = position + progressDirection * stepLength;
        SkeletonSegment segment{};
        segment.parentIndex = parentSegment;
        segment.start = position;
        segment.end = nextPosition;
        segment.startRadius = startRadius;
        segment.endRadius = endRadius;
        segment.materialGroup = build.materialGroup;
        segment.order = build.order;
        segment.chainStart = chainStart;
        segment.generation = step;
        segment.allowChildren = build.endSegment && step + 1 < steps;
        skeleton.segments.push_back(segment);
        position = nextPosition;
        direction = progressDirection;
    }

    SkeletonChain chain{};
    chain.parentSegment = parentSegment;
    chain.firstSegment = chainStart;
    chain.segmentCount = steps;
    chain.order = build.order;
    chain.bearingKind = build.bearingKind;
    chain.ageFactor = 1.0F;
    chain.snowCapture = build.snowCapture;
    skeleton.chains.push_back(chain);
    return chainStart;
}

GrowSite ChainSite(const TreeSkeleton& skeleton, const std::int32_t firstSegment,
    const std::int32_t segmentCount, const float fraction) noexcept
{
    GrowSite site;
    site.branchForward = { 1.0F, 0.0F, 0.0F };
    if (firstSegment < 0 || segmentCount < 1 ||
        firstSegment + segmentCount > static_cast<std::int32_t>(skeleton.segments.size()))
        return site;

    float total = 0.0F;
    for (int index = 0; index < segmentCount; ++index)
    {
        const auto& segment = skeleton.segments[static_cast<std::size_t>(firstSegment + index)];
        total += Length(segment.end - segment.start);
    }
    if (total <= 1.0e-6F) return site;

    float remaining = Clamp01(fraction) * total;
    for (int index = 0; index < segmentCount; ++index)
    {
        const auto& segment = skeleton.segments[static_cast<std::size_t>(firstSegment + index)];
        const float segmentLength = Length(segment.end - segment.start);
        if (remaining > segmentLength && index + 1 < segmentCount)
        {
            remaining -= segmentLength;
            continue;
        }
        return SegmentSite(skeleton, firstSegment + index, remaining / std::max(1.0e-6F, segmentLength));
    }
    return site;
}

GrowSite SegmentSite(const TreeSkeleton& skeleton, const std::int32_t segmentIndex,
    const float fraction) noexcept
{
    GrowSite site;
    site.branchForward = { 1.0F, 0.0F, 0.0F };
    if (segmentIndex < 0 || segmentIndex >= static_cast<std::int32_t>(skeleton.segments.size()))
        return site;
    const SkeletonSegment& segment = skeleton.segments[static_cast<std::size_t>(segmentIndex)];
    const Float3 direction = Normalized(segment.end - segment.start);
    const float clamped = Clamp01(fraction);
    site.position = Lerp(segment.start, segment.end, clamped);
    site.radius = segment.startRadius + (segment.endRadius - segment.startRadius) * clamped;
    // Perpendicular basis to the parent direction so child lateral spawn relies
    // on the branch axis rather than on global axes: the spread stays stable
    // across curve directions.
    const Float3 reference = std::abs(Dot(direction, Float3{ 0.0F, 1.0F, 0.0F })) > 0.92F
        ? Float3{ 1.0F, 0.0F, 0.0F }
        : Float3{ 0.0F, 1.0F, 0.0F };
    site.branchUp = Normalized(Cross(direction, reference));
    site.branchForward = direction;
    return site;
}

// Shared vertical-stem builder. Radius follows the physiological taper model
// (butt swell -> near-constant breast-height -> fast top taper). Lean is a
// wind/loading lean applied over the lower band, relaxing back toward
// vertical; the leader keeps the straight central axis conifers show.
std::int32_t BuildTrunkChain(TreeSkeleton& skeleton, const TrunkBuild& build)
{
    const int steps = std::max(8, static_cast<int>(std::ceil(build.height / 0.62F)));
    const float stepLength = build.height / static_cast<float>(steps);
    const std::uint64_t seed = Mix64(build.seed);

    // Deterministic wind direction for lean and snow exposure. Matches the
    // renderer's fallback wind vector so geometry lean and shading agree even
    // without environment data at generation time.
    const Float3 windXZ = Normalized({ 0.914F, 0.0F, 0.406F });
    const Float3 windAxis = Normalized(Cross(Float3{ 0.0F, 1.0F, 0.0F }, windXZ));
    const Float3 breezeAxis = Normalized(Cross(Float3{ 0.0F, 1.0F, 0.0F },
        std::abs(windXZ[0]) < 0.9F ? Float3{ 1.0F, 0.0F, 0.0F } : Float3{ 0.0F, 0.0F, 1.0F }));

    Float3 position = build.base;
    Float3 direction = Float3{ 0.0F, 1.0F, 0.0F };
    const std::int32_t chainStart = static_cast<std::int32_t>(skeleton.segments.size());
    for (int step = 0; step < steps; ++step)
    {
        const float stepStartHeight = static_cast<float>(step) * stepLength;
        const float stepEndHeight = static_cast<float>(step + 1) * stepLength;
        const float startRadius = TrunkRadiusAtHeight(build, stepStartHeight);
        const float endRadius = TrunkRadiusAtHeight(build, stepEndHeight);

        Float3 progressDirection = direction;
        // Wind lean grows through the lower band; above bendRecovery height
        // the stem realigns to vertical (dominant central leader behaviour).
        const float leanProgress = stepStartHeight /
            std::max(0.001F, build.height * build.bendRecovery);
        if (leanProgress < 1.0F)
        {
            const float technicalLean = ExposureBell(leanProgress * 0.5F) * build.windLean * 0.05F;
            const float structuralLean =
                (FbmNoise(seed + 91, 0.35F) - 0.5F) * (1.0F - build.straightness) * 0.085F;
            BendDirection(progressDirection, windAxis, technicalLean + structuralLean);
        }
        const float superiorNoise = (FbmNoise(seed + 31,
            0.22F * build.noiseScale * stepStartHeight + 1.1F) - 0.5F) * 2.0F;
        BendDirection(progressDirection, breezeAxis, superiorNoise * build.noiseScale * 0.35F);
        progressDirection = Normalized(progressDirection);
        direction = progressDirection;

        const Float3 nextPosition = position + progressDirection * stepLength;
        SkeletonSegment segment{};
        segment.parentIndex = step == 0 ? -1 : chainStart + step - 1;
        segment.start = position;
        segment.end = nextPosition;
        segment.startRadius = startRadius;
        segment.endRadius = endRadius;
        segment.materialGroup = 0;
        segment.order = 0;
        segment.chainStart = chainStart;
        segment.generation = step;
        segment.allowChildren = step + 1 < steps && stepStartHeight > 0.65F;
        skeleton.segments.push_back(segment);
        position = nextPosition;
    }

    SkeletonChain chain{};
    chain.parentSegment = -1;
    chain.firstSegment = chainStart;
    chain.segmentCount = steps;
    chain.order = 0;
    chain.bearingKind = 0;
    chain.ageFactor = 1.0F;
    skeleton.chains.push_back(chain);
    return chainStart;
}
} // namespace talvisota::vegetation
