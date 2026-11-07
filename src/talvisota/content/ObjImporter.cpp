#include "talvisota/content/ObjImporter.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace talvisota::content
{
namespace
{
constexpr std::uint64_t MaximumObjBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::size_t MaximumLineBytes = 1024U * 1024U;
constexpr std::size_t MaximumFaceCorners = 4096U;
constexpr std::size_t MaximumVertices = 10'000'000U;
constexpr std::size_t MaximumIndices = 30'000'000U;

void Add(std::vector<ModelDiagnostic>& diagnostics, const std::filesystem::path& path,
         std::size_t line, std::string field, std::string message)
{
    diagnostics.push_back({path, line, std::move(field), std::move(message)});
}

[[nodiscard]] std::string_view Trim(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
        value.remove_prefix(1U);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1U);
    return value;
}

[[nodiscard]] std::vector<std::string_view> Tokens(std::string_view line)
{
    std::vector<std::string_view> result;
    while (!(line = Trim(line)).empty())
    {
        const std::size_t end = line.find_first_of(" \t\r");
        if (end == std::string_view::npos)
        {
            result.push_back(line);
            break;
        }
        result.push_back(line.substr(0U, end));
        line.remove_prefix(end + 1U);
    }
    return result;
}

[[nodiscard]] bool ParseFloat(std::string_view token, float& value)
{
    if (token.empty())
        return false;
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value,
                                        std::chars_format::general);
    return result.ec == std::errc{} && result.ptr == token.data() + token.size() &&
           std::isfinite(value);
}

[[nodiscard]] bool ParseIndex(std::string_view token, std::size_t count, int& output)
{
    if (token.empty())
        return false;
    int raw = 0;
    const auto result = std::from_chars(token.data(), token.data() + token.size(), raw);
    if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || raw == 0)
        return false;
    const std::int64_t resolved = raw > 0
        ? static_cast<std::int64_t>(raw) - 1
        : static_cast<std::int64_t>(count) + raw;
    if (resolved < 0 || resolved >= static_cast<std::int64_t>(count))
        return false;
    output = static_cast<int>(resolved);
    return true;
}

struct Corner final
{
    int position = -1;
    int texCoord = -1;
    int normal = -1;
};

[[nodiscard]] bool ParseCorner(std::string_view token, std::size_t positionCount,
                               std::size_t texCoordCount, std::size_t normalCount,
                               Corner& output)
{
    const std::size_t firstSlash = token.find('/');
    const std::size_t secondSlash = firstSlash == std::string_view::npos
        ? std::string_view::npos : token.find('/', firstSlash + 1U);
    const std::string_view position = firstSlash == std::string_view::npos
        ? token : token.substr(0U, firstSlash);
    if (!ParseIndex(position, positionCount, output.position))
        return false;
    if (firstSlash == std::string_view::npos)
        return true;

    const std::string_view texCoord = token.substr(firstSlash + 1U,
        (secondSlash == std::string_view::npos ? token.size() : secondSlash) - firstSlash - 1U);
    if (!texCoord.empty() && !ParseIndex(texCoord, texCoordCount, output.texCoord))
        return false;
    if (secondSlash != std::string_view::npos)
    {
        const std::string_view normal = token.substr(secondSlash + 1U);
        if (!ParseIndex(normal, normalCount, output.normal))
            return false;
    }
    return true;
}

struct Point2 final { double x = 0.0; double y = 0.0; };

[[nodiscard]] double Cross(const Point2& a, const Point2& b, const Point2& c) noexcept
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

[[nodiscard]] bool PointInTriangle(const Point2& point, const Point2& a,
                                   const Point2& b, const Point2& c,
                                   double orientation) noexcept
{
    constexpr double epsilon = 1.0e-10;
    return Cross(a, b, point) * orientation >= -epsilon &&
           Cross(b, c, point) * orientation >= -epsilon &&
           Cross(c, a, point) * orientation >= -epsilon;
}

[[nodiscard]] bool Triangulate(const std::vector<Corner>& corners,
                               const std::vector<std::array<float, 3>>& positions,
                               std::vector<std::array<std::size_t, 3>>& triangles,
                               std::array<float, 3>& faceNormal)
{
    std::array<double, 3> normal{};
    for (std::size_t index = 0; index < corners.size(); ++index)
    {
        const auto& current = positions[static_cast<std::size_t>(corners[index].position)];
        const auto& next = positions[static_cast<std::size_t>(corners[(index + 1U) % corners.size()].position)];
        normal[0] += (static_cast<double>(current[1]) - next[1]) *
                     (static_cast<double>(current[2]) + next[2]);
        normal[1] += (static_cast<double>(current[2]) - next[2]) *
                     (static_cast<double>(current[0]) + next[0]);
        normal[2] += (static_cast<double>(current[0]) - next[0]) *
                     (static_cast<double>(current[1]) + next[1]);
    }
    const double magnitude = std::sqrt(normal[0] * normal[0] +
                                       normal[1] * normal[1] + normal[2] * normal[2]);
    if (!std::isfinite(magnitude) || magnitude < 1.0e-12)
        return false;
    faceNormal = {static_cast<float>(normal[0] / magnitude),
                  static_cast<float>(normal[1] / magnitude),
                  static_cast<float>(normal[2] / magnitude)};

    std::size_t dropAxis = 0U;
    if (std::abs(normal[1]) > std::abs(normal[dropAxis])) dropAxis = 1U;
    if (std::abs(normal[2]) > std::abs(normal[dropAxis])) dropAxis = 2U;
    std::vector<Point2> projected;
    projected.reserve(corners.size());
    for (const Corner& corner : corners)
    {
        const auto& p = positions[static_cast<std::size_t>(corner.position)];
        if (dropAxis == 0U) projected.push_back({p[1], p[2]});
        else if (dropAxis == 1U) projected.push_back({p[0], p[2]});
        else projected.push_back({p[0], p[1]});
    }

    double area = 0.0;
    for (std::size_t index = 0; index < projected.size(); ++index)
    {
        const Point2& a = projected[index];
        const Point2& b = projected[(index + 1U) % projected.size()];
        area += a.x * b.y - b.x * a.y;
    }
    if (std::abs(area) < 1.0e-12)
        return false;
    const double orientation = area > 0.0 ? 1.0 : -1.0;

    std::vector<std::size_t> remaining(corners.size());
    for (std::size_t index = 0; index < remaining.size(); ++index)
        remaining[index] = index;
    triangles.clear();
    triangles.reserve(corners.size() - 2U);
    constexpr double epsilon = 1.0e-10;
    while (remaining.size() > 3U)
    {
        bool clipped = false;
        for (std::size_t cursor = 0; cursor < remaining.size(); ++cursor)
        {
            const std::size_t previous = remaining[(cursor + remaining.size() - 1U) % remaining.size()];
            const std::size_t current = remaining[cursor];
            const std::size_t next = remaining[(cursor + 1U) % remaining.size()];
            if (Cross(projected[previous], projected[current], projected[next]) * orientation <= epsilon)
                continue;
            bool containsPoint = false;
            for (const std::size_t candidate : remaining)
            {
                if (candidate == previous || candidate == current || candidate == next)
                    continue;
                if (PointInTriangle(projected[candidate], projected[previous],
                                    projected[current], projected[next], orientation))
                {
                    containsPoint = true;
                    break;
                }
            }
            if (containsPoint)
                continue;
            triangles.push_back({previous, current, next});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(cursor));
            clipped = true;
            break;
        }
        if (!clipped)
            return false;
    }
    triangles.push_back({remaining[0], remaining[1], remaining[2]});
    return true;
}

struct VertexKey final
{
    int position = -1;
    int texCoord = -1;
    int normal = -1;
    std::size_t flatFace = 0U;
    friend bool operator==(const VertexKey&, const VertexKey&) = default;
};

struct VertexKeyHash final
{
    std::size_t operator()(const VertexKey& key) const noexcept
    {
        std::size_t value = static_cast<std::size_t>(key.position + 1);
        value = value * 1'000'003U + static_cast<std::size_t>(key.texCoord + 1);
        value = value * 1'000'003U + static_cast<std::size_t>(key.normal + 1);
        value = value * 1'000'003U + key.flatFace;
        return value;
    }
};
} // namespace

bool DecodeObj(std::string_view source, const std::filesystem::path& sourcePath,
               const ObjImportOptions& options, ObjMesh& output,
               std::vector<ModelDiagnostic>& diagnostics)
{
    diagnostics.clear();
    if (!std::isfinite(options.unitsToMeters) || options.unitsToMeters <= 0.0F ||
        options.unitsToMeters > 1.0e6F)
    {
        Add(diagnostics, sourcePath, 0U, "unitsToMeters", "Scale must be finite and in (0, 1,000,000].");
        return false;
    }
    if (source.size() > MaximumObjBytes)
    {
        Add(diagnostics, sourcePath, 0U, "fileSize", "OBJ source exceeds the 256 MiB input safety limit.");
        return false;
    }

    ObjMesh candidate;
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> texCoords;
    std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> vertexMap;
    std::string currentMaterial;
    std::size_t lineNumber = 0U;
    std::size_t faceNumber = 0U;
    std::size_t offset = 0U;
    bool failed = false;

    while (offset <= source.size())
    {
        const std::size_t end = source.find('\n', offset);
        std::string_view line = source.substr(offset,
            end == std::string_view::npos ? source.size() - offset : end - offset);
        ++lineNumber;
        if (line.size() > MaximumLineBytes)
        {
            Add(diagnostics, sourcePath, lineNumber, "line", "OBJ line exceeds the 1 MiB safety limit.");
            failed = true;
            break;
        }
        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos)
            line = line.substr(0U, comment);
        const auto tokens = Tokens(line);
        if (!tokens.empty())
        {
            if (tokens[0] == "v")
            {
                if (tokens.size() != 4U && tokens.size() != 5U)
                    Add(diagnostics, sourcePath, lineNumber, "vertex", "Position requires x y z and optional homogeneous w.");
                else
                {
                    std::array<float, 4> values{0.0F,0.0F,0.0F,1.0F};
                    bool valid = true;
                    for (std::size_t index = 1U; index < tokens.size(); ++index)
                        valid = ParseFloat(tokens[index], values[index - 1U]) && valid;
                    if (!valid || values[3] == 0.0F)
                        Add(diagnostics, sourcePath, lineNumber, "vertex", "Position components must be finite and homogeneous w must be non-zero.");
                    else
                    {
                        const float inverseW = 1.0F / values[3];
                        const std::array<float, 3> position{
                            values[0] * inverseW * options.unitsToMeters,
                            values[1] * inverseW * options.unitsToMeters,
                            values[2] * inverseW * options.unitsToMeters};
                        if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2]))
                            Add(diagnostics, sourcePath, lineNumber, "vertex", "Scaled position exceeds finite float range.");
                        else
                            positions.push_back(position);
                    }
                }
            }
            else if (tokens[0] == "vt")
            {
                if (tokens.size() != 3U && tokens.size() != 4U)
                    Add(diagnostics, sourcePath, lineNumber, "texCoord", "Texture coordinate requires u v and optional w.");
                else
                {
                    float u = 0.0F, v = 0.0F, ignored = 0.0F;
                    const bool valid = ParseFloat(tokens[1], u) && ParseFloat(tokens[2], v) &&
                        (tokens.size() == 3U || ParseFloat(tokens[3], ignored));
                    if (!valid)
                        Add(diagnostics, sourcePath, lineNumber, "texCoord", "Texture coordinate components must be finite.");
                    else
                        texCoords.push_back({u, options.flipV ? 1.0F - v : v});
                }
            }
            else if (tokens[0] == "vn")
            {
                if (tokens.size() != 4U)
                    Add(diagnostics, sourcePath, lineNumber, "normal", "Normal requires exactly three components.");
                else
                {
                    std::array<float, 3> normal{};
                    if (!ParseFloat(tokens[1], normal[0]) || !ParseFloat(tokens[2], normal[1]) ||
                        !ParseFloat(tokens[3], normal[2]))
                        Add(diagnostics, sourcePath, lineNumber, "normal", "Normal components must be finite.");
                    else
                    {
                        const float magnitude = std::sqrt(normal[0] * normal[0] +
                                                           normal[1] * normal[1] + normal[2] * normal[2]);
                        if (!std::isfinite(magnitude) || magnitude < 1.0e-12F)
                            Add(diagnostics, sourcePath, lineNumber, "normal", "Normal vector must be non-zero.");
                        else
                        {
                            for (float& component : normal) component /= magnitude;
                            normals.push_back(normal);
                        }
                    }
                }
            }
            else if (tokens[0] == "f")
            {
                ++faceNumber;
                if (tokens.size() < 4U || tokens.size() - 1U > MaximumFaceCorners)
                    Add(diagnostics, sourcePath, lineNumber, "face", "Face must contain between 3 and 4096 corners.");
                else
                {
                    std::vector<Corner> corners(tokens.size() - 1U);
                    bool valid = true;
                    for (std::size_t index = 1U; index < tokens.size(); ++index)
                        valid = ParseCorner(tokens[index], positions.size(), texCoords.size(),
                                            normals.size(), corners[index - 1U]) && valid;
                    std::vector<std::array<std::size_t, 3>> triangles;
                    std::array<float, 3> faceNormal{};
                    if (!valid || !Triangulate(corners, positions, triangles, faceNormal))
                        Add(diagnostics, sourcePath, lineNumber, "face",
                            "Face indices are invalid or polygon is degenerate/self-intersecting and cannot be ear-clipped.");
                    else
                    {
                        if (candidate.indices.size() + triangles.size() * 3U > MaximumIndices)
                            Add(diagnostics, sourcePath, lineNumber, "face", "OBJ exceeds the 30 million index safety limit.");
                        else
                        {
                            if (candidate.submeshes.empty() ||
                                candidate.submeshes.back().materialName != currentMaterial)
                                candidate.submeshes.push_back({currentMaterial,
                                    static_cast<std::uint32_t>(candidate.indices.size()), 0U});
                            for (const auto& triangle : triangles)
                            {
                                for (const std::size_t cornerIndex : triangle)
                                {
                                    const Corner& corner = corners[cornerIndex];
                                    const VertexKey key{corner.position, corner.texCoord, corner.normal,
                                        corner.normal < 0 ? faceNumber : 0U};
                                    auto found = vertexMap.find(key);
                                    std::uint32_t vertexIndex = 0U;
                                    if (found == vertexMap.end())
                                    {
                                        if (candidate.vertices.size() >= MaximumVertices)
                                        {
                                            Add(diagnostics, sourcePath, lineNumber, "vertices",
                                                "OBJ exceeds the 10 million vertex safety limit.");
                                            valid = false;
                                            break;
                                        }
                                        ObjVertex vertex;
                                        vertex.position = positions[static_cast<std::size_t>(corner.position)];
                                        vertex.normal = corner.normal >= 0
                                            ? normals[static_cast<std::size_t>(corner.normal)] : faceNormal;
                                        if (corner.texCoord >= 0)
                                            vertex.texCoord = texCoords[static_cast<std::size_t>(corner.texCoord)];
                                        vertexIndex = static_cast<std::uint32_t>(candidate.vertices.size());
                                        candidate.vertices.push_back(vertex);
                                        vertexMap.emplace(key, vertexIndex);
                                    }
                                    else
                                        vertexIndex = found->second;
                                    candidate.indices.push_back(vertexIndex);
                                    ++candidate.submeshes.back().indexCount;
                                }
                                if (!valid) break;
                            }
                        }
                    }
                }
            }
            else if (tokens[0] == "usemtl")
            {
                if (tokens.size() < 2U)
                    Add(diagnostics, sourcePath, lineNumber, "usemtl", "Material name is missing.");
                else
                {
                    currentMaterial = std::string(tokens[1]);
                    for (std::size_t index = 2U; index < tokens.size(); ++index)
                        currentMaterial += " " + std::string(tokens[index]);
                }
            }
            else if (tokens[0] == "mtllib")
            {
                if (tokens.size() < 2U)
                    Add(diagnostics, sourcePath, lineNumber, "mtllib", "Material library path is missing.");
                else
                {
                    for (std::size_t index = 1U; index < tokens.size(); ++index)
                        candidate.materialLibraries.emplace_back(tokens[index]);
                }
            }
        }
        if (end == std::string_view::npos)
            break;
        offset = end + 1U;
    }

    if (positions.empty())
        Add(diagnostics, sourcePath, 0U, "vertices", "OBJ contains no valid position records.");
    if (candidate.indices.empty())
        Add(diagnostics, sourcePath, 0U, "faces", "OBJ contains no valid triangle geometry.");
    if (failed || !diagnostics.empty())
        return false;
    output = std::move(candidate);
    return true;
}

bool LoadObj(const std::filesystem::path& path, const ObjImportOptions& options,
             ObjMesh& output, std::vector<ModelDiagnostic>& diagnostics)
{
    diagnostics.clear();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        Add(diagnostics, path, 0U, "file", "OBJ source could not be opened.");
        return false;
    }
    const std::streamoff end = static_cast<std::streamoff>(input.tellg());
    if (end < 0 || static_cast<std::uint64_t>(end) > MaximumObjBytes)
    {
        Add(diagnostics, path, 0U, "fileSize", "OBJ source is unreadable or exceeds the 256 MiB input safety limit.");
        return false;
    }
    std::string source(static_cast<std::size_t>(end), '\0');
    input.seekg(0, std::ios::beg);
    if (!source.empty())
        input.read(source.data(), static_cast<std::streamsize>(source.size()));
    if (!input || static_cast<std::size_t>(input.gcount()) != source.size())
    {
        Add(diagnostics, path, 0U, "file", "OBJ source could not be read completely.");
        return false;
    }
    return DecodeObj(source, path, options, output, diagnostics);
}
} // namespace talvisota::content
