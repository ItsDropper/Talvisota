#include "talvisota/vegetation/Mesh.hpp"
#include "talvisota/vegetation/Random.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace talvisota::vegetation
{
namespace
{
constexpr float kTau = 6.2831853071795864769F;

[[nodiscard]] Float3 SafePerpendicular(const Float3& tangent) noexcept
{
    const Float3 reference = std::abs(Dot(tangent, Float3{0.0F, 1.0F, 0.0F})) > 0.92F
        ? Float3{1.0F, 0.0F, 0.0F} : Float3{0.0F, 1.0F, 0.0F};
    return Normalized(Cross(tangent, reference));
}

[[nodiscard]] float BarkRelief(const std::uint64_t seed, const int segment,
    const int side) noexcept
{
    const float broad = HashUnit01(seed, static_cast<std::uint64_t>(segment) * 31u + 7u);
    const float fine = HashUnit01(seed, static_cast<std::uint64_t>(segment) * 131u +
        static_cast<std::uint64_t>(side) * 17u + 19u);
    return (broad - 0.5F) * 0.018F + (fine - 0.5F) * 0.026F;
}
}

TreeMesh BuildTreeMesh(const TreeSkeleton& skeleton, const std::uint64_t seed,
    const int requestedRadialSegments)
{
    TreeMesh mesh;
    const int radialSegments = std::clamp(requestedRadialSegments, 5, 16);
    mesh.vertices.reserve(skeleton.segments.size() *
        static_cast<std::size_t>(radialSegments + 1) * 2u);
    mesh.indices.reserve(skeleton.segments.size() *
        static_cast<std::size_t>(radialSegments) * 6u);

    // Emit complete material ranges in batches. Skeleton generation interleaves
    // primary limbs and their shoots; emitting in source order would fragment
    // material ranges into hundreds of tiny draw sections.
    std::vector<std::int32_t> materialGroups;
    materialGroups.reserve(skeleton.segments.size());
    for (const auto& segment : skeleton.segments)
        if (segment.startRadius > 0.0F && segment.endRadius > 0.0F &&
            segment.endRadius <= segment.startRadius)
            materialGroups.push_back(segment.materialGroup);
    std::sort(materialGroups.begin(), materialGroups.end());
    materialGroups.erase(std::unique(materialGroups.begin(), materialGroups.end()),
        materialGroups.end());

    for (const std::int32_t materialGroup : materialGroups)
    {
        MeshSection section{materialGroup, static_cast<std::uint32_t>(mesh.indices.size()), 0u};
        for (std::size_t segmentIndex = 0; segmentIndex < skeleton.segments.size(); ++segmentIndex)
        {
            const SkeletonSegment& segment = skeleton.segments[segmentIndex];
            if (segment.materialGroup != materialGroup) continue;
            const Float3 axis = segment.end - segment.start;
            const float length = Length(axis);
            if (length <= 1.0e-5F) continue;

            const Float3 tangent = axis * (1.0F / length);
            const Float3 frameA = SafePerpendicular(tangent);
            const Float3 frameB = Normalized(Cross(tangent, frameA));
            const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());

            for (int ring = 0; ring < 2; ++ring)
            {
                const float along = static_cast<float>(ring);
                const Float3 center = ring == 0 ? segment.start : segment.end;
                const float radius = ring == 0 ? segment.startRadius : segment.endRadius;
                for (int side = 0; side <= radialSegments; ++side)
                {
                    const float angle = kTau * static_cast<float>(side) /
                        static_cast<float>(radialSegments);
                    const float relief = BarkRelief(seed, static_cast<int>(segmentIndex), side);
                    const float displacedRadius = radius * (1.0F + relief);
                    const Float3 radial = Normalized(frameA * std::cos(angle) +
                        frameB * std::sin(angle));
                    const Float3 position = center + radial * displacedRadius;
                    mesh.vertices.push_back({position, radial,
                        static_cast<float>(side) / static_cast<float>(radialSegments),
                        (static_cast<float>(segment.generation) + along) * length * 1.8F});
                }
            }

            const std::uint32_t ringStride = static_cast<std::uint32_t>(radialSegments + 1);
            for (int side = 0; side < radialSegments; ++side)
            {
                const std::uint32_t a = base + static_cast<std::uint32_t>(side);
                const std::uint32_t b = a + 1u;
                const std::uint32_t c = a + ringStride;
                const std::uint32_t d = c + 1u;
                mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
                section.indexCount += 6u;
            }
        }
        if (section.indexCount > 0u)
            mesh.sections.push_back(section);
    }
    return mesh;
}
TreeMesh BuildSnowMesh(const TreeSkeleton& skeleton)
{
    TreeMesh mesh;
    constexpr int crossSectionSteps = 4;
    constexpr int verticesPerRing = crossSectionSteps + 1;
    mesh.vertices.reserve(skeleton.segments.size() * verticesPerRing * 2u);
    mesh.indices.reserve(skeleton.segments.size() * crossSectionSteps * 6u);

    MeshSection snowSection{kSnowMeshMaterialGroup, 0u, 0u};
    for (const SkeletonChain& chain : skeleton.chains)
    {
        if ((chain.bearingKind != 1 && chain.bearingKind != 2) ||
            chain.snowCapture.fitness < 0.08F ||
            chain.snowCapture.carryingRadius <= 0.0F ||
            chain.firstSegment < 0 || chain.segmentCount <= 0 ||
            static_cast<std::size_t>(chain.firstSegment + chain.segmentCount) >
                skeleton.segments.size())
            continue;

        const float fitness = Clamp01(chain.snowCapture.fitness);
        const float width = chain.snowCapture.carryingRadius * (0.72F + 0.52F * fitness);
        const float thickness = chain.snowCapture.carryingRadius *
            (0.18F + 0.30F * fitness) + 0.0025F;
        for (int offset = 0; offset < chain.segmentCount; ++offset)
        {
            const auto& segment = skeleton.segments[
                static_cast<std::size_t>(chain.firstSegment + offset)];
            const Float3 axis = segment.end - segment.start;
            const float length = Length(axis);
            if (length <= 1.0e-5F) continue;
            const Float3 tangent = axis * (1.0F / length);
            Float3 up = Float3{0.0F, 1.0F, 0.0F} -
                tangent * Dot(Float3{0.0F, 1.0F, 0.0F}, tangent);
            if (Length(up) <= 1.0e-4F) up = SafePerpendicular(tangent);
            else up = Normalized(up);
            const Float3 side = Normalized(Cross(tangent, up));
            const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());

            for (int ring = 0; ring < 2; ++ring)
            {
                const Float3 center = ring == 0 ? segment.start : segment.end;
                const float branchRadius = ring == 0 ? segment.startRadius : segment.endRadius;
                for (int sample = 0; sample <= crossSectionSteps; ++sample)
                {
                    const float angle = -1.57079632679F +
                        3.14159265359F * static_cast<float>(sample) /
                        static_cast<float>(crossSectionSteps);
                    const float across = std::sin(angle);
                    const float crown = std::cos(angle);
                    const Float3 normal = Normalized(up * crown + side * across);
                    const Float3 position = center + up * (branchRadius + thickness * crown) +
                        side * (width * across);
                    mesh.vertices.push_back({position, normal,
                        static_cast<float>(sample) / static_cast<float>(crossSectionSteps),
                        (static_cast<float>(segment.generation) + static_cast<float>(ring)) *
                            length * 1.8F});
                }
            }

            constexpr std::uint32_t ringStride = verticesPerRing;
            for (int sample = 0; sample < crossSectionSteps; ++sample)
            {
                const std::uint32_t a = base + static_cast<std::uint32_t>(sample);
                const std::uint32_t b = a + 1u;
                const std::uint32_t c = a + ringStride;
                const std::uint32_t d = c + 1u;
                mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
                snowSection.indexCount += 6u;
            }
        }
    }
    if (snowSection.indexCount > 0u)
        mesh.sections.push_back(snowSection);
    return mesh;
}

} // namespace talvisota::vegetation
