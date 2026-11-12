#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void Require(const bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void TestSakkolaTerrainMaterialTiling()
{
    const std::filesystem::path terrainPath =
        std::filesystem::path(TALVISOTA_SOURCE_DIR) /
        "assets/models/terrain/sakkola-church-village.obj";
    std::ifstream input(terrainPath);
    Require(static_cast<bool>(input), "Could not open the Sakkola terrain source mesh.");

    constexpr double expectedWorldSpan = 4096.0;
    constexpr double expectedTileWorldSpan = 40.0;
    constexpr double expectedUvSpan = expectedWorldSpan / expectedTileWorldSpan;
    double minimumU = std::numeric_limits<double>::max();
    double maximumU = std::numeric_limits<double>::lowest();
    double minimumV = std::numeric_limits<double>::max();
    double maximumV = std::numeric_limits<double>::lowest();
    std::size_t uvCount = 0;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.starts_with("vt "))
            continue;

        std::istringstream values(line.substr(3));
        double u = 0.0;
        double v = 0.0;
        Require(static_cast<bool>(values >> u >> v) && std::isfinite(u) && std::isfinite(v),
                "Sakkola terrain contains a malformed texture coordinate.");
        minimumU = std::min(minimumU, u);
        maximumU = std::max(maximumU, u);
        minimumV = std::min(minimumV, v);
        maximumV = std::max(maximumV, v);
        ++uvCount;
    }

    Require(uvCount == 31329U,
            "Sakkola terrain UV count must continue to match its 177x177 source grid.");
    Require(std::abs(minimumU) <= 0.0001 && std::abs(minimumV) <= 0.0001 &&
            std::abs((maximumU - minimumU) - expectedUvSpan) <= 0.0001 &&
            std::abs((maximumV - minimumV) - expectedUvSpan) <= 0.0001,
            "Sakkola terrain snow materials must repeat every 40 metres, not once across the 4 km region.");
    std::cout << "Verified Sakkola terrain PBR UV tiling: 40 m per repeat across 4096 m.\n";
}

void TestTaipaleIceTextureDensity()
{
    const std::filesystem::path icePath =
        std::filesystem::path(TALVISOTA_SOURCE_DIR) /
        "assets/models/environment/taipale-ice.obj";
    std::ifstream input(icePath);
    Require(static_cast<bool>(input), "Could not open the Taipale frozen-river source mesh.");

    std::vector<std::array<double, 3>> vertices;
    std::vector<std::array<double, 2>> texcoords;
    std::size_t faceCount = 0U;
    std::string line;
    while (std::getline(input, line))
    {
        if (line.starts_with("v "))
        {
            std::istringstream values(line.substr(2));
            std::array<double, 3> position{};
            Require(static_cast<bool>(values >> position[0] >> position[1] >> position[2]) &&
                    std::all_of(position.begin(), position.end(), [](const double value) {
                        return std::isfinite(value);
                    }),
                    "Taipale ice contains a malformed vertex.");
            vertices.push_back(position);
        }
        else if (line.starts_with("vt "))
        {
            std::istringstream values(line.substr(3));
            std::array<double, 2> uv{};
            Require(static_cast<bool>(values >> uv[0] >> uv[1]) &&
                    std::all_of(uv.begin(), uv.end(), [](const double value) {
                        return std::isfinite(value);
                    }),
                    "Taipale ice contains a malformed texture coordinate.");
            texcoords.push_back(uv);
        }
        else if (line.starts_with("f "))
        {
            std::istringstream corners(line.substr(2));
            std::string corner;
            std::size_t cornerCount = 0U;
            while (corners >> corner)
            {
                const std::size_t separator = corner.find('/');
                Require(separator != std::string::npos,
                        "Taipale ice face must reference a texture coordinate.");
                const std::size_t nextSeparator = corner.find('/', separator + 1U);
                const std::string vertexText = corner.substr(0U, separator);
                const std::string uvText = corner.substr(separator + 1U,
                    nextSeparator == std::string::npos ? std::string::npos :
                    nextSeparator - separator - 1U);
                const std::size_t vertexIndex = static_cast<std::size_t>(std::stoul(vertexText));
                const std::size_t uvIndex = static_cast<std::size_t>(std::stoul(uvText));
                Require(vertexIndex > 0U && vertexIndex <= vertices.size() &&
                        uvIndex > 0U && uvIndex <= texcoords.size() &&
                        vertexIndex == uvIndex,
                        "Taipale ice face has an invalid or non-corresponding vertex/UV index.");
                ++cornerCount;
            }
            Require(cornerCount == 3U, "Taipale ice must remain triangulated.");
            ++faceCount;
        }
    }

    Require(vertices.size() == 130U && texcoords.size() == vertices.size() &&
            faceCount == 128U,
            "Taipale ice source topology or one-to-one UV indexing changed unexpectedly.");
    Require(vertices.size() % 2U == 0U,
            "Taipale ice cross-sections must contain exactly two vertices.");

    constexpr double textureRepeatMetres = 64.0;
    constexpr double uvTolerance = 0.00002;
    double cumulativeDistance = 0.0;
    for (std::size_t section = 0U; section < vertices.size() / 2U; ++section)
    {
        const auto& left = vertices[section * 2U];
        const auto& right = vertices[section * 2U + 1U];
        const double centerX = (left[0] + right[0]) * 0.5;
        const double centerZ = (left[2] + right[2]) * 0.5;
        const double channelWidth = std::hypot(left[0] - right[0], left[2] - right[2]);
        if (section > 0U)
        {
            const auto& previousLeft = vertices[(section - 1U) * 2U];
            const auto& previousRight = vertices[(section - 1U) * 2U + 1U];
            const double previousX = (previousLeft[0] + previousRight[0]) * 0.5;
            const double previousZ = (previousLeft[2] + previousRight[2]) * 0.5;
            cumulativeDistance += std::hypot(centerX - previousX, centerZ - previousZ);
        }

        const double expectedU = cumulativeDistance / textureRepeatMetres;
        const double expectedV = channelWidth / textureRepeatMetres;
        const auto& firstUv = texcoords[section * 2U];
        const auto& secondUv = texcoords[section * 2U + 1U];
        Require(channelWidth > 0.0 &&
                std::abs(firstUv[0] - expectedU) <= uvTolerance &&
                std::abs(secondUv[0] - expectedU) <= uvTolerance &&
                std::abs(firstUv[1]) <= uvTolerance &&
                std::abs(secondUv[1] - expectedV) <= uvTolerance,
                "Taipale ice must tile at 64 metres using river arc length and local channel width.");
    }

    Require(cumulativeDistance > 4000.0,
            "Taipale ice centreline unexpectedly lost its full riverfront extent.");
    std::cout << "Verified Taipale ice PBR UV density: 64 m per repeat along the river and across the channel.\n";
}
} // namespace

int main()
{
    try
    {
        TestSakkolaTerrainMaterialTiling();
        TestTaipaleIceTextureDensity();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Environment asset test failure: " << exception.what() << '\n';
        return 1;
    }
}
