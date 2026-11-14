#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/MaterialDefinition.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/content/AuthoredDefinition.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
std::string LowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

void PrintUsage(std::ostream& output)
{
    output << "Talvisota content validator\n"
           << "Usage: talvisota_content_validate <manifest-path> <content-root>\n"
           << "       talvisota_content_validate --help\n";
}

void PrintDiagnostic(const std::filesystem::path& file, std::size_t line,
                     const std::string& field, const std::string& message)
{
    std::cerr << file.generic_string();
    if (line != 0U)
        std::cerr << ':' << line;
    if (!field.empty())
        std::cerr << " [" << field << ']';
    std::cerr << ": " << message << '\n';
}
struct TerrainMorphologyMetrics final
{
    std::size_t totalGridSamples = 0U;
    std::size_t landGridSamples = 0U;
    std::size_t waterGridSamples = 0U;
    std::size_t localReliefSamples = 0U;
    double minimumElevation = 0.0;
    double maximumElevation = 0.0;
    double elevationP05 = 0.0;
    double elevationP50 = 0.0;
    double elevationP95 = 0.0;
    double slopeP50Degrees = 0.0;
    double slopeP95Degrees = 0.0;
    double maximumSlopeDegrees = 0.0;
    double slopeOverFivePercent = 0.0;
    double slopeOverFifteenPercent = 0.0;
    double localReliefP95 = 0.0;
    double landGridCoveragePercent = 0.0;
    double localReliefWindowX = 0.0;
    double localReliefWindowZ = 0.0;
};

double Percentile(const std::vector<double>& sortedValues, const double fraction) noexcept
{
    if (sortedValues.empty())
        return 0.0;
    const double position = std::clamp(fraction, 0.0, 1.0) *
        static_cast<double>(sortedValues.size() - 1U);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    const double blend = position - static_cast<double>(lower);
    return sortedValues[lower] + (sortedValues[upper] - sortedValues[lower]) * blend;
}

bool IsAuthoredWaterSample(const talvisota::world::WorldRegion& region,
                           const float x, const float z) noexcept
{
    // WorldRegion loading sorts water surfaces by minX and rejects overlapping
    // X intervals. Mirror the runtime sampler's binary search instead of doing
    // an O(grid samples * water segments) scan on the 177x177 Sakkola grid.
    const auto after = std::upper_bound(region.waterSurfaces.begin(),
        region.waterSurfaces.end(), x,
        [](const float coordinate, const talvisota::world::WaterSurface& surface) {
            return coordinate < surface.minX;
        });
    if (after == region.waterSurfaces.begin())
        return false;
    const auto& candidate = *(after - 1);
    return x <= candidate.maxX && z >= candidate.minZ && z <= candidate.maxZ;
}

bool ComputeTerrainMorphologyMetrics(const talvisota::world::WorldRegion& region,
                                     TerrainMorphologyMetrics& output,
                                     std::string& error)
{
    const auto& terrain = region.terrain;
    if (terrain.rows < 2U || terrain.columns < 2U ||
        terrain.rows > std::numeric_limits<std::size_t>::max() / terrain.columns ||
        terrain.elevations.size() != terrain.rows * terrain.columns ||
        !std::isfinite(terrain.originX) || !std::isfinite(terrain.originZ) ||
        !std::isfinite(terrain.spacingX) || !std::isfinite(terrain.spacingZ) ||
        terrain.spacingX <= 0.0F || terrain.spacingZ <= 0.0F)
    {
        error = "Terrain morphology requires a complete heightfield with finite origin and positive spacing.";
        return false;
    }
    for (std::size_t index = 0U; index < terrain.elevations.size(); ++index)
    {
        if (!std::isfinite(terrain.elevations[index]))
        {
            error = "Terrain morphology found a non-finite source elevation at sample " +
                std::to_string(index) + ".";
            return false;
        }
    }
    for (std::size_t index = 0U; index < region.waterSurfaces.size(); ++index)
    {
        const auto& water = region.waterSurfaces[index];
        if (!std::isfinite(water.minX) || !std::isfinite(water.maxX) ||
            !std::isfinite(water.minZ) || !std::isfinite(water.maxZ) ||
            !std::isfinite(water.elevation) || water.minX > water.maxX ||
            water.minZ > water.maxZ ||
            (index > 0U && region.waterSurfaces[index - 1U].maxX > water.minX))
        {
            error = "Terrain morphology requires finite, X-sorted, non-overlapping water bounds.";
            return false;
        }
    }

    const std::size_t sampleCount = terrain.rows * terrain.columns;
    std::vector<double> elevations(sampleCount, 0.0);
    std::vector<std::uint8_t> landMask(sampleCount, 0U);
    std::vector<double> slopes;
    slopes.reserve(sampleCount);
    std::size_t waterSamples = 0U;

    for (std::size_t row = 0U; row < terrain.rows; ++row)
    {
        for (std::size_t column = 0U; column < terrain.columns; ++column)
        {
            const std::size_t index = row * terrain.columns + column;
            const double worldX = static_cast<double>(terrain.originX) +
                static_cast<double>(column) * terrain.spacingX;
            const double worldZ = static_cast<double>(terrain.originZ) +
                static_cast<double>(row) * terrain.spacingZ;
            if (!std::isfinite(worldX) || !std::isfinite(worldZ) ||
                worldX < -std::numeric_limits<float>::max() ||
                worldX > std::numeric_limits<float>::max() ||
                worldZ < -std::numeric_limits<float>::max() ||
                worldZ > std::numeric_limits<float>::max())
            {
                error = "Terrain morphology grid coordinates exceed the finite float range.";
                return false;
            }
            const float x = static_cast<float>(worldX);
            const float z = static_cast<float>(worldZ);
            if (IsAuthoredWaterSample(region, x, z))
            {
                ++waterSamples;
                continue;
            }

            float height = 0.0F;
            std::array<float, 3> normal{};
            std::string sampleError;
            if (!talvisota::world::SampleTerrainHeight(region, x, z,
                    height, normal, sampleError))
            {
                error = "Terrain morphology sample failed at grid (" +
                    std::to_string(row) + "," + std::to_string(column) + "): " +
                    sampleError;
                return false;
            }
            if (!std::isfinite(height) ||
                !std::all_of(normal.begin(), normal.end(),
                    [](const float value) { return std::isfinite(value); }))
            {
                error = "Terrain morphology received a non-finite sampled surface at grid (" +
                    std::to_string(row) + "," + std::to_string(column) + ").";
                return false;
            }
            const double normalLength = std::sqrt(
                static_cast<double>(normal[0]) * normal[0] +
                static_cast<double>(normal[1]) * normal[1] +
                static_cast<double>(normal[2]) * normal[2]);
            if (!std::isfinite(normalLength) || std::abs(normalLength - 1.0) > 0.001 ||
                normal[1] <= 0.0F)
            {
                error = "Terrain morphology received a non-unit or downward-facing surface normal at grid (" +
                    std::to_string(row) + "," + std::to_string(column) + ").";
                return false;
            }

            const double slope = std::atan2(
                std::hypot(static_cast<double>(normal[0]), static_cast<double>(normal[2])),
                static_cast<double>(normal[1])) * 57.2957795130823208768;
            if (!std::isfinite(slope))
            {
                error = "Terrain morphology produced a non-finite slope angle.";
                return false;
            }
            elevations[index] = height;
            landMask[index] = 1U;
            slopes.push_back(slope);
        }
    }

    if (slopes.size() < 4U)
    {
        error = "Terrain morphology requires at least four non-water samples.";
        return false;
    }

    std::vector<double> sortedElevations;
    sortedElevations.reserve(slopes.size());
    for (std::size_t index = 0U; index < sampleCount; ++index)
        if (landMask[index] != 0U)
            sortedElevations.push_back(elevations[index]);
    std::sort(sortedElevations.begin(), sortedElevations.end());
    std::sort(slopes.begin(), slopes.end());

    std::vector<double> localRelief;
    localRelief.reserve(sampleCount);
    for (std::size_t row = 1U; row + 1U < terrain.rows; ++row)
    {
        for (std::size_t column = 1U; column + 1U < terrain.columns; ++column)
        {
            double minimum = std::numeric_limits<double>::max();
            double maximum = std::numeric_limits<double>::lowest();
            bool completeLandWindow = true;
            for (std::size_t offsetZ = 0U; offsetZ < 3U && completeLandWindow; ++offsetZ)
            {
                for (std::size_t offsetX = 0U; offsetX < 3U; ++offsetX)
                {
                    const std::size_t sample = (row + offsetZ - 1U) * terrain.columns +
                        column + offsetX - 1U;
                    if (landMask[sample] == 0U)
                    {
                        completeLandWindow = false;
                        break;
                    }
                    minimum = std::min(minimum, elevations[sample]);
                    maximum = std::max(maximum, elevations[sample]);
                }
            }
            if (completeLandWindow)
                localRelief.push_back(maximum - minimum);
        }
    }
    std::sort(localRelief.begin(), localRelief.end());

    TerrainMorphologyMetrics candidate;
    candidate.totalGridSamples = sampleCount;
    candidate.landGridSamples = slopes.size();
    candidate.waterGridSamples = waterSamples;
    candidate.localReliefSamples = localRelief.size();
    candidate.minimumElevation = sortedElevations.front();
    candidate.maximumElevation = sortedElevations.back();
    candidate.elevationP05 = Percentile(sortedElevations, 0.05);
    candidate.elevationP50 = Percentile(sortedElevations, 0.50);
    candidate.elevationP95 = Percentile(sortedElevations, 0.95);
    candidate.slopeP50Degrees = Percentile(slopes, 0.50);
    candidate.slopeP95Degrees = Percentile(slopes, 0.95);
    candidate.maximumSlopeDegrees = slopes.back();
    candidate.slopeOverFivePercent = 100.0 * static_cast<double>(
        std::count_if(slopes.begin(), slopes.end(),
            [](const double slope) { return slope > 5.0; })) /
        static_cast<double>(slopes.size());
    candidate.slopeOverFifteenPercent = 100.0 * static_cast<double>(
        std::count_if(slopes.begin(), slopes.end(),
            [](const double slope) { return slope > 15.0; })) /
        static_cast<double>(slopes.size());
    candidate.localReliefP95 = Percentile(localRelief, 0.95);
    candidate.landGridCoveragePercent = 100.0 * static_cast<double>(slopes.size()) /
        static_cast<double>(sampleCount);
    candidate.localReliefWindowX = static_cast<double>(terrain.spacingX) * 2.0;
    candidate.localReliefWindowZ = static_cast<double>(terrain.spacingZ) * 2.0;
    output = candidate;
    error.clear();
    return true;
}

void PrintTerrainMorphology(const talvisota::world::WorldRegion& region,
                            const TerrainMorphologyMetrics& metrics)
{
    const auto previousFlags = std::cout.flags();
    const auto previousPrecision = std::cout.precision();
    std::cout << std::fixed << std::setprecision(2)
        << "Terrain morphology [" << region.id << "]: grid="
        << region.terrain.rows << "x" << region.terrain.columns
        << " spacing=" << region.terrain.spacingX << "x" << region.terrain.spacingZ
        << "m land/water=" << metrics.landGridSamples << "/" << metrics.waterGridSamples
        << " land-grid-coverage=" << metrics.landGridCoveragePercent << "%"
        << " elevation-m[min/p05/p50/p95/max]="
        << metrics.minimumElevation << "/" << metrics.elevationP05 << "/"
        << metrics.elevationP50 << "/" << metrics.elevationP95 << "/"
        << metrics.maximumElevation
        << " slope-deg[p50/p95/max]=" << metrics.slopeP50Degrees << "/"
        << metrics.slopeP95Degrees << "/" << metrics.maximumSlopeDegrees
        << " slope-area-percent[>5/>15]=" << metrics.slopeOverFivePercent << "/"
        << metrics.slopeOverFifteenPercent << " local-relief-p95=";
    if (metrics.localReliefSamples == 0U)
        std::cout << "n/a";
    else
        std::cout << metrics.localReliefP95 << "m";
    std::cout << " over " << metrics.localReliefWindowX << "x"
        << metrics.localReliefWindowZ << "m (" << metrics.localReliefSamples
        << " complete 3x3 land windows; snow displacement included, authored water excluded).\\n";
    std::cout.flags(previousFlags);
    std::cout.precision(previousPrecision);
}

bool RunTerrainMorphologySelfTests(std::string& error)
{
    const auto makeHeightfield = [] {
        talvisota::world::WorldRegion region;
        region.id = "region.terrain-metrics-test";
        region.terrain.rows = 3U;
        region.terrain.columns = 3U;
        region.terrain.originX = -10.0F;
        region.terrain.originZ = -10.0F;
        region.terrain.spacingX = 10.0F;
        region.terrain.spacingZ = 10.0F;
        region.terrain.elevations.assign(9U, 4.0F);
        return region;
    };

    TerrainMorphologyMetrics metrics;
    auto flat = makeHeightfield();
    if (!ComputeTerrainMorphologyMetrics(flat, metrics, error) ||
        metrics.landGridSamples != 9U || metrics.waterGridSamples != 0U ||
        std::abs(metrics.slopeP95Degrees) > 0.001 ||
        std::abs(metrics.localReliefP95) > 0.001 ||
        std::abs(metrics.minimumElevation - 4.0) > 0.001 ||
        std::abs(metrics.maximumElevation - 4.0) > 0.001)
    {
        if (error.empty())
            error = "Flat-plane metrics did not resolve to zero slope and relief.";
        return false;
    }

    auto ramp = makeHeightfield();
    for (std::size_t row = 0U; row < ramp.terrain.rows; ++row)
        for (std::size_t column = 0U; column < ramp.terrain.columns; ++column)
            ramp.terrain.elevations[row * ramp.terrain.columns + column] =
                2.0F + 0.5F * static_cast<float>(column) * ramp.terrain.spacingX;
    if (!ComputeTerrainMorphologyMetrics(ramp, metrics, error))
        return false;
    constexpr double expectedSlopeDegrees = 26.56505117707799;
    if (std::abs(metrics.slopeP50Degrees - expectedSlopeDegrees) > 0.01 ||
        std::abs(metrics.slopeP95Degrees - expectedSlopeDegrees) > 0.01 ||
        std::abs(metrics.localReliefP95 - 10.0) > 0.01)
    {
        error = "Planar-ramp metrics did not preserve the expected 26.565-degree slope and 10 m 3x3 relief.";
        return false;
    }

    auto shoreline = ramp;
    shoreline.waterSurfaces.push_back({
        "water.metrics-test", "placement.metrics-test",
        -0.1F, 0.1F, -0.1F, 0.1F, 0.0F});
    if (!ComputeTerrainMorphologyMetrics(shoreline, metrics, error) ||
        metrics.landGridSamples != 8U || metrics.waterGridSamples != 1U)
    {
        if (error.empty())
            error = "Authored water was not excluded from terrain morphology samples.";
        return false;
    }

    auto invalidSpacing = flat;
    invalidSpacing.terrain.spacingX = 0.0F;
    if (ComputeTerrainMorphologyMetrics(invalidSpacing, metrics, error))
    {
        error = "Terrain morphology accepted a zero-spacing heightfield.";
        return false;
    }
    auto invalidElevation = flat;
    invalidElevation.terrain.elevations[4] = std::numeric_limits<float>::quiet_NaN();
    if (ComputeTerrainMorphologyMetrics(invalidElevation, metrics, error))
    {
        error = "Terrain morphology accepted a non-finite source elevation.";
        return false;
    }
    error.clear();
    return true;
}

}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        PrintUsage(std::cout);
        return 0;
    }
    if (argc != 3)
    {
        PrintUsage(std::cerr);
        return 2;
    }

    try
    {
        std::string terrainMetricsSelfTestError;
        if (!RunTerrainMorphologySelfTests(terrainMetricsSelfTestError))
        {
            std::cerr << "Terrain morphology diagnostics self-test failed: "
                      << terrainMetricsSelfTestError << '\n';
            return 1;
        }

        const std::filesystem::path manifestPath(argv[1]);
        const std::filesystem::path contentRoot(argv[2]);
        talvisota::content::ContentRegistry registry;
        std::vector<talvisota::content::ContentDiagnostic> diagnostics;
        if (!talvisota::content::LoadContentManifest(manifestPath, contentRoot, registry, diagnostics))
        {
            for (const auto& diagnostic : diagnostics)
                PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
            return 1;
        }

        std::unordered_map<std::string, talvisota::content::WorldDefinition> worlds;
        std::unordered_map<std::string, talvisota::world::WorldRegion> regions;
        for (const auto& asset : registry.Assets())
        {
            if (asset.kind != talvisota::content::AssetKind::Definition ||
                LowerExtension(asset.relativePath) != ".tworld")
                continue;
            if (!asset.required)
            {
                std::error_code optionalError;
                const bool present = std::filesystem::exists(contentRoot / asset.relativePath, optionalError);
                if (optionalError)
                {
                    PrintDiagnostic(contentRoot / asset.relativePath, 0U, "path", "Could not inspect optional world.");
                    return 1;
                }
                if (!present)
                    continue;
            }
            talvisota::content::WorldDefinition world;
            std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
            if (!talvisota::content::LoadWorldDefinition(contentRoot / asset.relativePath, registry,
                                                         world, worldDiagnostics))
            {
                for (const auto& diagnostic : worldDiagnostics)
                    PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
                return 1;
            }
            worlds.emplace(world.id, std::move(world));
        }

        std::size_t materialCount = 0;
        std::size_t worldCount = 0;
        std::size_t authoredDefinitionCount = 0;
        std::size_t regionCount = 0;
        for (const auto& asset : registry.Assets())
        {
            if (!asset.required)
            {
                std::error_code optionalError;
                const bool present = std::filesystem::exists(contentRoot / asset.relativePath, optionalError);
                if (optionalError)
                {
                    PrintDiagnostic(contentRoot / asset.relativePath, 0U, "path",
                                    "Could not inspect optional asset.");
                    return 1;
                }
                if (!present)
                    continue;
            }
            if (asset.kind == talvisota::content::AssetKind::Definition &&
                LowerExtension(asset.relativePath) == ".tworld")
            {
                ++worldCount;
                continue;
            }

            if (asset.kind == talvisota::content::AssetKind::Definition &&
                LowerExtension(asset.relativePath) == ".tregion")
            {
                const talvisota::content::WorldDefinition* parentWorld = nullptr;
                for (const auto& dependency : asset.dependencies)
                {
                    const auto* dependencyAsset = registry.Find(dependency);
                    if (dependencyAsset != nullptr &&
                        dependencyAsset->kind == talvisota::content::AssetKind::Definition &&
                        LowerExtension(dependencyAsset->relativePath) == ".tworld")
                    {
                        if (parentWorld != nullptr)
                        {
                            PrintDiagnostic(contentRoot / asset.relativePath, 0U, "world",
                                            "Region must declare exactly one parent world dependency.");
                            return 1;
                        }
                        const auto found = worlds.find(dependency);
                        if (found != worlds.end())
                            parentWorld = &found->second;
                    }
                }
                if (parentWorld == nullptr)
                {
                    PrintDiagnostic(contentRoot / asset.relativePath, 0U, "world",
                                    "Region parent world dependency is missing or invalid.");
                    return 1;
                }
                talvisota::world::WorldRegion region;
                std::vector<talvisota::world::WorldRegionDiagnostic> regionDiagnostics;
                if (!talvisota::world::LoadWorldRegion(contentRoot / asset.relativePath, registry,
                                                       *parentWorld, region, regionDiagnostics))
                {
                    for (const auto& diagnostic : regionDiagnostics)
                        PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
                    return 1;
                }
                TerrainMorphologyMetrics terrainMetrics;
                std::string terrainMetricsError;
                if (!ComputeTerrainMorphologyMetrics(region, terrainMetrics, terrainMetricsError))
                {
                    PrintDiagnostic(contentRoot / asset.relativePath, 0U,
                                    "terrain_morphology", terrainMetricsError);
                    return 1;
                }
                PrintTerrainMorphology(region, terrainMetrics);
                regions.emplace(asset.id, std::move(region));
                ++regionCount;
                continue;
            }

            if (asset.kind == talvisota::content::AssetKind::Definition)
            {
                const std::string extension = LowerExtension(asset.relativePath);
                if (extension != ".tmission" && extension != ".tfaction" &&
                    extension != ".tcharacter" && extension != ".tequipment")
                {
                    PrintDiagnostic(contentRoot / asset.relativePath, 0U, "format",
                                    "Definition extension has no implemented typed parser.");
                    return 1;
                }
                talvisota::content::AuthoredDefinition definition;
                std::vector<talvisota::content::AuthoredDefinitionDiagnostic> definitionDiagnostics;
                if (!talvisota::content::LoadAuthoredDefinition(contentRoot / asset.relativePath,
                                                                registry, definition, definitionDiagnostics))
                {
                    for (const auto& diagnostic : definitionDiagnostics)
                        PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
                    return 1;
                }
                if (definition.kind == talvisota::content::AuthoredDefinitionKind::Mission)
                {
                    const auto region = regions.find(definition.regionId);
                    if (region == regions.end())
                    {
                        PrintDiagnostic(contentRoot / asset.relativePath, 0U, "region_id",
                                        "Mission region must be a validated dependency loaded before the mission.");
                        return 1;
                    }
                    if (region->second.worldId != definition.worldId)
                    {
                        PrintDiagnostic(contentRoot / asset.relativePath, 0U, "region_id",
                                        "Mission region belongs to a different world than world_id.");
                        return 1;
                    }
                    for (const auto& objective : definition.objectives)
                    {
                        const auto placement = std::find_if(region->second.placements.begin(),
                            region->second.placements.end(), [&](const auto& candidate)
                            {
                                return candidate.id == objective.interactionTargetId;
                            });
                        const bool actorTarget = std::any_of(definition.actors.begin(),
                            definition.actors.end(), [&](const auto& actor) {
                                return actor.id == objective.interactionTargetId;
                            });
                        if (placement == region->second.placements.end() && !actorTarget)
                        {
                            PrintDiagnostic(contentRoot / asset.relativePath, 0U, "objective",
                                "Interaction target is neither a placement in the mission region nor an authored mission actor: " +
                                objective.interactionTargetId);
                            return 1;
                        }
                    }
                }
                ++authoredDefinitionCount;
                continue;
            }
            if (asset.kind != talvisota::content::AssetKind::Material ||
                LowerExtension(asset.relativePath) == ".mtl")
                continue;

            talvisota::content::MaterialDefinition material;
            std::vector<talvisota::content::MaterialDiagnostic> materialDiagnostics;
            if (!talvisota::content::LoadMaterialDefinition(contentRoot / asset.relativePath, registry,
                                                            material, materialDiagnostics))
            {
                for (const auto& diagnostic : materialDiagnostics)
                    PrintDiagnostic(diagnostic.file, diagnostic.line, diagnostic.field, diagnostic.message);
                return 1;
            }
            ++materialCount;
        }

        std::cout << "Validated " << registry.Assets().size() << " content assets, "
                  << materialCount << " typed materials, " << worldCount << " typed worlds, "
                  << authoredDefinitionCount << " typed mission/faction/character/equipment definitions, "
                  << regionCount << " validated world regions.\n";
        for (const std::size_t index : registry.LoadOrder())
            std::cout << "  " << registry.Assets()[index].id << '\n';
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Content validation failed: " << exception.what() << '\n';
        return 1;
    }
}
