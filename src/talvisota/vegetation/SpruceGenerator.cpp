#include "talvisota/vegetation/Species.hpp"

#include "talvisota/vegetation/SpeciesDetail.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace talvisota::vegetation::detail
{
// ---------------------------------------------------------------------------
// Norway spruce (Picea abies L. H. Karst.) architecture.
//
// Whorled branching: essentially one branch whorl per growing season along a
// strong, straight central leader, with 4..7 laterals per whorl (published
// Norway spruce whorl-lateral range; see docs/production/vegetation-references.md),
// each whorl rotating its azimuth by the golden angle so branch layers never
// align into planar fans. Lower whorl branches spread wide and sag gently with
// age; upper whorl branches ascend toward the pointed crown. Secondary shoots
// form flattened sprays stacked along each primary, shorter toward the tip;
// secondary radii shrink away from the primary so the whole chain keeps
// load-consistent thickness.
//
// The height/DBH envelope is anchored to published Finnish stand statistics
// recorded in the reference document; every other numerical value below is a
// documented artistic configuration parameter (not a measurement).
// ---------------------------------------------------------------------------

struct SpruceArchitecture final
{
    // Height/DBH envelope, mid-rotation dense stands.
    static constexpr float kMinHeight = 11.0F;
    static constexpr float kMaxHeight = 26.0F;
    static constexpr float kDbhAtMin = 0.16F;    // metres (breast-height diameter)
    static constexpr float kDbhAtMax = 0.50F;

    // Crown/whorl architecture.
    static constexpr float kCrownBaseMin = 0.08F;    // crown stem fraction
    static constexpr float kCrownBaseMax = 0.30F;
    static constexpr float kBranchRootMin = 0.085F;  // branch/trunk radius ratio
    static constexpr float kBranchRootMax = 0.150F;
    static constexpr float kBranchCountMin = 4;      // laterals per whorl (published 3..8)
    static constexpr float kBranchCountMax = 7;
    static constexpr float kSecondaryCountMin = 5;   // sprays per primary
    static constexpr float kSecondaryCountMax = 9;

    // Droop rates (radians per metre).
    static constexpr float kPrimaryDroopMin = 0.050F;
    static constexpr float kPrimaryDroopMax = 0.115F;
    static constexpr float kSecondaryDroopMin = 0.070F;
    static constexpr float kSecondaryDroopMax = 0.160F;

    // Crown width, fraction of height (mid-density stands).
    static constexpr float kCrownRadiusMin = 0.085F;
    static constexpr float kCrownRadiusMax = 0.150F;

    // Leader tip cluster: short ascending shoots capping the crown spike.
    static constexpr int kLeaderClusterMin = 4;
    static constexpr int kLeaderClusterMax = 7;
};

TreeSkeleton GenerateSpruce(const SpeciesBuild& build) noexcept
{
    TreeSkeleton skeleton;
    const std::uint64_t seed = Mix64(build.seed + 0x53507275C1ull);
    RandomSequence random{ Mix64(seed) };
    const std::uint64_t trunkSeed = Mix64(seed + 1);

    const float height = std::clamp(build.height, SpruceArchitecture::kMinHeight,
        SpruceArchitecture::kMaxHeight);
    const float heightRange = SpruceArchitecture::kMaxHeight - SpruceArchitecture::kMinHeight;
    const float heightNorm = Clamp01((height - SpruceArchitecture::kMinHeight) / heightRange);
    const float diversity = std::clamp(build.branchDiversity, 0.55F, 1.5F);

    // Breast-height diameter grows with height; endpoints anchored to the
    // published stand envelopes, jitter is documented artistic variation.
    const float dbh = (SpruceArchitecture::kDbhAtMin +
        (SpruceArchitecture::kDbhAtMax - SpruceArchitecture::kDbhAtMin) *
            std::pow(heightNorm, 1.55F)) * (0.86F + 0.28F * random.Uniform01()) * diversity;
    const float breastRadius = std::max(0.048F, dbh * 0.5F);
    const float buttRadius = breastRadius * (1.16F + 0.16F * random.Uniform01());
    const float tipRadius = std::max(0.018F, breastRadius * 0.075F);

    TrunkBuild trunk{};
    trunk.base = { 0.0F, 0.0F, 0.0F };
    trunk.height = height;
    trunk.buttRadius = buttRadius;
    trunk.breastRadius = breastRadius;
    trunk.tipRadius = tipRadius;
    trunk.taperPower = 2.1F;
    trunk.noiseScale = 0.014F * (1.0F + 2.0F * Clamp01(build.windLean));
    trunk.bendRecovery = 0.30F;
    trunk.windLean = Clamp01(build.windLean);
    // Spruce keeps a dominant straight central leader.
    trunk.straightness = 0.78F + 0.18F * random.Uniform01();
    trunk.seed = trunkSeed;
    const std::int32_t trunkChain = BuildTrunkChain(skeleton, trunk);
    if (trunkChain < 0) return skeleton;
    const int trunkSteps = skeleton.chains.empty() ? 0 : skeleton.chains.front().segmentCount;

    // Crown extent: dense conifer competition lifts the living crown; more
    // open growth keeps the lower whorls (dense-crown bias is competition).
    const float crownBase = height * Lerp(SpruceArchitecture::kCrownBaseMin,
        SpruceArchitecture::kCrownBaseMax,
        random.Uniform01() * 0.55F + Clamp01(build.asymmetry) * 0.25F);
    const float crownTop = height * 0.995F;
    const float crownLength = crownTop - crownBase;
    if (crownLength <= 1.2F) return skeleton;

    // Branch envelope: wide low whorls, shorter ascending top whorls.
    const float crownRadius = height * Lerp(
        SpruceArchitecture::kCrownRadiusMin, SpruceArchitecture::kCrownRadiusMax,
        Clamp01(0.62F - 0.40F * heightNorm + 0.15F * random.Uniform01())) * diversity;
    const int whorlCount = std::clamp(
        static_cast<int>(crownLength / Lerp(1.75F, 1.15F, random.Uniform01())), 6, 17);
    const int branchCountBase = static_cast<int>(
        static_cast<float>(SpruceArchitecture::kBranchCountMin) +
        static_cast<float>(SpruceArchitecture::kBranchCountMax -
            SpruceArchitecture::kBranchCountMin) *
            (random.Uniform01() * 0.6F + 0.2F));

    const float trunkHeight = trunk.height;
    for (int whorl = 0; whorl < whorlCount; ++whorl)
    {
        const float whorlProgress = static_cast<float>(whorl) /
            static_cast<float>(std::max(1, whorlCount - 1));
        // Whorls pack slightly tighter toward the crown top (vigorous recent
        // years stack up); the lowest whorls spread wider.
        const float crownRatio = std::pow(whorlProgress, 1.12F);
        const float whorlHeight = crownBase + crownLength * crownRatio;

        // Whorl azimuth rotates by the golden angle plus per-whorl jitter, so
        // fans never freeze into a cross pattern.
        const float whorlAzimuth = std::fmod(static_cast<float>(whorl) *
            (3.0F - std::sqrt(5.0F)) * 3.14159265358979F +
            (random.Uniform01() - 0.5F) * 0.24F, 6.283185307F);
        const int branchCount = std::max(3, branchCountBase -
            static_cast<int>(3.0F * std::pow(1.0F - crownRatio, 2.4F)) +
            random.UniformInt(-1, 1));

        for (int branch = 0; branch < branchCount; ++branch)
        {
            const float azimuth = whorlAzimuth +
                static_cast<float>(branch) * (6.283185307F / static_cast<float>(branchCount)) +
                (random.Uniform01() - 0.5F) * 0.14F;
            // Branch rise: low whorls hang near horizontal, upper whorls
            // ascend. Articulated on a crown-ratio curve, +/- deterministic
            // jitter.
            const float incline = 2.0F + 34.0F * std::pow(crownRatio, 1.7F) +
                (random.Uniform01() - 0.5F) * 9.0F;
            const float branchLength = crownRadius *
                (0.94F - 0.66F * std::pow(crownRatio, 2.3F)) *
                (0.86F + 0.26F * random.Uniform01()) * diversity;
            if (branchLength < 0.25F) continue;

            // Branch root grows from the trunk surface at whorl height with
            // load-consistent relative thickness.
            const GrowSite trunkSite = ChainSite(skeleton, trunkChain, trunkSteps,
                Clamp01(whorlHeight / std::max(0.001F, trunkHeight)));
            const float branchRadius = trunkSite.radius * Lerp(
                SpruceArchitecture::kBranchRootMin, SpruceArchitecture::kBranchRootMax,
                random.Uniform01() * 0.70F + 0.15F);

            ChainBuild primary{};
            primary.origin = trunkSite.position;
            primary.direction = DirectionFromAzimuthIncline(azimuth, incline * 3.14159265358979F / 180.0F);
            primary.startRadius = std::max(0.016F, branchRadius);
            primary.endRadius = primary.startRadius * 0.15F;
            primary.length = branchLength;
            primary.stepCount = std::max(3, static_cast<int>(std::ceil(branchLength / 0.52F)));
            primary.droop = Lerp(SpruceArchitecture::kPrimaryDroopMin,
                SpruceArchitecture::kPrimaryDroopMax, random.Uniform01()) *
                (0.55F + 0.85F * (1.0F - crownRatio));
            primary.order = 1;
            primary.materialGroup = 0; // conifer primaries share the bark material
            primary.noiseScale = 0.26F;
            primary.taperPower = 0.62F;
            primary.bearingKind = 1; // spruce shoot sprays
            primary.snowCapture = SnowCaptureFor(crownRatio, branchRadius,
                0.30F, 0.28F, random.Uniform01());

            const std::uint64_t primarySeed = Mix64(seed + static_cast<std::uint64_t>(whorl) * 7817u +
                static_cast<std::uint64_t>(branch) * 104729u);
            const std::int32_t primaryFirst = BuildChain(skeleton, primarySeed, trunkChain, primary);
            if (primaryFirst < 0) continue;
            const int primarySteps = primary.stepCount;

            // Secondary needle sprays: flattened shoots stacked on both
            // lateral sides of the primary, larger near the base, shorter
            // toward the tip, plus terminal tertiary shoots at the primary tip.
            const int secondaryCount = std::max(2, static_cast<int>(
                static_cast<float>(SpruceArchitecture::kSecondaryCountMin) +
                static_cast<float>(SpruceArchitecture::kSecondaryCountMax -
                    SpruceArchitecture::kSecondaryCountMin) *
                    random.Uniform01() * static_cast<float>(primarySteps) * 0.20F));
            const Float3 lateral = Normalized(Cross(primary.direction, Float3{ 0.0F, 1.0F, 0.0F }));

            for (int secondary = 0; secondary < secondaryCount; ++secondary)
            {
                const float lane = (secondary % 2 == 0) ? 1.0F : -1.0F;
                const float progress = static_cast<float>(secondary) /
                    std::max(1.0F, static_cast<float>(secondaryCount - 1));
                const float attachFraction = 0.18F + 0.70F * progress +
                    (random.Uniform01() - 0.5F) * 0.07F;
                if (attachFraction > 0.97F) continue;
                const GrowSite spot = ChainSite(skeleton, primaryFirst, primarySteps, attachFraction);
                if (spot.radius <= 0.0F) continue;

                // Spray direction: tilted down out of the primary's lateral
                // plane; near-horizontal shoots catch the stacked needle
                // mats most efficiently.
                const float sprayTilt = (19.0F + 12.0F * random.Uniform01()) * 3.14159265358979F / 180.0F;
                ChainBuild spray{};
                spray.origin = spot.position;
                spray.direction = Normalized(Float3{
                    primary.direction[0] * 0.30F + lateral[0] * lane * 0.90F,
                    primary.direction[1] * 0.22F - std::sin(sprayTilt),
                    primary.direction[2] * 0.30F + lateral[2] * lane * 0.90F });
                spray.startRadius = std::max(0.010F, spot.radius * Lerp(0.42F, 0.60F, random.Uniform01()));
                spray.endRadius = spray.startRadius * 0.28F;
                // Spray length decreases along the primary toward the tip.
                spray.length = primary.length * Lerp(0.44F, 0.13F, progress * 0.85F) *
                    (0.80F + 0.36F * random.Uniform01());
                spray.stepCount = std::max(2, static_cast<int>(std::ceil(spray.length / 0.38F)));
                spray.droop = Lerp(SpruceArchitecture::kSecondaryDroopMin,
                    SpruceArchitecture::kSecondaryDroopMax, random.Uniform01());
                spray.order = 2;
                spray.materialGroup = 2;
                spray.noiseScale = 0.30F;
                spray.taperPower = 0.72F;
                spray.bearingKind = 1;
                spray.snowCapture = SnowCaptureFor(crownRatio, spray.startRadius,
                    0.26F, 0.30F, random.Uniform01());
                const std::uint64_t spraySeed = Mix64(primarySeed +
                    static_cast<std::uint64_t>(secondary) * 3571u);
                BuildChain(skeleton, spraySeed, primaryFirst, spray);
            }

            // Terminal bundle: 1-3 tiny shoots at the primary tip completing
            // the spray silhouette with short tertiary whorls.
            const int tertiaryCount = random.UniformInt(1, 3);
            for (int tertiary = 0; tertiary < tertiaryCount; ++tertiary)
            {
                const GrowSite tip = ChainSite(skeleton, primaryFirst, primarySteps, 0.94F);
                if (tip.radius <= 0.0F) continue;
                const float tipAzimuth = random.Uniform01() * 6.283185307F;
                ChainBuild tipChain{};
                tipChain.origin = tip.position;
                tipChain.direction = Normalized(Float3{
                    std::cos(tipAzimuth) * 0.72F,
                    primary.direction[1] * 0.55F - 0.28F,
                    std::sin(tipAzimuth) * 0.72F });
                tipChain.startRadius = std::max(0.006F, tip.radius * 0.55F);
                tipChain.endRadius = tipChain.startRadius * 0.45F;
                tipChain.length = Lerp(0.14F, 0.42F, random.Uniform01());
                tipChain.stepCount = 2;
                tipChain.droop = 0.08F + 0.06F * random.Uniform01();
                tipChain.order = 3;
                tipChain.materialGroup = 2;
                tipChain.noiseScale = 0.25F;
                tipChain.taperPower = 0.60F;
                tipChain.bearingKind = 1;
                tipChain.snowCapture = SnowCaptureFor(crownRatio, tipChain.startRadius,
                    0.24F, 0.30F, random.Uniform01());
                BuildChain(skeleton, Mix64(primarySeed + 911u +
                    static_cast<std::uint64_t>(tertiary) * 131u),
                    primaryFirst + primarySteps - 1, tipChain);
            }
        }
    }

    // Central leader tip: a rising bundle of short shoots capping the crown
    // spike (the characteristic sharp spruce top of every winter photo).
    const int leaderCount = random.UniformInt(SpruceArchitecture::kLeaderClusterMin,
        SpruceArchitecture::kLeaderClusterMax);
    const GrowSite trunkTip = ChainSite(skeleton, trunkChain, trunkSteps, 1.0F);
    for (int leader = 0; leader < leaderCount; ++leader)
    {
        const float azimuth = random.Uniform01() * 6.283185307F;
        const float tipRise = 0.62F + 0.30F * random.Uniform01();
        ChainBuild tip{};
        tip.origin = trunkTip.position;
        tip.direction = Normalized(Float3{
            std::cos(azimuth) * (1.0F - tipRise) * 0.62F,
            tipRise,
            std::sin(azimuth) * (1.0F - tipRise) * 0.62F });
        tip.startRadius = std::max(0.010F, trunkTip.radius * 0.38F);
        tip.endRadius = tip.startRadius * 0.42F;
        tip.length = Lerp(0.35F, 0.95F, random.Uniform01());
        tip.stepCount = std::max(2, static_cast<int>(std::ceil(tip.length / 0.30F)));
        tip.droop = 0.02F + 0.03F * random.Uniform01();
        tip.order = 2;
        tip.materialGroup = 2;
        tip.noiseScale = 0.18F;
        tip.taperPower = 0.55F;
        tip.bearingKind = 1;
        tip.snowCapture = SnowCaptureFor(0.97F, tip.startRadius, 0.46F, 0.30F,
            random.Uniform01());
        BuildChain(skeleton, Mix64(seed + 66001u + static_cast<std::uint64_t>(leader) * 271u),
            trunkChain, tip);
    }

    skeleton.height = height;
    skeleton.trunkRadius = breastRadius;
    return skeleton;
}
} // namespace talvisota::vegetation::detail
