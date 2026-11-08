#pragma once

#include <cmath>
#include <cstdint>

namespace talvisota::vegetation
{
// SplitMix64 core: the vegetation library must only ever produce deterministic
// geometry, so all variation flows through this single well-defined mixing
// function. Same seed -> identical tree bytes across tool runs and machines.
[[nodiscard]] inline std::uint64_t Mix64(std::uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

class RandomSequence final
{
public:
    explicit RandomSequence(std::uint64_t seed) noexcept
        : state_(Mix64(seed))
    {
    }

    [[nodiscard]] std::uint64_t NextU64() noexcept
    {
        state_ += 0x9E3779B97F4A7C15ULL;
        return Mix64(state_);
    }

    // Uniform in [0, 1).
    [[nodiscard]] float Uniform01() noexcept
    {
        return static_cast<float>(NextU64() >> 40U) * (1.0F / 16777216.0F);
    }

    // Uniform in [lo, hi).
    [[nodiscard]] float Uniform(const float lo, const float hi) noexcept
    {
        return lo + (hi - lo) * Uniform01();
    }

    [[nodiscard]] bool Bernoulli(const float probability) noexcept
    {
        return Uniform01() < probability;
    }

    [[nodiscard]] int UniformInt(const int lo, const int hi) noexcept
    {
        // hi may equal lo; the +1U keeps closed intervals non-degenerate.
        const std::uint64_t span = static_cast<std::uint64_t>(hi - lo + 1);
        return lo + static_cast<int>(NextU64() % span);
    }

    // Deterministic independent 2-second mixing that keeps two Uniform calls
    // correlated structurally while remaining reproducible. Used for angle
    // pairs that should cover a full period evenly across specimens.
    [[nodiscard]] float Signed01() noexcept
    {
        return Uniform01() * 2.0F - 1.0F;
    }

private:
    std::uint64_t state_;
};

// Unit vector: deterministic uniform spherical direction. Avoids trig and
// Mantel guesses so specimens with the same seed produce identical geometry.
[[nodiscard]] inline float HashUnit01(const std::uint64_t seed, const std::uint64_t salt) noexcept
{
    return static_cast<float>(Mix64(seed + salt * 0x9E3779B97F4A7C15ULL) >> 40U) *
        (1.0F / 16777216.0F);
}

// Deterministic smooth 1D value noise in [0, 1); used for bark irregularity
// and branch curvature rather than raw jitter so features look continuous.
[[nodiscard]] inline float ByteNoise(const std::uint64_t seed, const int index) noexcept
{
    std::uint64_t value = Mix64(seed + static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ULL);
    value >>= 40U;
    return static_cast<float>(value) * (1.0F / 16777216.0F);
}

[[nodiscard]] inline float SmoothNoise(const std::uint64_t seed, const float x) noexcept
{
    const float floorX = floorf(x);
    const int cell = static_cast<int>(floorX);
    const float fraction = x - floorX;
    const float eased = fraction * fraction * (3.0F - 2.0F * fraction);
    const float a = ByteNoise(seed, cell);
    const float b = ByteNoise(seed, cell + 1);
    return a + (b - a) * eased;
}

[[nodiscard]] inline float FbmNoise(const std::uint64_t seed, const float x, const int octaves = 4) noexcept
{
    float amplitude = 0.5F;
    float total = 0.0F;
    float normalization = 0.0F;
    float frequency = 1.0F;
    for (int octave = 0; octave < octaves; ++octave)
    {
        total += SmoothNoise(seed + static_cast<std::uint64_t>(octave) * 0x79F245EA7BEB8D33ULL,
            x * frequency) * amplitude;
        normalization += amplitude;
        amplitude *= 0.5F;
        frequency *= 2.03F;
    }
    return total / normalization;
}
} // namespace talvisota::vegetation
