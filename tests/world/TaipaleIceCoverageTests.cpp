#include "../support/ContentManifestFixture.hpp"
#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <unordered_map>
#include <vector>

namespace
{
using Position = std::array<float, 3>;
using Triangle = std::array<std::size_t, 3>;

struct IceMesh final
{
    std::vector<Position> vertices;
    std::vector<Triangle> triangles;
};

struct SurfacePoint final
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

void Require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

IceMesh LoadIceMesh(const std::filesystem::path& path)
{
    std::ifstream input(path);
    Require(static_cast<bool>(input), "Taipale ice mesh must be readable.");

    IceMesh mesh;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.starts_with("v "))
        {
            std::istringstream record(line.substr(2));
            Position position{};
            std::string trailing;
            Require(static_cast<bool>(record >> position[0] >> position[1] >> position[2]) &&
                    !(record >> trailing),
                    "Taipale ice vertex must contain exactly three coordinates.");
            Require(std::all_of(position.begin(), position.end(),
                        [](const float value) { return std::isfinite(value); }),
                    "Taipale ice vertex coordinates must be finite.");
            mesh.vertices.push_back(position);
        }
        else if (line.starts_with("f "))
        {
            std::istringstream record(line.substr(2));
            Triangle triangle{};
            std::size_t vertexCount = 0U;
            std::string token;
            while (record >> token)
            {
                Require(vertexCount < triangle.size(),
                        "Taipale ice faces must remain triangulated.");
                const std::size_t separator = token.find('/');
                const std::string_view indexText(token.data(),
                    separator == std::string::npos ? token.size() : separator);
                std::size_t oneBasedIndex = 0U;
                const auto [end, error] = std::from_chars(
                    indexText.data(), indexText.data() + indexText.size(), oneBasedIndex);
                Require(error == std::errc{} &&
                        end == indexText.data() + indexText.size() &&
                        oneBasedIndex > 0U,
                        "Taipale ice face must use a valid positive OBJ vertex index.");
                triangle[vertexCount++] = oneBasedIndex - 1U;
            }
            Require(vertexCount == triangle.size(),
                    "Every Taipale ice face must contain exactly three vertices.");
            mesh.triangles.push_back(triangle);
        }
    }
    Require(!input.bad(), "Taipale ice mesh read failed.");
    Require(mesh.vertices.size() == 130U && mesh.triangles.size() == 128U,
            "Taipale frozen river must retain its 130-vertex, 128-triangle ribbon mesh.");
    for (const auto& triangle : mesh.triangles)
        for (const std::size_t index : triangle)
            Require(index < mesh.vertices.size(),
                    "Taipale ice face references a missing vertex.");
    return mesh;
}

std::vector<SurfacePoint> ClipAgainstX(std::vector<SurfacePoint> polygon,
                                       const double boundary, const bool keepGreater)
{
    if (polygon.empty())
        return {};

    std::vector<SurfacePoint> clipped;
    clipped.reserve(polygon.size() + 1U);
    for (std::size_t i = 0U; i < polygon.size(); ++i)
    {
        const SurfacePoint& start = polygon[i];
        const SurfacePoint& end = polygon[(i + 1U) % polygon.size()];
        const bool startInside = keepGreater ? start.x >= boundary : start.x <= boundary;
        const bool endInside = keepGreater ? end.x >= boundary : end.x <= boundary;

        if (startInside && endInside)
        {
            clipped.push_back(end);
        }
        else if (startInside && !endInside)
        {
            const double t = (boundary - start.x) / (end.x - start.x);
            clipped.push_back({
                boundary,
                start.y + t * (end.y - start.y),
                start.z + t * (end.z - start.z)
            });
        }
        else if (!startInside && endInside)
        {
            const double t = (boundary - start.x) / (end.x - start.x);
            clipped.push_back({
                boundary,
                start.y + t * (end.y - start.y),
                start.z + t * (end.z - start.z)
            });
            clipped.push_back(end);
        }
    }
    return clipped;
}

const talvisota::world::WaterSurface* FindWaterSurface(
    const talvisota::world::WorldRegion& region, const float x, const float z)
{
    // Match FindWaterSurfaceAt exactly: a shared X boundary belongs to the
    // interval on its right, not the first rectangle that happens to contain it.
    const auto after = std::upper_bound(region.waterSurfaces.begin(), region.waterSurfaces.end(), x,
        [](const float coordinate, const auto& surface) {
            return coordinate < surface.minX;
        });
    if (after == region.waterSurfaces.begin())
        return nullptr;
    const auto& candidate = *(after - 1);
    return x <= candidate.maxX && z >= candidate.minZ && z <= candidate.maxZ
        ? &candidate : nullptr;
}

void VerifyWaterSample(const talvisota::content::WorldDefinition& world,
                       const talvisota::world::WorldRegion& region,
                       const SurfacePoint& point, std::size_t& sampleCount)
{
    const Position position{
        static_cast<float>(point.x),
        static_cast<float>(point.y),
        static_cast<float>(point.z)
    };
    for (std::size_t axis = 0U; axis < position.size(); ++axis)
    {
        Require(position[axis] >= world.boundsMin[axis] - 0.001F &&
                position[axis] <= world.boundsMax[axis] + 0.001F,
                "Every sampled Taipale ice point must remain inside world bounds.");
        Require(position[axis] >= region.boundsMin[axis] - 0.001F &&
                position[axis] <= region.boundsMax[axis] + 0.001F,
                "Every sampled Taipale ice point must remain inside region bounds.");
    }

    const auto* water = FindWaterSurface(region, position[0], position[2]);
    Require(water != nullptr,
            "Every sampled Taipale ice point must be covered by an authored water rectangle.");
    Require(std::abs(position[1] - water->elevation) <= 0.001F,
            "Rendered Taipale ice height must match the authored water elevation.");

    float sampledHeight = 0.0F;
    std::array<float, 3> sampledNormal{};
    std::string error;
    if (!talvisota::world::SampleTerrainHeight(region, position[0], position[2],
            sampledHeight, sampledNormal, error))
        throw std::runtime_error("Taipale ice collision sample failed: " + error);
    Require(std::abs(sampledHeight - water->elevation) <= 0.001F &&
            sampledNormal[1] > 0.999F,
            "Taipale ice collision sampling must return the same flat water surface as the mesh.");
    Require(talvisota::world::SampleTerrainDisplacement(region, position[0], position[2]) == 0.0F,
            "Authored Taipale water must remain free of snow displacement.");
    ++sampleCount;
}

bool FindIceCrossSectionBounds(const IceMesh& mesh, const double x,
                               double& minimumZ, double& maximumZ)
{
    std::vector<double> intersections;
    intersections.reserve(mesh.triangles.size() * 2U);
    for (const Triangle& triangle : mesh.triangles)
    {
        for (std::size_t edge = 0U; edge < triangle.size(); ++edge)
        {
            const Position& start = mesh.vertices[triangle[edge]];
            const Position& end = mesh.vertices[triangle[(edge + 1U) % triangle.size()]];
            const double deltaX = static_cast<double>(end[0]) - start[0];
            if (std::abs(deltaX) <= 1.0e-10)
            {
                if (std::abs(x - start[0]) <= 1.0e-7)
                {
                    intersections.push_back(start[2]);
                    intersections.push_back(end[2]);
                }
                continue;
            }

            const double t = (x - start[0]) / deltaX;
            if (t < -1.0e-9 || t > 1.0 + 1.0e-9)
                continue;
            const double clamped = std::clamp(t, 0.0, 1.0);
            intersections.push_back(static_cast<double>(start[2]) +
                clamped * (static_cast<double>(end[2]) - start[2]));
        }
    }
    if (intersections.empty())
        return false;
    const auto [minimum, maximum] = std::minmax_element(
        intersections.begin(), intersections.end());
    minimumZ = *minimum;
    maximumZ = *maximum;
    return true;
}

std::pair<double, double> VerifyWaterEnvelope(
    const IceMesh& mesh, const talvisota::world::WorldRegion& region)
{
    std::vector<double> vertexXs;
    vertexXs.reserve(mesh.vertices.size());
    for (const Position& vertex : mesh.vertices)
        vertexXs.push_back(vertex[0]);
    std::sort(vertexXs.begin(), vertexXs.end());
    vertexXs.erase(std::unique(vertexXs.begin(), vertexXs.end()), vertexXs.end());

    double maximumLowerOverhang = 0.0;
    double maximumUpperOverhang = 0.0;
    for (const auto& water : region.waterSurfaces)
    {
        std::vector<double> sampleXs{water.minX, water.maxX};
        const auto firstVertex = std::upper_bound(
            vertexXs.begin(), vertexXs.end(), static_cast<double>(water.minX) + 1.0e-7);
        const auto afterLastVertex = std::lower_bound(
            vertexXs.begin(), vertexXs.end(), static_cast<double>(water.maxX) - 1.0e-7);
        sampleXs.insert(sampleXs.end(), firstVertex, afterLastVertex);
        std::sort(sampleXs.begin(), sampleXs.end());
        sampleXs.erase(std::unique(sampleXs.begin(), sampleXs.end()), sampleXs.end());

        for (const double x : sampleXs)
        {
            double iceMinimumZ = 0.0;
            double iceMaximumZ = 0.0;
            Require(FindIceCrossSectionBounds(mesh, x, iceMinimumZ, iceMaximumZ),
                    "Every Taipale water strip must intersect the rendered ice ribbon.");
            const double lowerOverhang = iceMinimumZ - water.minZ;
            const double upperOverhang = water.maxZ - iceMaximumZ;
            Require(lowerOverhang >= -0.001 && upperOverhang >= -0.001,
                    "Every Taipale water strip must contain the complete ice cross-section.");
            maximumLowerOverhang = std::max(maximumLowerOverhang, lowerOverhang);
            maximumUpperOverhang = std::max(maximumUpperOverhang, upperOverhang);
        }
    }

    Require(maximumLowerOverhang <= 4.0 && maximumUpperOverhang <= 4.0,
            "Taipale water rectangles must not remove more than 4 m of snow terrain beyond the visible ice ribbon.");
    return {maximumLowerOverhang, maximumUpperOverhang};
}

void TestCompleteTaipaleIceTriangleCoverage()
{
    const std::filesystem::path root = TALVISOTA_SOURCE_DIR;
    talvisota::test_support::ContentManifestFixture manifestFixture(root, "taipale-ice-coverage");
    Require(manifestFixture.Ready(), manifestFixture.Error());

    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    Require(talvisota::content::LoadContentManifest(manifestFixture.ManifestPath(),
                root, registry, manifestDiagnostics),
            "Shipped content manifest must load for the Taipale ice coverage test.");

    talvisota::content::WorldDefinition world;
    std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
    Require(talvisota::content::LoadWorldDefinition(
                root / "content/worlds/taipale.tworld", registry, world, worldDiagnostics),
            "Taipale world definition must load for the ice coverage test.");

    talvisota::world::WorldRegion region;
    std::vector<talvisota::world::WorldRegionDiagnostic> regionDiagnostics;
    Require(talvisota::world::LoadWorldRegion(
                root / "content/worlds/taipale-riverfront.tregion",
                registry, world, region, regionDiagnostics),
            "Taipale riverfront region must load for the ice coverage test.");
    Require(region.waterSurfaces.size() == 2048U,
            "Taipale must retain its 2048 mesh-derived narrow frozen-river water strips.");

    const IceMesh mesh = LoadIceMesh(root / "assets/models/environment/taipale-ice.obj");
    const auto [maximumLowerOverhang, maximumUpperOverhang] = VerifyWaterEnvelope(mesh, region);
    const auto [minimumVertex, maximumVertex] = std::minmax_element(
        mesh.vertices.begin(), mesh.vertices.end(),
        [](const Position& left, const Position& right) { return left[0] < right[0]; });
    Require(region.waterSurfaces.front().minX <= (*minimumVertex)[0] + 0.001F &&
            region.waterSurfaces.back().maxX >= (*maximumVertex)[0] - 0.001F,
            "Taipale water intervals must span the complete frozen-river X extent.");
    for (std::size_t i = 1U; i < region.waterSurfaces.size(); ++i)
        Require(std::abs(region.waterSurfaces[i - 1U].maxX -
                         region.waterSurfaces[i].minX) <= 0.0001F,
                "Taipale water intervals must be contiguous along the river.");

    std::size_t runtimeSamples = 0U;
    for (const Position& vertex : mesh.vertices)
        VerifyWaterSample(world, region,
            {vertex[0], vertex[1], vertex[2]}, runtimeSamples);

    constexpr std::size_t Subdivisions = 8U;
    constexpr double SubdivisionScale = static_cast<double>(Subdivisions);
    std::size_t clippedPolygonPoints = 0U;
    for (const Triangle& indices : mesh.triangles)
    {
        const std::array<SurfacePoint, 3> triangle{{
            {mesh.vertices[indices[0]][0], mesh.vertices[indices[0]][1], mesh.vertices[indices[0]][2]},
            {mesh.vertices[indices[1]][0], mesh.vertices[indices[1]][1], mesh.vertices[indices[1]][2]},
            {mesh.vertices[indices[2]][0], mesh.vertices[indices[2]][1], mesh.vertices[indices[2]][2]}
        }};

        const float triangleMinimumX = std::min({static_cast<float>(triangle[0].x),
            static_cast<float>(triangle[1].x), static_cast<float>(triangle[2].x)});
        const float triangleMaximumX = std::max({static_cast<float>(triangle[0].x),
            static_cast<float>(triangle[1].x), static_cast<float>(triangle[2].x)});
        const auto firstWater = std::lower_bound(region.waterSurfaces.begin(),
            region.waterSurfaces.end(), triangleMinimumX,
            [](const auto& candidate, const float coordinate) {
                return candidate.maxX <= coordinate;
            });
        std::size_t intersectingSegments = 0U;
        for (auto water = firstWater;
             water != region.waterSurfaces.end() && water->minX < triangleMaximumX; ++water)
        {
            std::vector<SurfacePoint> clipped(triangle.begin(), triangle.end());
            clipped = ClipAgainstX(std::move(clipped), water->minX, true);
            clipped = ClipAgainstX(std::move(clipped), water->maxX, false);
            if (clipped.empty())
                continue;

            ++intersectingSegments;
            for (const SurfacePoint& point : clipped)
            {
                Require(point.z >= water->minZ - 0.001F &&
                        point.z <= water->maxZ + 0.001F,
                        "Every clipped Taipale ice triangle must fit inside its water rectangle.");
                VerifyWaterSample(world, region, point, runtimeSamples);
                ++clippedPolygonPoints;
            }
        }
        Require(intersectingSegments > 0U,
                "Every Taipale ice triangle must intersect at least one authored water interval.");

        for (std::size_t a = 0U; a <= Subdivisions; ++a)
        {
            for (std::size_t b = 0U; b <= Subdivisions - a; ++b)
            {
                const std::size_t c = Subdivisions - a - b;
                const double wa = static_cast<double>(a) / SubdivisionScale;
                const double wb = static_cast<double>(b) / SubdivisionScale;
                const double wc = static_cast<double>(c) / SubdivisionScale;
                const SurfacePoint point{
                    triangle[0].x * wa + triangle[1].x * wb + triangle[2].x * wc,
                    triangle[0].y * wa + triangle[1].y * wb + triangle[2].y * wc,
                    triangle[0].z * wa + triangle[1].z * wb + triangle[2].z * wc
                };
                VerifyWaterSample(world, region, point, runtimeSamples);
            }
        }
    }

    // Recreate the original 32-strip segment-04 lower-bank bound at an
    // interior point. First prove that this control is genuinely on the fixed
    // ice surface; otherwise a point that was already snow could make the
    // negative assertion pass without exercising the regression.
    constexpr float controlX = -1539.681F;
    constexpr float controlZ = 1377.28125F;
    const auto* originalControlWater = FindWaterSurface(region, controlX, controlZ);
    Require(originalControlWater != nullptr &&
            std::abs(originalControlWater->elevation - 4.60F) <= 0.001F,
            "The negative-control point must be covered by the corrected ice surface before mutation.");
    float originalControlHeight = 0.0F;
    std::array<float, 3> originalControlNormal{};
    std::string originalControlError;
    Require(talvisota::world::SampleTerrainHeight(region, controlX, controlZ,
                originalControlHeight, originalControlNormal, originalControlError) &&
            std::abs(originalControlHeight - originalControlWater->elevation) <= 0.001F &&
            originalControlNormal[1] > 0.999F,
            "The negative-control point must sample the corrected flat ice surface before mutation.");

    talvisota::world::WorldRegion brokenRegion = region;
    const auto afterControl = std::upper_bound(brokenRegion.waterSurfaces.begin(),
        brokenRegion.waterSurfaces.end(), controlX,
        [](const float coordinate, const auto& surface) {
            return coordinate < surface.minX;
        });
    Require(afterControl != brokenRegion.waterSurfaces.begin(),
            "The negative control must resolve to a Taipale water interval.");
    const auto controlIndex = static_cast<std::size_t>(
        afterControl - brokenRegion.waterSurfaces.begin() - 1);
    Require(controlIndex == static_cast<std::size_t>(
                originalControlWater - region.waterSurfaces.data()) &&
            controlX >= brokenRegion.waterSurfaces[controlIndex].minX &&
            controlX <= brokenRegion.waterSurfaces[controlIndex].maxX &&
            controlZ >= brokenRegion.waterSurfaces[controlIndex].minZ &&
            controlZ <= brokenRegion.waterSurfaces[controlIndex].maxZ,
            "The negative control must mutate the exact runtime-selected strip that originally covered it.");
    brokenRegion.waterSurfaces[controlIndex].minZ = 1377.84F;
    Require(brokenRegion.waterSurfaces[controlIndex].minZ > controlZ &&
            brokenRegion.waterSurfaces[controlIndex].minZ <=
                brokenRegion.waterSurfaces[controlIndex].maxZ,
            "The negative control must move the selected strip's lower bank beyond the sample point.");
    float brokenHeight = 0.0F;
    std::array<float, 3> brokenNormal{};
    std::string brokenError;
    Require(talvisota::world::SampleTerrainHeight(brokenRegion,
                controlX, controlZ, brokenHeight, brokenNormal, brokenError) &&
            (std::abs(brokenHeight - originalControlHeight) > 0.001F ||
                brokenNormal[1] < 0.999F),
            "The negative control must reproduce the former ice-to-snow sampler gap.");

    std::cout << "Verified Taipale frozen river: " << mesh.vertices.size()
              << " vertices, " << mesh.triangles.size() << " triangles, "
              << region.waterSurfaces.size() << " narrow water strips, "
              << clippedPolygonPoints << " exact clipped-polygon points and "
              << runtimeSamples << " runtime water samples; maximum terrain-cutout overhang "
              << maximumLowerOverhang << "/" << maximumUpperOverhang << " m.\n";
}


struct TerrainMesh final
{
    std::vector<Position> vertices;
    std::vector<Triangle> triangles;
};

TerrainMesh LoadTerrainMesh(const std::filesystem::path& path)
{
    std::ifstream input(path);
    Require(static_cast<bool>(input), "Taipale terrain mesh must be readable.");

    TerrainMesh mesh;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.starts_with("v "))
        {
            std::istringstream record(line.substr(2));
            Position position{};
            std::string trailing;
            Require(static_cast<bool>(record >> position[0] >> position[1] >> position[2]) &&
                    !(record >> trailing),
                    "Taipale terrain vertex must contain exactly three coordinates.");
            Require(std::all_of(position.begin(), position.end(),
                        [](const float value) { return std::isfinite(value); }),
                    "Taipale terrain vertex coordinates must be finite.");
            mesh.vertices.push_back(position);
        }
        else if (line.starts_with("f "))
        {
            std::istringstream record(line.substr(2));
            Triangle triangle{};
            std::size_t vertexCount = 0U;
            std::string token;
            while (record >> token)
            {
                Require(vertexCount < triangle.size(),
                        "Taipale terrain faces must remain triangulated.");
                const std::size_t separator = token.find('/');
                const std::string_view indexText(token.data(),
                    separator == std::string::npos ? token.size() : separator);
                std::size_t oneBasedIndex = 0U;
                const auto [end, error] = std::from_chars(
                    indexText.data(), indexText.data() + indexText.size(), oneBasedIndex);
                Require(error == std::errc{} &&
                        end == indexText.data() + indexText.size() &&
                        oneBasedIndex > 0U,
                        "Taipale terrain face must use a valid positive OBJ vertex index.");
                triangle[vertexCount++] = oneBasedIndex - 1U;
            }
            Require(vertexCount == triangle.size(),
                    "Every Taipale terrain face must contain exactly three vertices.");
            mesh.triangles.push_back(triangle);
        }
    }
    Require(!input.bad(), "Taipale terrain mesh read failed.");
    Require(mesh.vertices.size() == 4225U && mesh.triangles.size() == 8192U,
            "Taipale terrain must retain its 65x65 grid and 8192 source triangles.");
    for (const Triangle& triangle : mesh.triangles)
        for (const std::size_t index : triangle)
            Require(index < mesh.vertices.size(),
                    "Taipale terrain face references a missing vertex.");
    return mesh;
}

std::vector<SurfacePoint> ClipTerrainPolygon(const std::vector<SurfacePoint>& polygon,
    const bool clipX, const double boundary, const bool keepGreater)
{
    if (polygon.size() < 3U)
        return {};

    const auto coordinate = [clipX](const SurfacePoint& point) {
        return clipX ? point.x : point.z;
    };
    const auto inside = [&](const SurfacePoint& point) {
        return keepGreater ? coordinate(point) >= boundary : coordinate(point) <= boundary;
    };

    std::vector<SurfacePoint> output;
    output.reserve(polygon.size() + 1U);
    const SurfacePoint* previous = &polygon.back();
    bool previousInside = inside(*previous);
    for (const SurfacePoint& current : polygon)
    {
        const bool currentInside = inside(current);
        if (currentInside != previousInside)
        {
            const double previousCoordinate = coordinate(*previous);
            const double currentCoordinate = coordinate(current);
            const double denominator = currentCoordinate - previousCoordinate;
            if (std::abs(denominator) > 1.0e-12)
            {
                const double t = std::clamp((boundary - previousCoordinate) / denominator, 0.0, 1.0);
                SurfacePoint intersection{
                    previous->x + (current.x - previous->x) * t,
                    previous->y + (current.y - previous->y) * t,
                    previous->z + (current.z - previous->z) * t
                };
                if (clipX)
                    intersection.x = boundary;
                else
                    intersection.z = boundary;
                output.push_back(intersection);
            }
        }
        if (currentInside)
            output.push_back(current);
        previous = &current;
        previousInside = currentInside;
    }
    return output.size() >= 3U ? output : std::vector<SurfacePoint>{};
}

std::vector<std::vector<SurfacePoint>> SubtractWaterRectangle(
    const std::vector<SurfacePoint>& polygon,
    const talvisota::world::WaterSurface& water)
{
    std::vector<std::vector<SurfacePoint>> outsidePieces;
    std::vector<SurfacePoint> remaining = polygon;
    struct Boundary final
    {
        bool clipX;
        double value;
        bool keepGreater;
    };
    const std::array<Boundary, 4> boundaries{{
        {true, water.minX, true},
        {true, water.maxX, false},
        {false, water.minZ, true},
        {false, water.maxZ, false}
    }};
    for (const Boundary& boundary : boundaries)
    {
        auto outside = ClipTerrainPolygon(
            remaining, boundary.clipX, boundary.value, !boundary.keepGreater);
        if (outside.size() >= 3U)
            outsidePieces.push_back(std::move(outside));
        remaining = ClipTerrainPolygon(
            remaining, boundary.clipX, boundary.value, boundary.keepGreater);
        if (remaining.size() < 3U)
            break;
    }
    return outsidePieces;
}

struct ClippedTerrain final
{
    std::size_t vertexCount = 0U;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::size_t sourceTrianglesTouchedByWater = 0U;
};

ClippedTerrain BuildWaterClippedTerrain(
    const TerrainMesh& mesh, const talvisota::world::WorldRegion& region)
{
    ClippedTerrain result;
    result.triangles.reserve(mesh.triangles.size());
    std::size_t nextVertex = mesh.vertices.size();

    for (const Triangle& sourceIndices : mesh.triangles)
    {
        std::vector<SurfacePoint> sourcePolygon;
        sourcePolygon.reserve(3U);
        double minimumX = std::numeric_limits<double>::max();
        double maximumX = std::numeric_limits<double>::lowest();
        double minimumZ = std::numeric_limits<double>::max();
        double maximumZ = std::numeric_limits<double>::lowest();
        for (const std::size_t index : sourceIndices)
        {
            const Position& vertex = mesh.vertices[index];
            sourcePolygon.push_back({vertex[0], vertex[1], vertex[2]});
            minimumX = std::min(minimumX, static_cast<double>(vertex[0]));
            maximumX = std::max(maximumX, static_cast<double>(vertex[0]));
            minimumZ = std::min(minimumZ, static_cast<double>(vertex[2]));
            maximumZ = std::max(maximumZ, static_cast<double>(vertex[2]));
        }

        std::vector<std::vector<SurfacePoint>> fragments{std::move(sourcePolygon)};
        bool overlapsWater = false;
        const auto firstWater = std::lower_bound(
            region.waterSurfaces.begin(), region.waterSurfaces.end(), minimumX,
            [](const auto& candidate, const double coordinate) {
                return candidate.maxX <= coordinate;
            });
        for (auto water = firstWater;
             water != region.waterSurfaces.end() && water->minX < maximumX; ++water)
        {
            if (maximumZ <= water->minZ || minimumZ >= water->maxZ)
                continue;
            overlapsWater = true;
            std::vector<std::vector<SurfacePoint>> nextFragments;
            for (const auto& fragment : fragments)
            {
                auto outside = SubtractWaterRectangle(fragment, *water);
                for (auto& piece : outside)
                    nextFragments.push_back(std::move(piece));
            }
            fragments.swap(nextFragments);
            if (fragments.empty())
                break;
        }

        if (!overlapsWater)
        {
            result.triangles.push_back({
                static_cast<std::uint32_t>(sourceIndices[0]),
                static_cast<std::uint32_t>(sourceIndices[1]),
                static_cast<std::uint32_t>(sourceIndices[2])
            });
            continue;
        }
        ++result.sourceTrianglesTouchedByWater;
        for (const auto& polygon : fragments)
        {
            if (polygon.size() < 3U)
                continue;
            double twiceArea = 0.0;
            for (std::size_t i = 0U; i < polygon.size(); ++i)
            {
                const auto& a = polygon[i];
                const auto& b = polygon[(i + 1U) % polygon.size()];
                twiceArea += a.x * b.z - b.x * a.z;
            }
            if (std::abs(twiceArea) <= 1.0e-5)
                continue;
            Require(nextVertex + polygon.size() <=
                        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
                    "Taipale clipped terrain must remain addressable by 32-bit indices.");
            const auto first = static_cast<std::uint32_t>(nextVertex);
            nextVertex += polygon.size();
            for (std::size_t i = 1U; i + 1U < polygon.size(); ++i)
                result.triangles.push_back({
                    first,
                    first + static_cast<std::uint32_t>(i),
                    first + static_cast<std::uint32_t>(i + 1U)
                });
        }
    }

    // Match the runtime's compaction: retain only vertices referenced by the
    // post-cutout index buffer, then make the IDs dense before subdivision.
    std::unordered_map<std::uint32_t, std::uint32_t> remap;
    remap.reserve(result.triangles.size());
    for (auto& triangle : result.triangles)
    {
        for (std::uint32_t& index : triangle)
        {
            const auto [entry, inserted] = remap.emplace(
                index, static_cast<std::uint32_t>(remap.size()));
            (void)inserted;
            index = entry->second;
        }
    }
    result.vertexCount = remap.size();
    return result;
}

struct RefinementPass final
{
    std::size_t vertices = 0U;
    std::size_t triangles = 0U;
    std::size_t indices = 0U;
};

std::vector<RefinementPass> EstimateMidpointRefinement(
    const ClippedTerrain& terrain, const std::size_t passCount)
{
    std::vector<RefinementPass> result;
    auto triangles = terrain.triangles;
    std::size_t vertexCount = terrain.vertexCount;
    result.reserve(passCount);

    for (std::size_t pass = 0U; pass < passCount; ++pass)
    {
        std::unordered_map<std::uint64_t, std::uint32_t> midpointIndices;
        midpointIndices.reserve(triangles.size() * 2U);
        std::vector<std::array<std::uint32_t, 3>> refined;
        refined.reserve(triangles.size() * 4U);
        const auto midpoint = [&](std::uint32_t a, std::uint32_t b) {
            if (a > b)
                std::swap(a, b);
            const std::uint64_t key = (static_cast<std::uint64_t>(a) << 32U) | b;
            const auto found = midpointIndices.find(key);
            if (found != midpointIndices.end())
                return found->second;
            const auto index = static_cast<std::uint32_t>(
                vertexCount + midpointIndices.size());
            midpointIndices.emplace(key, index);
            return index;
        };

        for (const auto& triangle : triangles)
        {
            const std::uint32_t a = triangle[0];
            const std::uint32_t b = triangle[1];
            const std::uint32_t c = triangle[2];
            const std::uint32_t ab = midpoint(a, b);
            const std::uint32_t bc = midpoint(b, c);
            const std::uint32_t ca = midpoint(c, a);
            refined.push_back({a, ab, ca});
            refined.push_back({ab, b, bc});
            refined.push_back({ca, bc, c});
            refined.push_back({ab, bc, ca});
        }
        vertexCount += midpointIndices.size();
        triangles.swap(refined);
        result.push_back({vertexCount, triangles.size(), triangles.size() * 3U});
    }
    return result;
}

void TestTaipaleTerrainRefinementBudgetEnvelope()
{
    const std::filesystem::path root = TALVISOTA_SOURCE_DIR;
    talvisota::test_support::ContentManifestFixture manifestFixture(root, "taipale-refinement-budget");
    Require(manifestFixture.Ready(), manifestFixture.Error());

    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> manifestDiagnostics;
    Require(talvisota::content::LoadContentManifest(
                manifestFixture.ManifestPath(), root, registry, manifestDiagnostics),
            "Shipped content manifest must load for the Taipale refinement-budget test.");
    talvisota::content::WorldDefinition world;
    std::vector<talvisota::content::WorldDiagnostic> worldDiagnostics;
    Require(talvisota::content::LoadWorldDefinition(
                root / "content/worlds/taipale.tworld", registry, world, worldDiagnostics),
            "Taipale world definition must load for the refinement-budget test.");
    talvisota::world::WorldRegion region;
    std::vector<talvisota::world::WorldRegionDiagnostic> regionDiagnostics;
    Require(talvisota::world::LoadWorldRegion(
                root / "content/worlds/taipale-riverfront.tregion",
                registry, world, region, regionDiagnostics),
            "Taipale riverfront region must load for the refinement-budget test.");

    Require(region.terrain.rows == 65U && region.terrain.columns == 65U &&
            std::abs(region.terrain.spacingX - 64.0F) <= 1.0e-4F &&
            std::abs(region.terrain.spacingZ - 64.0F) <= 1.0e-4F,
            "Taipale refinement must be budgeted against its physical 64 m source-grid spacing.");
    const TerrainMesh source = LoadTerrainMesh(
        root / "assets/models/terrain/taipale-riverfront.obj");
    const ClippedTerrain clipped = BuildWaterClippedTerrain(source, region);
    Require(clipped.sourceTrianglesTouchedByWater == 474U &&
            clipped.triangles.size() == 20430U &&
            clipped.vertexCount == 29708U,
            "Taipale water separation must retain its measured clipped-mesh complexity baseline.");

    constexpr std::size_t MaximumVertices = 1000000U;
    const auto passes = EstimateMidpointRefinement(clipped, 3U);
    Require(passes.size() == 3U &&
            passes[0].vertices == 73388U && passes[0].triangles == 81720U &&
            passes[1].vertices == 222038U && passes[1].triangles == 326880U &&
            passes[2].vertices == 764498U && passes[2].triangles == 1307520U,
            "Taipale midpoint-refinement cost must remain reproducible from the shipped terrain and water strips.");
    Require(passes[0].vertices + passes[0].indices < MaximumVertices &&
            passes[1].vertices + passes[1].indices > MaximumVertices &&
            passes[2].vertices < MaximumVertices,
            "The runtime's vertices-plus-indices guard must be shown to reject a third pass that still fits the actual vertex budget.");

    const double sourceCellDiagonal = std::hypot(
        static_cast<double>(region.terrain.spacingX),
        static_cast<double>(region.terrain.spacingZ));
    const double afterTwoPasses = sourceCellDiagonal / 4.0;
    const double afterThreePasses = sourceCellDiagonal / 8.0;
    Require(afterTwoPasses > 2.2 && afterThreePasses > 2.2 &&
            afterThreePasses < afterTwoPasses,
            "Taipale's 64 m cell diagonal requires the third refinement pass to reduce the longest edge, while remaining above the 2.2 m target.");

    std::cout << "Taipale refinement budget: " << source.vertices.size()
        << " source vertices / " << source.triangles.size() << " source triangles; "
        << clipped.vertexCount << " vertices / " << clipped.triangles.size()
        << " triangles after water separation; after two passes "
        << passes[1].vertices << " vertices / " << passes[1].triangles
        << " triangles; after three passes " << passes[2].vertices
        << " vertices / " << passes[2].triangles
        << " triangles; before pass 3 the runtime guard sees "
        << (passes[1].vertices + passes[1].indices)
        << " combined vertex/index entries although the completed pass 3 remains below one million vertices.";
}

} // namespace

int main()
{
    try
    {
        TestCompleteTaipaleIceTriangleCoverage();
        TestTaipaleTerrainRefinementBudgetEnvelope();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Taipale ice coverage test failure: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
