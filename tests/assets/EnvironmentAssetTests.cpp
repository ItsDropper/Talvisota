#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

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
} // namespace

int main()
{
    try
    {
        TestSakkolaTerrainMaterialTiling();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Environment asset test failure: " << exception.what() << '\n';
        return 1;
    }
}
