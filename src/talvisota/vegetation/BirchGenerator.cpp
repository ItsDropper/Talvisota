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

TreeSkeleton GenerateBirch(const SpeciesBuild& build) noexcept
{
    TreeSkeleton tree;
    const std::uint64_t seed = Mix64(build.seed ^ 0x426574756C615065ull);
    RandomSequence rng(seed);
    const float height = std::clamp(build.height, 8.0F, 23.0F);
    const float h = Clamp01((height - 8.0F) / 15.0F);
    const float dbh = (0.12F + 0.25F * std::pow(h, 1.2F)) *
        (0.88F + 0.24F * rng.Uniform01());
    TrunkBuild trunk{};
    trunk.height = height;
    trunk.buttRadius = dbh * 0.62F;
    trunk.breastRadius = dbh * 0.5F;
    trunk.tipRadius = std::max(0.012F, dbh * 0.055F);
    trunk.taperPower = 1.55F;
    trunk.noiseScale = 0.026F;
    trunk.bendRecovery = 0.66F;
    trunk.straightness = 0.42F;
    trunk.windLean = Clamp01(build.windLean);
    trunk.seed = Mix64(seed + 29);
    const auto trunkFirst = BuildTrunkChain(tree, trunk);
    const int trunkCount = tree.chains.front().segmentCount;

    // Silver birch has an irregular, light crown and a fine winter silhouette:
    // scaffold limbs divide repeatedly into slender, often pendulous twigs.
    const float crownBase = height * (0.34F + 0.12F * rng.Uniform01());
    const float crownLength = height - crownBase;
    const float crownRadius = height * (0.19F + 0.055F * rng.Uniform01()) *
        std::clamp(build.branchDiversity, 0.65F, 1.5F);
    const float asymmetry = Clamp01(build.asymmetry);
    const int tiers = std::clamp(static_cast<int>(crownLength / 0.85F), 7, 15);

    for (int tier = 0; tier < tiers; ++tier)
    {
        const float t = static_cast<float>(tier) / static_cast<float>(tiers - 1);
        const float crownRatio = t;
        const float y = crownBase + crownLength * t;
        const auto anchor = ChainSite(tree, trunkFirst, trunkCount, y / height);
        const int count = 3 + rng.UniformInt(0, 2);
        const float phase = tier * 2.39996323F + rng.Signed01() * 0.6F;
        for (int b = 0; b < count; ++b)
        {
            const float azimuth = phase + 2.0F * kPi * static_cast<float>(b) /
                static_cast<float>(count) + rng.Signed01() * 0.7F +
                asymmetry * rng.Signed01() * 0.9F;
            const float widthProfile = 0.58F + 0.42F * std::sin(kPi *
                std::pow(t, 0.78F));
            const float length = crownRadius * widthProfile *
                (0.62F + 0.65F * rng.Uniform01());
            const float incline = 28.0F - 38.0F * t + rng.Signed01() * 18.0F;
            const float root = std::max(0.009F, anchor.radius *
                (0.075F + 0.045F * rng.Uniform01()));
            ChainBuild scaffold{};
            scaffold.origin = anchor.position;
            scaffold.direction = DirectionFromAzimuthIncline(azimuth, incline * kPi / 180.0F);
            scaffold.startRadius = root;
            scaffold.endRadius = root * 0.10F;
            scaffold.length = length;
            scaffold.stepCount = std::max(4, static_cast<int>(std::ceil(length / 0.38F)));
            scaffold.droop = 0.055F + 0.10F * (1.0F - t);
            scaffold.order = 1;
            scaffold.materialGroup = 0;
            scaffold.noiseScale = 0.48F;
            scaffold.taperPower = 0.78F;
            scaffold.bearingKind = 0;
            const auto first = BuildChain(tree, Mix64(seed + tier * 1237u + b * 65537u),
                trunkFirst, scaffold);
            if (first < 0) continue;

            const int secondaries = std::max(3, scaffold.stepCount / 2);
            for (int s = 0; s < secondaries; ++s)
            {
                const float u = 0.22F + 0.70F * static_cast<float>(s) /
                    static_cast<float>(secondaries);
                const auto site = ChainSite(tree, first, scaffold.stepCount, u);
                const float side = (s & 1) ? -1.0F : 1.0F;
                const float twigAzimuth = azimuth + side * (0.42F + rng.Uniform01() * 0.9F);
                ChainBuild secondary{};
                secondary.origin = site.position;
                secondary.direction = DirectionFromAzimuthIncline(twigAzimuth,
                    (-18.0F + rng.Signed01() * 30.0F) * kPi / 180.0F);
                secondary.startRadius = std::max(0.0035F, site.radius * 0.38F);
                secondary.endRadius = secondary.startRadius * 0.12F;
                secondary.length = length * (0.19F + 0.17F * rng.Uniform01());
                secondary.stepCount = 3;
                secondary.droop = 0.12F + 0.12F * rng.Uniform01();
                secondary.order = 2;
                secondary.materialGroup = 2;
                secondary.noiseScale = 0.62F;
                secondary.taperPower = 0.82F;
                const auto secondaryFirst = BuildChain(tree,
                    Mix64(seed + tier * 7919u + b * 313u + s * 43u),
                    first + scaffold.stepCount - 1, secondary);
                if (secondaryFirst < 0) continue;

                // Fine, leafless terminal branchlets create the characteristic
                // high-frequency winter silhouette without leaf-card clutter.
                for (int twig = 0; twig < 2; ++twig)
                {
                    const auto tip = ChainSite(tree, secondaryFirst,
                        secondary.stepCount, twig == 0 ? 0.52F : 0.88F);
                    ChainBuild terminal{};
                    terminal.origin = tip.position;
                    terminal.direction = DirectionFromAzimuthIncline(
                        twigAzimuth + (twig == 0 ? -0.65F : 0.65F),
                        (-22.0F + rng.Signed01() * 24.0F) * kPi / 180.0F);
                    terminal.startRadius = std::max(0.0018F, tip.radius * 0.46F);
                    terminal.endRadius = terminal.startRadius * 0.08F;
                    terminal.length = 0.12F + 0.18F * rng.Uniform01();
                    terminal.stepCount = 2;
                    terminal.droop = 0.18F;
                    terminal.order = 3;
                    terminal.materialGroup = 2;
                    terminal.noiseScale = 0.75F;
                    terminal.taperPower = 0.9F;
                    BuildChain(tree, Mix64(seed + tier * 101u + b * 17u + s * 7u + twig),
                        secondaryFirst + secondary.stepCount - 1, terminal);
                }
            }
        }
    }

    tree.height = height;
    tree.trunkRadius = trunk.breastRadius;
    return tree;
}
} // namespace talvisota::vegetation::detail
