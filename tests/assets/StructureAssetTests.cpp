#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
void Require(const bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::size_t ParsePositiveIndex(const std::string_view value)
{
    Require(!value.empty(), "Church OBJ contains an empty face index.");
    std::size_t consumed = 0;
    const auto index = std::stoul(std::string(value), &consumed);
    Require(consumed == value.size() && index > 0U,
            "Church OBJ face indices must be positive integers.");
    return static_cast<std::size_t>(index);
}

struct GeometrySummary
{
    std::size_t positions = 0;
    std::size_t textureCoordinates = 0;
    std::size_t normals = 0;
    std::size_t triangles = 0;
    std::array<double, 3> minimum{
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()};
    std::array<double, 3> maximum{
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()};
    std::map<std::string, std::size_t> materialTriangles;
};

GeometrySummary InspectChurch(const std::filesystem::path& path)
{
    std::ifstream input(path);
    Require(static_cast<bool>(input), "Could not open the Sakkola church model.");

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line))
        lines.push_back(line);

    GeometrySummary summary;
    for (const auto& item : lines)
    {
        if (item.starts_with("v "))
        {
            std::istringstream values(item.substr(2));
            std::array<double, 3> position{};
            Require(static_cast<bool>(values >> position[0] >> position[1] >> position[2]),
                    "Church OBJ contains a malformed position.");
            for (std::size_t axis = 0; axis < 3U; ++axis)
            {
                Require(std::isfinite(position[axis]), "Church OBJ contains a non-finite position.");
                summary.minimum[axis] = std::min(summary.minimum[axis], position[axis]);
                summary.maximum[axis] = std::max(summary.maximum[axis], position[axis]);
            }
            ++summary.positions;
        }
        else if (item.starts_with("vt "))
        {
            ++summary.textureCoordinates;
        }
        else if (item.starts_with("vn "))
        {
            std::istringstream values(item.substr(3));
            double x = 0.0, y = 0.0, z = 0.0;
            Require(static_cast<bool>(values >> x >> y >> z) &&
                    std::isfinite(x) && std::isfinite(y) && std::isfinite(z),
                    "Church OBJ contains a malformed normal.");
            const double length = std::sqrt(x * x + y * y + z * z);
            Require(std::abs(length - 1.0) <= 0.002,
                    "Church OBJ normals must be normalized.");
            ++summary.normals;
        }
    }

    std::string activeMaterial;
    for (const auto& item : lines)
    {
        if (item.starts_with("usemtl "))
        {
            activeMaterial = item.substr(7);
            Require(!activeMaterial.empty(), "Church OBJ contains an empty material name.");
        }
        else if (item.starts_with("f "))
        {
            Require(!activeMaterial.empty(), "Church OBJ face appears before a material assignment.");
            std::istringstream face(item.substr(2));
            std::string token;
            std::size_t corners = 0;
            while (face >> token)
            {
                const auto firstSlash = token.find('/');
                const auto secondSlash = token.find('/', firstSlash == std::string::npos ? 0U : firstSlash + 1U);
                Require(firstSlash != std::string::npos && secondSlash != std::string::npos,
                        "Church OBJ faces must provide position/UV/normal indices.");
                const auto position = ParsePositiveIndex(std::string_view(token).substr(0, firstSlash));
                const auto uv = ParsePositiveIndex(std::string_view(token).substr(
                    firstSlash + 1U, secondSlash - firstSlash - 1U));
                const auto normal = ParsePositiveIndex(std::string_view(token).substr(secondSlash + 1U));
                Require(position <= summary.positions && uv <= summary.textureCoordinates &&
                        normal <= summary.normals,
                        "Church OBJ face references an out-of-range position, UV, or normal.");
                ++corners;
            }
            Require(corners == 3U, "Church OBJ must use triangulated faces.");
            ++summary.triangles;
            ++summary.materialTriangles[activeMaterial];
        }
    }
    return summary;
}

void TestChurchBelfrySilhouette()
{
    const auto path = std::filesystem::path(TALVISOTA_SOURCE_DIR) /
        "assets/models/environment/church.obj";
    const auto mesh = InspectChurch(path);

    Require(mesh.positions >= 2600U && mesh.normals >= 2600U &&
            mesh.textureCoordinates >= 2800U && mesh.triangles >= 1400U,
            "Church model must retain its authored architectural geometry and added belfry detail.");
    Require(mesh.minimum[0] <= -6.0 && mesh.maximum[0] >= 6.0 &&
            mesh.minimum[2] <= -5.0 && mesh.maximum[2] >= 5.0 &&
            mesh.minimum[1] <= 0.001 && mesh.maximum[1] >= 17.19,
            "Church model footprint and tower height must remain in metre-scale bounds.");

    const auto count = [&mesh](const char* material) {
        const auto found = mesh.materialTriangles.find(material);
        return found == mesh.materialTriangles.end() ? 0U : found->second;
    };
    Require(count("church_boards") >= 1000U && count("window_glass") >= 48U,
            "Church body and window materials must remain present.");
    Require(count("tower_wood") >= 64U,
            "The belfry must retain its four-post frame and open timber louver courses.");
    Require(count("roof_wood") >= 12U && count("roof_snow") >= 12U,
            "The belfry must have a distinct pitched timber roof and snow cap.");

    std::cout << "Verified Sakkola church geometry: " << mesh.positions << " positions, "
        << mesh.triangles << " triangles, " << mesh.materialTriangles.size()
        << " material groups; belfry louvers and snow-capped roof are present.\n";
}
} // namespace

int main()
{
    try
    {
        TestChurchBelfrySilhouette();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Structure asset test failure: " << exception.what() << '\n';
        return 1;
    }
}
