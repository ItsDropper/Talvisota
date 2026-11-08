#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr int TextureSize = 1024;
constexpr float Pi = 3.14159265358979323846F;

struct Sample
{
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float height = 0.5F;
    float roughness = 0.9F;
    float occlusion = 1.0F;
};

struct Rgb8
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

int Wrap(const int value, const int period) noexcept
{
    const int remainder = value % period;
    return remainder < 0 ? remainder + period : remainder;
}

float Hash(const int x, const int y, const std::uint32_t seed) noexcept
{
    std::uint32_t value = seed ^
        (static_cast<std::uint32_t>(x) * 0x9e3779b9U) ^
        (static_cast<std::uint32_t>(y) * 0x85ebca6bU);
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return static_cast<float>(value & 0x00ffffffU) / 16777215.0F;
}

float Smooth(const float value) noexcept
{
    const float t = std::clamp(value, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

float NoiseTile(const float x, const float y, const int periodX, const int periodY,
                const std::uint32_t seed) noexcept
{
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = Smooth(x - static_cast<float>(x0));
    const float ty = Smooth(y - static_cast<float>(y0));
    const float a = Hash(Wrap(x0, periodX), Wrap(y0, periodY), seed);
    const float b = Hash(Wrap(x0 + 1, periodX), Wrap(y0, periodY), seed);
    const float c = Hash(Wrap(x0, periodX), Wrap(y0 + 1, periodY), seed);
    const float d = Hash(Wrap(x0 + 1, periodX), Wrap(y0 + 1, periodY), seed);
    const float top = a + (b - a) * tx;
    const float bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

float NoiseTile(const float x, const float y, const int period,
                const std::uint32_t seed) noexcept
{
    return NoiseTile(x, y, period, period, seed);
}

float Fbm(const float u, const float v, const int baseFrequencyX,
          const int baseFrequencyY, const std::uint32_t seed, const int octaves) noexcept
{
    float total = 0.0F;
    float amplitude = 0.5F;
    float normalization = 0.0F;
    int frequencyX = baseFrequencyX;
    int frequencyY = baseFrequencyY;
    for (int octave = 0; octave < octaves; ++octave)
    {
        total += NoiseTile(u * static_cast<float>(frequencyX),
            v * static_cast<float>(frequencyY), frequencyX, frequencyY,
            seed + static_cast<std::uint32_t>(octave) * 0x9e3779b9U) * amplitude;
        normalization += amplitude;
        amplitude *= 0.5F;
        frequencyX *= 2;
        frequencyY *= 2;
    }
    return normalization > 0.0F ? total / normalization : 0.5F;
}

float Fbm(const float u, const float v, const int baseFrequency,
          const std::uint32_t seed, const int octaves) noexcept
{
    return Fbm(u, v, baseFrequency, baseFrequency, seed, octaves);
}

float VoronoiEdge(const float u, const float v, const int period,
                   const std::uint32_t seed) noexcept
{
    const float x = u * static_cast<float>(period);
    const float y = v * static_cast<float>(period);
    const int cellX = static_cast<int>(std::floor(x));
    const int cellY = static_cast<int>(std::floor(y));
    float nearest = 1000.0F;
    float second = 1000.0F;
    for (int oy = -1; oy <= 1; ++oy)
    {
        for (int ox = -1; ox <= 1; ++ox)
        {
            const int wrappedX = Wrap(cellX + ox, period);
            const int wrappedY = Wrap(cellY + oy, period);
            const float jitterX = 0.16F + Hash(wrappedX, wrappedY, seed) * 0.68F;
            const float jitterY = 0.16F + Hash(wrappedX, wrappedY, seed ^ 0xa54ff53aU) * 0.68F;
            const float dx = static_cast<float>(ox) + jitterX - (x - static_cast<float>(cellX));
            const float dy = static_cast<float>(oy) + jitterY - (y - static_cast<float>(cellY));
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance < nearest)
            {
                second = nearest;
                nearest = distance;
            }
            else if (distance < second)
                second = distance;
        }
    }
    return second - nearest;
}

float PeriodicDistance(const float a, const float b) noexcept
{
    const float difference = std::abs(a - b);
    return std::min(difference, 1.0F - difference);
}

float Knot(const float u, const float v) noexcept
{
    constexpr std::array<std::array<float, 2>, 5> centers = {{
        {{0.17F, 0.24F}}, {{0.72F, 0.68F}}, {{0.43F, 0.91F}},
        {{0.91F, 0.12F}}, {{0.08F, 0.78F}}
    }};
    float result = 0.0F;
    for (const auto& center : centers)
    {
        const float dx = PeriodicDistance(u, center[0]);
        const float dy = PeriodicDistance(v, center[1]);
        const float radius = std::sqrt(dx * dx * 1.7F + dy * dy);
        const float ring = 1.0F - Smooth((radius - 0.012F) / 0.045F);
        result = std::max(result, ring);
    }
    return result;
}

Sample Evaluate(const std::string_view preset, const float u, const float v,
                const std::uint32_t seed) noexcept
{
    const float broad = Fbm(u, v, 4, seed, 5);
    const float medium = Fbm(u, v, 24, seed ^ 0x51ed270bU, 4);
    const float fine = Fbm(u, v, 96, seed ^ 0x68bc21ebU, 3);
    const float grain = Fbm(u, v, 256, seed ^ 0x02e5be93U, 2);
    const float knot = Knot(u, v);
    Sample sample{};

    if (preset == "bark")
    {
        // Longitudinal spruce bark plates, torn fissure edges, shallow flakes,
        // fine fibre relief and sparse cold-weather lichen.
        const float warpedU = u + (broad - 0.5F) * 0.018F + (medium - 0.5F) * 0.006F;
        const float ridge = std::sin((warpedU * 39.0F + (broad - 0.5F) * 0.7F) * 2.0F * Pi);
        const float fissure = Smooth((std::abs(ridge) - 0.67F) * 9.0F);
        const float flakeNoise = Fbm(u, v, 188, seed ^ 0x243f6a88U, 3);
        const float flakeEdges = Smooth((std::abs(flakeNoise - 0.5F) - 0.30F) * 15.0F);
        const float lichenField = Fbm(u, v, 17, seed ^ 0xb7e15162U, 4);
        const float lichen = Smooth((lichenField - 0.70F) * 7.0F) *
            Smooth((fine - 0.39F) * 3.5F);
        const float fibre = Fbm(u, v, 420, seed ^ 0x9e3779b9U, 2);
        const float height = 0.50F + ridge * 0.13F + (medium - 0.5F) * 0.11F +
            (fine - 0.5F) * 0.052F + (fibre - 0.5F) * 0.025F -
            fissure * 0.085F - flakeEdges * 0.022F - knot * 0.08F;
        const float variation = (broad - 0.5F) * 0.12F + (fine - 0.5F) * 0.035F;
        sample = {0.205F + variation - fissure * 0.070F + lichen * 0.035F,
                  0.108F + variation * 0.62F - fissure * 0.045F + lichen * 0.030F,
                  0.052F + variation * 0.38F - fissure * 0.026F + lichen * 0.018F,
                  height, 0.79F + medium * 0.15F + flakeEdges * 0.035F,
                  0.96F - fissure * 0.54F - knot * 0.22F - flakeEdges * 0.08F};
    }
    else if (preset == "pine_needles")
    {
        // Directional individual-needle striations sit inside larger,
        // irregular sprays; the two scales break up the old sinusoidal tiling.
        const float clumps = Fbm(u, v, 18, seed ^ 0x3c6ef372U, 4);
        const float sprays = Fbm(u, v, 54, seed ^ 0xa54ff53aU, 3);
        const float needle = std::sin((u * 186.0F + v * 71.0F +
            (clumps - 0.5F) * 2.4F + (sprays - 0.5F) * 0.9F) * 2.0F * Pi);
        const float needleEdge = Smooth((std::abs(needle) - 0.80F) * 11.0F);
        const float clusters = Smooth((clumps - 0.27F) * 2.3F);
        const float cuticle = Fbm(u, v, 360, seed ^ 0x510e527fU, 2);
        const float height = 0.50F + needle * (0.065F + clusters * 0.055F) +
            (fine - 0.5F) * 0.055F + (grain - 0.5F) * 0.030F +
            (cuticle - 0.5F) * 0.022F - needleEdge * 0.025F;
        const float highlight = std::max(0.0F, needle) * clusters * 0.032F;
        const float shadow = needleEdge * clusters * 0.018F;
        sample = {0.038F + broad * 0.035F + highlight - shadow,
                  0.105F + broad * 0.070F + highlight * 1.2F - shadow * 0.8F,
                  0.045F + medium * 0.030F + highlight * 0.45F - shadow * 0.4F,
                  height, 0.86F + medium * 0.10F + cuticle * 0.025F,
                  0.88F + clusters * 0.08F - needleEdge * 0.045F};
    }
    else if (preset == "birch_bark")
    {
        const float bands = std::sin((v * 46.0F + (broad - 0.5F) * 0.12F) * 2.0F * Pi);
        const float darkBand = Smooth((std::abs(bands) - 0.89F) * 15.0F);
        const float lenticelNoise = NoiseTile(u * 96.0F, v * 96.0F, 96,
            seed ^ 0xa54ff53aU);
        const float lenticels = Smooth((lenticelNoise - 0.82F) * 12.0F) *
            Smooth((0.25F - std::abs(bands)) * 5.0F);
        const float height = 0.50F + bands * 0.075F + (medium - 0.5F) * 0.06F -
            darkBand * 0.075F - lenticels * 0.035F;
        const float shade = darkBand * 0.43F + lenticels * 0.34F;
        sample = {0.78F + broad * 0.08F - shade,
                  0.775F + broad * 0.075F - shade * 0.96F,
                  0.72F + medium * 0.09F - shade * 0.84F,
                  height, 0.88F + fine * 0.09F, 0.98F - shade * 0.52F};
    }
    else if (preset == "aspen_bark")
    {
        const float scars = std::sin((v * 19.0F + (broad - 0.5F) * 0.18F) * 2.0F * Pi);
        const float scarMask = Smooth((std::abs(scars) - 0.955F) * 24.0F) *
            Smooth((fine - 0.34F) * 2.6F);
        const float smoothGrain = (medium - 0.5F) * 0.045F + (fine - 0.5F) * 0.018F;
        const float height = 0.50F + scars * 0.018F + smoothGrain - scarMask * 0.065F;
        sample = {0.48F + broad * 0.075F + smoothGrain - scarMask * 0.18F,
                  0.50F + broad * 0.075F + smoothGrain - scarMask * 0.16F,
                  0.43F + medium * 0.07F + smoothGrain - scarMask * 0.13F,
                  height, 0.88F + fine * 0.08F, 0.98F - scarMask * 0.52F};
    }
    else if (preset == "alder_bark")
    {
        const float ridges = std::sin((u * 49.0F + (broad - 0.5F) * 1.1F) * 2.0F * Pi);
        const float deepFissure = Smooth((std::abs(ridges) - 0.62F) * 6.0F);
        const float roughnessNoise = Fbm(u, v, 144, seed ^ 0x9b05688cU, 3);
        const float height = 0.50F + ridges * 0.15F + (medium - 0.5F) * 0.12F +
            (fine - 0.5F) * 0.07F - deepFissure * 0.11F - knot * 0.07F;
        const float darkening = deepFissure * 0.12F + knot * 0.045F;
        sample = {0.19F + broad * 0.09F - darkening,
                  0.16F + medium * 0.075F - darkening * 0.9F,
                  0.115F + fine * 0.05F - darkening * 0.7F,
                  height, 0.86F + roughnessNoise * 0.12F,
                  0.97F - deepFissure * 0.52F - knot * 0.22F};
    }
    else if (preset == "granite")
    {
        const float cells = std::abs(medium - 0.5F);
        const float fracture = Smooth((cells - 0.34F) * 16.0F);
        const float mineral = (fine - 0.5F) * 0.12F + (grain - 0.5F) * 0.055F;
        const float height = 0.50F + (broad - 0.5F) * 0.18F +
            (medium - 0.5F) * 0.12F + (fine - 0.5F) * 0.05F - fracture * 0.045F;
        sample = {0.36F + mineral - fracture * 0.075F,
                  0.38F + mineral * 0.92F - fracture * 0.070F,
                  0.40F + mineral * 1.08F - fracture * 0.062F,
                  height, 0.76F + medium * 0.18F, 0.98F - fracture * 0.38F};
    }
    else if (preset == "snow")
    {
        // Domain-warped, isotropic drift fields create broad wind accumulation
        // without the diagonal directional bias of periodic ridge functions.
        const float windX = Fbm(u, v, 5, seed ^ 0x243f6a88U, 4);
        const float windZ = Fbm(u, v, 7, seed ^ 0xb7e15162U, 4);
        const float drift = Fbm(u + (windX - 0.5F) * 0.075F,
            v + (windZ - 0.5F) * 0.075F, 8, seed ^ 0x3c6ef372U, 5);
        const float driftShoulder = Fbm(u + (windZ - 0.5F) * 0.022F,
            v - (windX - 0.5F) * 0.022F, 29, seed ^ 0xa54ff53aU, 3);
        const float crystalClusters = Fbm(u, v, 164, seed ^ 0x68bc21ebU, 3);
        const float crystalGrain = Fbm(u, v, 392, seed ^ 0x02e5be93U, 2);
        const float microCrust = Fbm(u, v, 620, seed ^ 0x9b05688cU, 2);
        const float microSparkle = Fbm(u, v, 940, seed ^ 0x1f83d9abU, 2);
        const float micro = (crystalClusters - 0.5F) * 0.052F +
            (crystalGrain - 0.5F) * 0.026F +
            (microCrust - 0.5F) * 0.014F + (microSparkle - 0.5F) * 0.008F;
        const float height = 0.50F + (drift - 0.5F) * 0.105F +
            (driftShoulder - 0.5F) * 0.042F + micro;
        const float albedoVariation = (drift - 0.5F) * 0.105F +
            (driftShoulder - 0.5F) * 0.060F + micro * 0.52F +
            (microCrust - 0.5F) * 0.020F;
        const float frost = Smooth((crystalClusters - 0.34F) * 2.4F);
        const float packedCrust = Smooth((microCrust - 0.56F) * 4.5F);
        sample = {0.875F + albedoVariation,
                  0.895F + albedoVariation * 0.91F,
                  0.925F + albedoVariation * 0.78F,
                  height, 0.89F + driftShoulder * 0.075F + frost * 0.012F +
                    packedCrust * 0.018F + microSparkle * 0.012F,
                  0.985F - frost * 0.075F - Smooth((crystalGrain - 0.88F) * 8.0F) * 0.035F -
                    packedCrust * 0.018F};
    }
    else if (preset == "packed_snow")
    {
        // Compaction follows irregular pressure and foot/vehicle scuffing rather
        // than a periodic sine wave, which produced conspicuous parallel tracks.
        // Preserve the original 9x18 compression scale while making both
        // texture axes repeat at their own integer lattice periods.
        const float compression = Fbm(u, v, 9, 18,
            seed ^ 0x510e527fU, 4);
        const float scuff = Fbm(u, v, 42, seed ^ 0x1f83d9abU, 3);
        const float granular = Fbm(u, v, 156, seed ^ 0x5be0cd19U, 2);
        const float height = 0.50F + (compression - 0.5F) * 0.075F +
            (scuff - 0.5F) * 0.040F + (granular - 0.5F) * 0.012F;
        const float albedoVariation = (compression - 0.5F) * 0.095F +
            (scuff - 0.5F) * 0.060F + (granular - 0.5F) * 0.025F;
        sample = {0.65F + broad * 0.045F + albedoVariation,
                  0.70F + broad * 0.043F + albedoVariation * 0.92F,
                  0.76F + broad * 0.040F + albedoVariation * 0.82F,
                  height, 0.86F + medium * 0.11F,
                  0.94F - Smooth((scuff - 0.8F) * 3.0F) * 0.13F};
    }
    else if (preset == "ice")
    {
        // Fractures are a jittered, periodic Voronoi network with a second
        // finer branch scale. This produces connected, branching lake-ice
        // seams rather than parallel contour bands or a tiled crack stencil.
        const float cellEdges = VoronoiEdge(u, v, 37, seed ^ 0x5be0cd19U);
        const float branchEdges = VoronoiEdge(u, v, 89, seed ^ 0x9b05688cU);
        const float branchNoise = Fbm(u, v, 43, seed ^ 0x1f83d9abU, 3);
        const float iceCloud = Fbm(u, v, 6, seed ^ 0x243f6a88U, 5);
        const float frostCloud = Fbm(u, v, 19, seed ^ 0xb7e15162U, 4);
        const float mainCracks = 1.0F - Smooth((cellEdges - 0.0042F) * 165.0F);
        const float fineCracks = 1.0F - Smooth((branchEdges - 0.0021F) * 316.0F);
        const float fracture = std::clamp(std::max(mainCracks,
            fineCracks * Smooth((branchNoise - 0.28F) * 2.2F) * 0.30F), 0.0F, 1.0F);
        const float frost = Smooth((frostCloud - 0.38F) * 2.5F);
        const float bubbles = Smooth((Fbm(u, v, 88, seed ^ 0xa54ff53aU, 3) - 0.72F) * 7.0F);
        const float height = 0.50F + (iceCloud - 0.5F) * 0.085F +
            (frostCloud - 0.5F) * 0.032F - fracture * 0.012F +
            (fine - 0.5F) * 0.018F;
        const float cloudTint = (iceCloud - 0.5F) * 0.12F +
            (frostCloud - 0.5F) * 0.055F + bubbles * 0.035F;
        sample = {0.54F + cloudTint + frost * 0.12F - fracture * 0.085F,
                  0.665F + cloudTint * 0.94F + frost * 0.13F - fracture * 0.082F,
                  0.775F + cloudTint * 0.86F + frost * 0.14F - fracture * 0.078F,
                  height, 0.11F + frost * 0.52F + iceCloud * 0.08F + fracture * 0.15F,
                  0.99F - fracture * 0.28F - bubbles * 0.035F};
    }
    else
    {
        const float stalks = std::sin((u * 52.0F + (broad - 0.5F) * 0.65F) * 2.0F * Pi);
        const float fibre = Fbm(u, v, 80, seed ^ 0x1f83d9abU, 3);
        const float height = 0.50F + stalks * 0.065F + (fibre - 0.5F) * 0.06F;
        sample = {0.34F + broad * 0.15F + std::max(0.0F, stalks) * 0.035F,
                  0.28F + medium * 0.12F + std::max(0.0F, stalks) * 0.025F,
                  0.16F + fine * 0.08F,
                  height, 0.91F + medium * 0.07F, 0.95F - Smooth((fibre - 0.82F) * 4.0F) * 0.12F};
    }

    sample.red = std::clamp(sample.red, 0.0F, 1.0F);
    sample.green = std::clamp(sample.green, 0.0F, 1.0F);
    sample.blue = std::clamp(sample.blue, 0.0F, 1.0F);
    sample.roughness = std::clamp(sample.roughness, 0.04F, 1.0F);
    sample.occlusion = std::clamp(sample.occlusion, 0.0F, 1.0F);
    return sample;
}

bool VerifyProceduralPreset(const std::string_view preset, const std::uint32_t seed,
                           std::string& error) noexcept
{
    constexpr float tolerance = 0.00015F;
    float minimumHeight = 1.0F, maximumHeight = 0.0F;
    float minimumRoughness = 1.0F, maximumRoughness = 0.0F;
    float minimumAlbedo = 1.0F, maximumAlbedo = 0.0F;
    const auto close = [tolerance](const Sample& a, const Sample& b) {
        return std::abs(a.red-b.red)<=tolerance && std::abs(a.green-b.green)<=tolerance &&
            std::abs(a.blue-b.blue)<=tolerance && std::abs(a.height-b.height)<=tolerance &&
            std::abs(a.roughness-b.roughness)<=tolerance && std::abs(a.occlusion-b.occlusion)<=tolerance;
    };
    for (int i = 0; i <= 128; ++i)
    {
        const float t = static_cast<float>(i) / 128.0F;
        if (!close(Evaluate(preset, 0.0F, t, seed), Evaluate(preset, 1.0F, t, seed)) ||
            !close(Evaluate(preset, t, 0.0F, seed), Evaluate(preset, t, 1.0F, seed)))
        {
            error = std::string(preset) + " procedural PBR field is not seamless at its texture boundaries.";
            return false;
        }
    }
    if (preset != "snow" && preset != "ice")
    {
        error.clear();
        return true;
    }
    for (int y = 0; y < 64; ++y)
    {
        for (int x = 0; x < 64; ++x)
        {
            const Sample sample = Evaluate(preset, static_cast<float>(x)/64.0F,
                static_cast<float>(y)/64.0F, seed);
            minimumHeight = std::min(minimumHeight, sample.height);
            maximumHeight = std::max(maximumHeight, sample.height);
            minimumRoughness = std::min(minimumRoughness, sample.roughness);
            maximumRoughness = std::max(maximumRoughness, sample.roughness);
            minimumAlbedo = std::min(minimumAlbedo, sample.red);
            maximumAlbedo = std::max(maximumAlbedo, sample.red);
        }
    }
    if (maximumHeight - minimumHeight < 0.018F ||
        maximumRoughness - minimumRoughness < (preset == "ice" ? 0.16F : 0.045F) ||
        (preset == "snow" && maximumAlbedo - minimumAlbedo < 0.045F))
    {
        error = std::string(preset) + " procedural PBR fields lack the required relief or roughness variation.";
        return false;
    }
    error.clear();
    return true;
}

float LinearToSrgb(const float value) noexcept
{
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return clamped <= 0.0031308F ? clamped * 12.92F :
        1.055F * std::pow(clamped, 1.0F / 2.4F) - 0.055F;
}

std::uint8_t ToByte(const float value) noexcept
{
    return static_cast<std::uint8_t>(std::clamp(
        std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F), 0L, 255L));
}

void WriteU16(std::ofstream& output, const std::uint16_t value)
{
    const std::array<char, 2> bytes = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU)
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void WriteU32(std::ofstream& output, const std::uint32_t value)
{
    const std::array<char, 4> bytes = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU)
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool WriteBmp(const std::filesystem::path& path, const std::vector<Rgb8>& pixels,
              const int width, const int height, std::string& error)
{
    const std::uint32_t rowBytes = static_cast<std::uint32_t>(width) * 3U;
    const std::uint32_t stride = (rowBytes + 3U) & ~3U;
    const std::uint64_t imageBytes64 = static_cast<std::uint64_t>(stride) *
        static_cast<std::uint64_t>(height);
    if (imageBytes64 > 0xffffffffULL - 54ULL ||
        pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
    {
        error = "Generated BMP dimensions or byte count are invalid.";
        return false;
    }

    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError)
    {
        error = "Could not create generated texture directory: " + filesystemError.message();
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = "Could not create generated BMP: " + path.string();
        return false;
    }

    const auto imageBytes = static_cast<std::uint32_t>(imageBytes64);
    WriteU16(output, 0x4d42U);
    WriteU32(output, 54U + imageBytes);
    WriteU16(output, 0U);
    WriteU16(output, 0U);
    WriteU32(output, 54U);
    WriteU32(output, 40U);
    WriteU32(output, static_cast<std::uint32_t>(width));
    WriteU32(output, static_cast<std::uint32_t>(height));
    WriteU16(output, 1U);
    WriteU16(output, 24U);
    WriteU32(output, 0U);
    WriteU32(output, imageBytes);
    WriteU32(output, 2835U);
    WriteU32(output, 2835U);
    WriteU32(output, 0U);
    WriteU32(output, 0U);

    const std::array<char, 3> padding{};
    const std::streamsize paddingBytes = static_cast<std::streamsize>(stride - rowBytes);
    for (int y = height - 1; y >= 0; --y)
    {
        const std::size_t rowStart = static_cast<std::size_t>(y) *
            static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x)
        {
            const Rgb8& pixel = pixels[rowStart + static_cast<std::size_t>(x)];
            const std::array<char, 3> bgr = {
                static_cast<char>(pixel.blue),
                static_cast<char>(pixel.green),
                static_cast<char>(pixel.red)
            };
            output.write(bgr.data(), static_cast<std::streamsize>(bgr.size()));
        }
        if (paddingBytes > 0)
            output.write(padding.data(), paddingBytes);
    }
    if (!output)
    {
        error = "Failed while writing generated BMP: " + path.string();
        return false;
    }
    return true;
}

std::uint16_t ReadU16(const std::array<char, 54>& header, const std::size_t offset) noexcept
{
    const auto a = static_cast<std::uint8_t>(header[offset]);
    const auto b = static_cast<std::uint8_t>(header[offset + 1U]);
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(a) |
        (static_cast<std::uint16_t>(b) << 8U));
}

std::uint32_t ReadU32(const std::array<char, 54>& header, const std::size_t offset) noexcept
{
    const auto a = static_cast<std::uint8_t>(header[offset]);
    const auto b = static_cast<std::uint8_t>(header[offset + 1U]);
    const auto c = static_cast<std::uint8_t>(header[offset + 2U]);
    const auto d = static_cast<std::uint8_t>(header[offset + 3U]);
    return static_cast<std::uint32_t>(a) |
        (static_cast<std::uint32_t>(b) << 8U) |
        (static_cast<std::uint32_t>(c) << 16U) |
        (static_cast<std::uint32_t>(d) << 24U);
}

bool VerifyBmp(const std::filesystem::path& path, std::string& error,
               const float minimumChannelStandardDeviation = 0.0F)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        error = "Generated texture is missing: " + path.string();
        return false;
    }
    std::array<char, 54> header{};
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (!input || header[0] != 'B' || header[1] != 'M' ||
        ReadU32(header, 10U) != 54U || ReadU32(header, 14U) != 40U ||
        ReadU16(header, 26U) != 1U || ReadU16(header, 28U) != 24U ||
        ReadU32(header, 30U) != 0U ||
        ReadU32(header, 18U) != static_cast<std::uint32_t>(TextureSize) ||
        ReadU32(header, 22U) != static_cast<std::uint32_t>(TextureSize))
    {
        error = "Generated texture has an invalid 24-bit BMP header: " + path.string();
        return false;
    }
    const std::uint32_t rowBytes = static_cast<std::uint32_t>(TextureSize) * 3U;
    const std::uint32_t stride = (rowBytes + 3U) & ~3U;
    const std::uint32_t expectedImageBytes = stride * static_cast<std::uint32_t>(TextureSize);
    if (ReadU32(header, 34U) != expectedImageBytes)
    {
        error = "Generated texture BMP image size does not match its dimensions: " + path.string();
        return false;
    }

    std::uint8_t minimum = 255U;
    std::uint8_t maximum = 0U;
    std::array<std::uint64_t, 3> channelSums{};
    std::array<std::uint64_t, 3> channelSquaredSums{};
    std::uint64_t bytesRead = 0U;
    std::array<char, 65536> buffer{};
    while (input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        for (std::streamsize index = 0; index < count; ++index)
        {
            const auto value = static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(index)]);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            const std::size_t channel = static_cast<std::size_t>(
                (bytesRead + static_cast<std::uint64_t>(index)) % 3U);
            channelSums[channel] += value;
            channelSquaredSums[channel] += static_cast<std::uint64_t>(value) * value;
        }
        bytesRead += static_cast<std::uint64_t>(count);
    }
    if (!input.eof() || bytesRead != expectedImageBytes ||
        static_cast<unsigned int>(maximum) - static_cast<unsigned int>(minimum) < 8U)
    {
        error = "Generated texture has insufficient pixel variation or a read error: " + path.string();
        return false;
    }

    if (minimumChannelStandardDeviation > 0.0F)
    {
        constexpr double pixelCount = static_cast<double>(TextureSize) *
            static_cast<double>(TextureSize);
        for (std::size_t channel = 0; channel < channelSums.size(); ++channel)
        {
            const double mean = static_cast<double>(channelSums[channel]) / pixelCount;
            const double meanSquare = static_cast<double>(channelSquaredSums[channel]) / pixelCount;
            const double variance = std::max(0.0, meanSquare - mean * mean);
            if (std::sqrt(variance) < static_cast<double>(minimumChannelStandardDeviation))
            {
                error = "Generated albedo channel has insufficient 8-bit variation: " + path.string();
                return false;
            }
        }
    }
    return true;
}

bool VerifyAll(const std::filesystem::path& directory, std::string& error)
{
    constexpr std::array<std::string_view, 10> presets = {
        "bark", "pine_needles", "birch_bark", "aspen_bark", "alder_bark",
        "granite", "snow", "packed_snow", "ice", "reeds"
    };
    constexpr std::array<std::string_view, 4> channels = {
        "albedo", "normal", "roughness", "ao"
    };
    for (std::size_t index = 0; index < presets.size(); ++index)
    {
        const auto preset = presets[index];
        const std::uint32_t seed = 0x53414b4bU +
            static_cast<std::uint32_t>(index) * 0x9e3779b9U;
        if (!VerifyProceduralPreset(preset, seed, error))
            return false;
        for (const auto channel : channels)
        {
            const std::string filename = std::string(preset) + "-" +
                std::string(channel) + ".bmp";
            const float minimumAlbedoDeviation =
                preset == "snow" || preset == "packed_snow" ? 1.5F : 0.0F;
            if (!VerifyBmp(directory / filename, error,
                    channel == "albedo" ? minimumAlbedoDeviation : 0.0F))
                return false;
        }
    }
    return VerifyBmp(directory / "snow-orm.bmp", error);
}

bool GeneratePreset(const std::filesystem::path& outputDirectory,
                    const std::string_view preset, const std::uint32_t seed,
                    std::string& error)
{
    constexpr int width = TextureSize;
    constexpr int height = TextureSize;
    const std::size_t pixelCount = static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);
    std::vector<Sample> samples(pixelCount);
    std::vector<float> heights(pixelCount);
    std::vector<Rgb8> albedo(pixelCount);
    std::vector<Rgb8> normal(pixelCount);
    std::vector<Rgb8> roughness(pixelCount);
    std::vector<Rgb8> occlusion(pixelCount);
    std::vector<Rgb8> orm(pixelCount);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(width);
            const float v = static_cast<float>(y) / static_cast<float>(height);
            const std::size_t index = static_cast<std::size_t>(y) *
                static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            samples[index] = Evaluate(preset, u, v, seed);
            heights[index] = samples[index].height;
            albedo[index] = {
                ToByte(LinearToSrgb(samples[index].red)),
                ToByte(LinearToSrgb(samples[index].green)),
                ToByte(LinearToSrgb(samples[index].blue))
            };
            const std::uint8_t rough = ToByte(samples[index].roughness);
            const std::uint8_t ao = ToByte(samples[index].occlusion);
            roughness[index] = {rough, rough, rough};
            occlusion[index] = {ao, ao, ao};
            // ORM channel packing follows the typed material schema: R=AO, G=roughness, B=metallic.
            orm[index] = {ao, rough, 0U};
        }
    }

    const float normalStrength = preset == "ice" ? 9.0F :
        preset == "snow" ? 7.0F : preset == "packed_snow" ? 6.2F : 8.0F;
    for (int y = 0; y < height; ++y)
    {
        const int y0 = Wrap(y - 1, height);
        const int y1 = Wrap(y + 1, height);
        for (int x = 0; x < width; ++x)
        {
            const int x0 = Wrap(x - 1, width);
            const int x1 = Wrap(x + 1, width);
            const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
            const std::size_t previousRow = static_cast<std::size_t>(y0) * static_cast<std::size_t>(width);
            const std::size_t nextRow = static_cast<std::size_t>(y1) * static_cast<std::size_t>(width);
            const float dx = (heights[row + static_cast<std::size_t>(x1)] -
                heights[row + static_cast<std::size_t>(x0)]) * normalStrength;
            const float dy = (heights[nextRow + static_cast<std::size_t>(x)] -
                heights[previousRow + static_cast<std::size_t>(x)]) * normalStrength;
            const float inverseLength = 1.0F / std::sqrt(dx * dx + dy * dy + 1.0F);
            const float nx = -dx * inverseLength;
            const float ny = -dy * inverseLength;
            const float nz = inverseLength;
            normal[row + static_cast<std::size_t>(x)] = {
                ToByte(nx * 0.5F + 0.5F),
                ToByte(ny * 0.5F + 0.5F),
                ToByte(nz * 0.5F + 0.5F)
            };
        }
    }

    const std::string base(preset);
    if (!WriteBmp(outputDirectory / (base + "-albedo.bmp"), albedo, width, height, error) ||
        !WriteBmp(outputDirectory / (base + "-normal.bmp"), normal, width, height, error) ||
        !WriteBmp(outputDirectory / (base + "-roughness.bmp"), roughness, width, height, error) ||
        !WriteBmp(outputDirectory / (base + "-ao.bmp"), occlusion, width, height, error))
        return false;
    return preset != "snow" ||
        WriteBmp(outputDirectory / "snow-orm.bmp", orm, width, height, error);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Talvisota deterministic environment PBR texture generator\n"
                  << "Usage: talvisota_environment_texture_gen <output-directory>\n"
                  << "       talvisota_environment_texture_gen --verify <output-directory>\n"
                  << "Generates 1024x1024 seamless albedo, tangent-normal, roughness and AO BMP maps, plus a packed ORM map for typed snow\n"
                  << "for bark, pine_needles, birch_bark, aspen_bark, alder_bark, granite, snow, packed_snow, ice and reeds.\n";
        return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--verify")
    {
        std::string error;
        if (!VerifyAll(std::filesystem::path(argv[2]), error))
        {
            std::cerr << error << '\n';
            return 1;
        }
        std::cout << "Verified all 41 environment PBR maps.\n";
        return 0;
    }
    if (argc != 2)
    {
        std::cerr << "Usage: talvisota_environment_texture_gen <output-directory>\n";
        return 2;
    }

    const std::filesystem::path outputDirectory(argv[1]);
    constexpr std::array<std::string_view, 10> presets = {
        "bark", "pine_needles", "birch_bark", "aspen_bark", "alder_bark",
        "granite", "snow", "packed_snow", "ice", "reeds"
    };
    std::string error;
    for (std::size_t index = 0; index < presets.size(); ++index)
    {
        const auto preset = presets[index];
        const std::uint32_t seed = 0x53414b4bU +
            static_cast<std::uint32_t>(index) * 0x9e3779b9U;
        if (!VerifyProceduralPreset(preset, seed, error) ||
            !GeneratePreset(outputDirectory, preset, seed, error))
        {
            std::cerr << error << '\n';
            return 1;
        }
        std::cout << "Generated " << preset << " 1024x1024 PBR maps.\n";
    }
    if (!VerifyAll(outputDirectory, error))
    {
        std::cerr << error << '\n';
        return 1;
    }
    std::cout << "Verified all 41 generated environment PBR maps and snow/ice continuity.\n";
    return 0;
}
