#include "../support/ContentManifestFixture.hpp"
#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/WorldDefinition.hpp"
#include "talvisota/world/WorldRegion.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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
    const auto found = std::find_if(region.waterSurfaces.begin(), region.waterSurfaces.end(),
        [x, z](const auto& surface) {
            return x >= surface.minX && x <= surface.maxX &&
                   z >= surface.minZ && z <= surface.maxZ;
        });
    return found == region.waterSurfaces.end() ? nullptr : &*found;
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
    Require(region.waterSurfaces.size() == 32U,
            "Taipale must retain its 32 segmented frozen-river water rectangles.");

    const IceMesh mesh = LoadIceMesh(root / "assets/models/environment/taipale-ice.obj");
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

        std::size_t intersectingSegments = 0U;
        for (const auto& water : region.waterSurfaces)
        {
            std::vector<SurfacePoint> clipped(triangle.begin(), triangle.end());
            clipped = ClipAgainstX(std::move(clipped), water.minX, true);
            clipped = ClipAgainstX(std::move(clipped), water.maxX, false);
            if (clipped.empty())
                continue;

            ++intersectingSegments;
            for (const SurfacePoint& point : clipped)
            {
                Require(point.z >= water.minZ - 0.001F &&
                        point.z <= water.maxZ + 0.001F,
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

    // Recreate the original segment-04 lower bank bound. This known triangle
    // interior point must fall back to snow, proving the regression catches
    // the pre-fix sampler hole rather than merely validating a loose mesh count.
    talvisota::world::WorldRegion brokenRegion = region;
    brokenRegion.waterSurfaces[3].minZ = 1377.84F;
    float brokenHeight = 0.0F;
    std::array<float, 3> brokenNormal{};
    std::string brokenError;
    Require(talvisota::world::SampleTerrainHeight(brokenRegion,
                -1539.681F, 1377.28125F, brokenHeight, brokenNormal, brokenError) &&
            (std::abs(brokenHeight - 4.60F) > 0.001F || brokenNormal[1] < 0.999F),
            "The negative control must reproduce the former ice-to-snow sampler gap.");

    std::cout << "Verified Taipale frozen river: " << mesh.vertices.size()
              << " vertices, " << mesh.triangles.size() << " triangles, "
              << clippedPolygonPoints << " exact clipped-polygon points and "
              << runtimeSamples << " runtime water samples.\n";
}
} // namespace

int main()
{
    try
    {
        TestCompleteTaipaleIceTriangleCoverage();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Taipale ice coverage test failure: " << exception.what() << '\n';
        return 1;
    }
    return 0;
}
