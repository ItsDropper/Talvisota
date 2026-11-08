#pragma once

#include "talvisota/vegetation/Skeleton.hpp"

#include <cstdint>

namespace talvisota::vegetation
{
// Generation request. Deterministic: identical (seed, stage) values rebuild
// byte-identical geometry, enabling variation by seed and height stage only.
struct SpeciesBuild final
{
    std::uint64_t seed = 0;
    float height = 16.0F;                  // metres, equals 1 m = 1 world unit
    float branchDiversity = 1.0F;          // 0.6..1.4 drives branch count/length scatter
    float asymmetry = 0.5F;                // 0 symmetric .. 1 one-sided crown
    float windLean = 0.0F;                 // 0 upright .. 1 strong exposure lean
};

enum class Species : std::uint8_t
{
    Spruce,
    Pine,
    Birch
};

// Species-specific architectural generators. These are not one generic tree
// with different textures: each species owns its own crown-layout, whorl,
// shoot and droop logic tuned separately below, including its own winter
// bare-branch handling (birch is bare, spruce/pine retain needle mass).
[[nodiscard]] TreeSkeleton GenerateTree(Species species, const SpeciesBuild& build);

// Species description used by asset tooling (material names, LOD policy).
struct SpeciesMaterialGroups final
{
    // Submesh names must match the authored MTL entries and the typed
    // material override rules (the engine sample swaps in typed materials
    // for the exact submesh name `snow`).
    static constexpr const char* kBark = "bark";
    static constexpr const char* kBarkLower = "bark_lower";
    static constexpr const char* kNeedles = "needles";
    static constexpr const char* kTwigs = "twigs";
    static constexpr const char* kSnow = "snow";
};

// Trunk statistics for integration (collision records, scale sanity):
struct TrunkMeasurements final
{
    float height = 0.0F;
    float buttRadius = 0.0F;               // ground radius
    float breastRadius = 0.0F;             // radius at 1.3 m
    float tipRadius = 0.0F;
};

[[nodiscard]] TrunkMeasurements MeasureTrunk(const TreeSkeleton& skeleton) noexcept;
} // namespace talvisota::vegetation
