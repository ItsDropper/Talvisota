#include "talvisota/vegetation/Species.hpp"

namespace talvisota::vegetation::detail
{
TreeSkeleton GenerateSpruce(const SpeciesBuild& build) noexcept;
TreeSkeleton GeneratePine(const SpeciesBuild& build) noexcept;
TreeSkeleton GenerateBirch(const SpeciesBuild& build) noexcept;
}

namespace talvisota::vegetation
{
TreeSkeleton GenerateTree(const Species species, const SpeciesBuild& build)
{
    switch (species)
    {
    case Species::Spruce: return detail::GenerateSpruce(build);
    case Species::Pine: return detail::GeneratePine(build);
    case Species::Birch: return detail::GenerateBirch(build);
    }
    return {};
}

TrunkMeasurements MeasureTrunk(const TreeSkeleton& skeleton) noexcept
{
    TrunkMeasurements result{};
    result.height = skeleton.height;
    result.breastRadius = skeleton.trunkRadius;
    if (skeleton.segments.empty()) return result;
    const SkeletonSegment* trunkBase = nullptr;
    const SkeletonSegment* trunkAtBreast = nullptr;
    const SkeletonSegment* trunkTip = nullptr;
    for (const auto& segment : skeleton.segments)
    {
        if (segment.order != 0) continue;
        if (trunkBase == nullptr) trunkBase = &segment;
        trunkTip = &segment;
        if (segment.start[1] <= 1.3F && segment.end[1] >= 1.3F)
        {
            const float t = (1.3F - segment.start[1]) /
                (segment.end[1] - segment.start[1] + 1.0e-6F);
            result.breastRadius = segment.startRadius +
                (segment.endRadius - segment.startRadius) * t;
            trunkAtBreast = &segment;
        }
    }
    if (trunkBase != nullptr) result.buttRadius = trunkBase->startRadius;
    if (trunkTip != nullptr) result.tipRadius = trunkTip->endRadius;
    (void)trunkAtBreast;
    return result;
}
} // namespace talvisota::vegetation
