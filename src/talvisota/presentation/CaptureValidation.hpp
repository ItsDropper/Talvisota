#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace talvisota::presentation
{

struct PresentedSkyRegionStats final
{
    std::size_t sampledPixels = 0U;
    std::size_t blueDominantSampleCount = 0U;
    std::uint8_t meanBlue = 0U;
    std::uint8_t meanGreen = 0U;
    std::uint8_t meanRed = 0U;
};

// Validate a broad, bright, blue-dominant region in the upper quarter of a
// presented BGRA framebuffer. A single fixed pixel is not a reliable sky
// sample: buildings, trees, and camera framing can legitimately occlude it.
[[nodiscard]] inline bool ValidatePresentedSkyRegion(
    const std::span<const std::uint8_t> bgra,
    const std::uint32_t width,
    const std::uint32_t height,
    PresentedSkyRegionStats& stats,
    std::string& error)
{
    stats = {};
    if (width < 8U || height < 8U)
    {
        error = "Presented framebuffer is too small for sky-region validation.";
        return false;
    }

    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h / 4U)
    {
        error = "Presented framebuffer dimensions overflow the BGRA byte count.";
        return false;
    }
    const std::size_t expectedBytes = w * h * 4U;
    if (bgra.size() != expectedBytes)
    {
        error = "Presented framebuffer has an invalid BGRA byte count.";
        return false;
    }

    const std::size_t regionHeight = std::max<std::size_t>(1U, h / 4U);
    const std::size_t regionPixels = w * regionHeight;
    const std::size_t requiredBlueSamples = std::min(
        regionPixels, std::max<std::size_t>(128U, regionPixels / 100U));
    stats.sampledPixels = regionPixels;

    double blueSum = 0.0;
    double greenSum = 0.0;
    double redSum = 0.0;
    for (std::size_t y = 0U; y < regionHeight; ++y)
    {
        for (std::size_t x = 0U; x < w; ++x)
        {
            const std::size_t offset = (y * w + x) * 4U;
            const unsigned int blue = bgra[offset];
            const unsigned int green = bgra[offset + 1U];
            const unsigned int red = bgra[offset + 2U];
            if (std::max({blue, green, red}) < 96U ||
                blue < 128U || blue < red + 8U)
                continue;

            ++stats.blueDominantSampleCount;
            blueSum += static_cast<double>(blue);
            greenSum += static_cast<double>(green);
            redSum += static_cast<double>(red);
        }
    }

    if (stats.blueDominantSampleCount < requiredBlueSamples)
    {
        error = "Presented top-quarter BGRA region contains " +
            std::to_string(stats.blueDominantSampleCount) +
            " bright blue-dominant pixels out of " + std::to_string(regionPixels) +
            "; expected at least " + std::to_string(requiredBlueSamples) +
            " to confirm a visible sky region and BGRA channel order.";
        return false;
    }

    const double count = static_cast<double>(stats.blueDominantSampleCount);
    stats.meanBlue = static_cast<std::uint8_t>(blueSum / count + 0.5);
    stats.meanGreen = static_cast<std::uint8_t>(greenSum / count + 0.5);
    stats.meanRed = static_cast<std::uint8_t>(redSum / count + 0.5);
    error.clear();
    return true;
}

} // namespace talvisota::presentation
