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

    std::vector<std::int32_t> chainLengths(skeleton.segments.size(), 0);
    for (const SkeletonChain& chain : skeleton.chains)
        if (chain.firstSegment >= 0 &&
            static_cast<std::size_t>(chain.firstSegment) < chainLengths.size())
            chainLengths[static_cast<std::size_t>(chain.firstSegment)] = chain.segmentCount;

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
            const std::size_t chainStart = segment.chainStart >= 0
                ? static_cast<std::size_t>(segment.chainStart) : skeleton.segments.size();
            const int chainLength = chainStart < chainLengths.size() ? chainLengths[chainStart] : 0;
            const bool hasPrevious = segment.generation > 0 &&
                segmentIndex > chainStart;
            const bool hasNext = segment.generation + 1 < chainLength &&
                segmentIndex + 1u < skeleton.segments.size();
            Float3 startTangent = tangent;
            Float3 endTangent = tangent;
            if (hasPrevious)
            {
                const auto& previous = skeleton.segments[segmentIndex - 1u];
                startTangent = Normalized(Normalized(previous.end - previous.start) + tangent);
            }
            if (hasNext)
            {
                const auto& next = skeleton.segments[segmentIndex + 1u];
                endTangent = Normalized(tangent + Normalized(next.end - next.start));
            }
            const Float3 startFrameA = SafePerpendicular(startTangent);
            const Float3 startFrameB = Normalized(Cross(startTangent, startFrameA));
            const Float3 endFrameA = SafePerpendicular(endTangent);
            const Float3 endFrameB = Normalized(Cross(endTangent, endFrameA));
            const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());

            for (int ring = 0; ring < 2; ++ring)
            {
                const float along = static_cast<float>(ring);
                const Float3 center = ring == 0 ? segment.start : segment.end;
                const float radius = ring == 0 ? segment.startRadius : segment.endRadius;
                const Float3& frameA = ring == 0 ? startFrameA : endFrameA;
                const Float3& frameB = ring == 0 ? startFrameB : endFrameB;
                for (int side = 0; side <= radialSegments; ++side)
                {
                    const float angle = kTau * static_cast<float>(side) /
                        static_cast<float>(radialSegments);
                    const int boundaryIndex = segment.chainStart + segment.generation + ring;
                    const float relief = BarkRelief(seed, boundaryIndex, side % radialSegments);
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

TreeMesh BuildNeedleMesh(const TreeSkeleton& skeleton, const std::uint64_t seed)
{
    TreeMesh mesh;
    MeshSection section{4, 0u, 0u};
    const auto appendBlade = [&](const Float3& origin, const Float3& direction,
        const Float3& planeNormal, const float length, const float width) {
        const Float3 side = Normalized(Cross(direction, planeNormal));
        const Float3 tip = origin + direction * length;
        const Float3 left = origin - side * (width * 0.5F);
        const Float3 right = origin + side * (width * 0.5F);
        const Float3 normal = Normalized(Cross(right - left, tip - origin));
        const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({left, normal, 0.0F, 0.0F});
        mesh.vertices.push_back({right, normal, 1.0F, 0.0F});
        mesh.vertices.push_back({tip, normal, 0.5F, 1.0F});
        mesh.vertices.push_back({left, normal * -1.0F, 0.0F, 0.0F});
        mesh.vertices.push_back({right, normal * -1.0F, 1.0F, 0.0F});
        mesh.vertices.push_back({tip, normal * -1.0F, 0.5F, 1.0F});
        mesh.indices.insert(mesh.indices.end(),
            {base, base + 1u, base + 2u, base + 5u, base + 4u, base + 3u});
        section.indexCount += 6u;
    };

    for (std::size_t chainIndex = 0; chainIndex < skeleton.chains.size(); ++chainIndex)
    {
        const SkeletonChain& chain = skeleton.chains[chainIndex];
        if ((chain.bearingKind != 1 && chain.bearingKind != 2) ||
            chain.firstSegment < 0 || chain.segmentCount <= 0 ||
            static_cast<std::size_t>(chain.firstSegment + chain.segmentCount) >
                skeleton.segments.size())
            continue;

        for (int offset = 0; offset < chain.segmentCount; ++offset)
        {
            const std::size_t segmentIndex =
                static_cast<std::size_t>(chain.firstSegment + offset);
            const SkeletonSegment& segment = skeleton.segments[segmentIndex];
            const Float3 axis = segment.end - segment.start;
            const float segmentLength = Length(axis);
            if (segmentLength <= 0.035F) continue;
            const Float3 tangent = axis * (1.0F / segmentLength);
            const Float3 radialA = SafePerpendicular(tangent);
            const Float3 radialB = Normalized(Cross(tangent, radialA));
            const bool pine = chain.bearingKind == 2;
            const int clusterCount = pine
                ? std::clamp(static_cast<int>(std::ceil(segmentLength / 0.30F)), 1, 2)
                : std::clamp(static_cast<int>(std::ceil(segmentLength / 0.22F)), 1, 3);
            const int needlesPerCluster = pine ? 2 : 5;
            const float phase = HashUnit01(seed,
                static_cast<std::uint64_t>(chainIndex) * 65537u + segmentIndex * 17u) *
                kTau;

            for (int cluster = 0; cluster < clusterCount; ++cluster)
            {
                const float along = (static_cast<float>(cluster) + 0.5F) /
                    static_cast<float>(clusterCount);
                const Float3 center = Lerp(segment.start, segment.end, along);
                const float angle = phase + static_cast<float>(cluster) * 2.39996323F;
                const Float3 radial = Normalized(radialA * std::cos(angle) +
                    radialB * std::sin(angle));
                const float branchRadius = segment.startRadius +
                    (segment.endRadius - segment.startRadius) * along;
                const Float3 attachment = center + radial * (branchRadius * 0.78F);

                for (int needle = 0; needle < needlesPerCluster; ++needle)
                {
                    const float jitter = HashUnit01(seed,
                        static_cast<std::uint64_t>(chainIndex) * 104729u +
                        segmentIndex * 97u + static_cast<std::uint64_t>(cluster * 11 + needle));
                    Float3 direction;
                    if (pine)
                    {
                        const float pairSide = needle == 0 ? -1.0F : 1.0F;
                        direction = Normalized(radial * 0.72F + tangent * 0.48F +
                            radialB * (pairSide * 0.20F));
                    }
                    else
                    {
                        direction = Normalized(radial * 0.88F + tangent * 0.22F +
                            radialB * ((jitter - 0.5F) * 0.18F));
                    }
                    const float needleLength = pine
                        ? 0.040F + jitter * 0.018F
                        : 0.020F + jitter * 0.012F;
                    const float width = pine ? 0.0032F : 0.0019F;
                    appendBlade(attachment, direction, radialB,
                        needleLength, width);
                    appendBlade(attachment, direction,
                        Normalized(Cross(direction, radialB)),
                        needleLength, width);
                }
            }
        }
    }

    if (section.indexCount > 0u)
        mesh.sections.push_back(section);
    return mesh;
}

} // namespace talvisota::vegetation
