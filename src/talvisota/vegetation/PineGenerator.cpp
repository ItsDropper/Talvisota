#include "talvisota/vegetation/Species.hpp"
#include "talvisota/vegetation/SpeciesDetail.hpp"
#include "talvisota/vegetation/Random.hpp"

#include <algorithm>
#include <cmath>

namespace talvisota::vegetation::detail
{
namespace
{
constexpr float kPi = 3.14159265358979323846F;
}

TreeSkeleton GeneratePine(const SpeciesBuild& build) noexcept
{
    TreeSkeleton tree;
    const std::uint64_t seed = Mix64(build.seed ^ 0x50696E757353796Cull);
    RandomSequence rng(seed);
    const float height = std::clamp(build.height, 9.0F, 25.0F);
    const float h = Clamp01((height - 9.0F) / 16.0F);
    const float dbh = (0.17F + 0.31F * std::pow(h, 1.35F)) *
        (0.88F + 0.24F * rng.Uniform01());
    TrunkBuild trunk{};
    trunk.height = height;
    trunk.buttRadius = dbh * 0.66F;
    trunk.breastRadius = dbh * 0.5F;
    trunk.tipRadius = std::max(0.018F, dbh * 0.075F);
    trunk.taperPower = 1.75F;
    trunk.noiseScale = 0.018F;
    trunk.bendRecovery = 0.48F;
    trunk.straightness = 0.68F;
    trunk.windLean = Clamp01(build.windLean);
    trunk.seed = Mix64(seed + 17);
    const auto trunkFirst = BuildTrunkChain(tree, trunk);
    const int trunkCount = tree.chains.front().segmentCount;

    // Scots pine carries a high, relatively open crown. Old lower branches
    // self-prune; surviving whorls broaden toward the middle of the live crown.
    const float crownBase = height * (0.42F + 0.12F * rng.Uniform01());
    const float crownLength = height - crownBase;
    const float crownRadius = height * (0.16F + 0.045F * rng.Uniform01()) *
        std::clamp(build.branchDiversity, 0.65F, 1.45F);
    const int whorls = std::clamp(static_cast<int>(crownLength / 0.72F), 7, 18);

    for (int w = 0; w < whorls; ++w)
    {
        const float t = static_cast<float>(w) / static_cast<float>(whorls - 1);
        const float crownRatio = std::pow(t, 1.08F);
        const float y = crownBase + crownLength * t;
        const auto anchor = ChainSite(tree, trunkFirst, trunkCount, y / height);
        const float azimuthBase = static_cast<float>(w) * 2.39996323F +
            rng.Signed01() * 0.22F;
        const int count = 3 + rng.UniformInt(0, 2);
        for (int b = 0; b < count; ++b)
        {
            const float azimuth = azimuthBase + 2.0F * kPi * static_cast<float>(b) /
                static_cast<float>(count) + rng.Signed01() * 0.18F;
            const float middleBulge = std::sin(kPi * std::pow(t, 0.72F));
            const float length = crownRadius * (0.52F + 0.55F * middleBulge) *
                (0.78F + 0.38F * rng.Uniform01());
            const float incline = 5.0F + 28.0F * crownRatio + rng.Signed01() * 8.0F;
            const float root = std::max(0.012F, anchor.radius *
                (0.105F + 0.055F * rng.Uniform01()));
            ChainBuild branch{};
            branch.origin = anchor.position;
            branch.direction = DirectionFromAzimuthIncline(azimuth, incline * kPi / 180.0F);
            branch.startRadius = root;
            branch.endRadius = root * 0.11F;
            branch.length = length;
            branch.stepCount = std::max(4, static_cast<int>(std::ceil(length / 0.42F)));
            branch.droop = 0.025F + 0.055F * (1.0F - crownRatio);
            branch.order = 1;
            branch.materialGroup = 0;
            branch.noiseScale = 0.34F;
            branch.taperPower = 0.70F;
            branch.bearingKind = 2;
            branch.snowCapture = SnowCaptureFor(crownRatio, root, 0.18F, 0.42F,
                rng.Uniform01());
            const auto first = BuildChain(tree, Mix64(seed + w * 7919u + b * 104729u),
                trunkFirst, branch);
            if (first < 0) continue;

            // Pine needles are carried by short terminal shoot clusters rather
            // than a continuous spruce-like spray mantle.
            const int tufts = std::max(3, branch.stepCount / 2);
            for (int n = 0; n < tufts; ++n)
            {
                const float u = 0.28F + 0.66F * static_cast<float>(n) /
                    static_cast<float>(tufts);
                const auto site = ChainSite(tree, first, branch.stepCount, u);
                const float side = (n & 1) ? -1.0F : 1.0F;
                const float angle = azimuth + side * (0.55F + rng.Uniform01() * 0.8F);
                ChainBuild shoot{};
                shoot.origin = site.position;
                shoot.direction = DirectionFromAzimuthIncline(angle,
                    (8.0F + rng.Uniform01() * 24.0F) * kPi / 180.0F);
                shoot.startRadius = std::max(0.0045F, site.radius * 0.38F);
                shoot.endRadius = shoot.startRadius * 0.42F;
                shoot.length = 0.16F + 0.22F * rng.Uniform01();
                shoot.stepCount = 2;
                shoot.droop = 0.015F;
                shoot.order = 2;
                shoot.materialGroup = 4;
                shoot.noiseScale = 0.22F;
                shoot.taperPower = 0.8F;
                shoot.bearingKind = 2;
                shoot.snowCapture = SnowCaptureFor(crownRatio, shoot.startRadius,
                    0.12F, 0.46F, rng.Uniform01());
                BuildChain(tree, Mix64(seed + w * 1009u + b * 97u + n * 31u),
                    first + branch.stepCount - 1, shoot);
            }
        }
    }

    tree.height = height;
    tree.trunkRadius = trunk.breastRadius;
    return tree;
}
} // namespace talvisota::vegetation::detail
