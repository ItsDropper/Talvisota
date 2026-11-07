#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace talvisota::content
{
struct ObjImportOptions final
{
    // OBJ has no universal unit declaration. The caller must supply the source
    // unit-to-metre multiplier; one means the file is already authored in metres.
    float unitsToMeters = 1.0F;
    bool flipV = false;
};

struct ObjVertex final
{
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 2> texCoord{};
};

struct ObjSubmesh final
{
    std::string materialName;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
};

struct ObjMesh final
{
    std::vector<ObjVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<ObjSubmesh> submeshes;
    std::vector<std::string> materialLibraries;
};

struct ModelDiagnostic final
{
    std::filesystem::path file;
    std::size_t line = 0;
    std::string field;
    std::string message;
};

// Imports Wavefront OBJ geometry into metre-scaled, right-handed (+X east,
// +Y up, +Z north) triangles. Polygon faces are ear-clipped, not fan-triangulated.
[[nodiscard]] bool DecodeObj(std::string_view source,
                             const std::filesystem::path& sourcePath,
                             const ObjImportOptions& options,
                             ObjMesh& output,
                             std::vector<ModelDiagnostic>& diagnostics);
[[nodiscard]] bool LoadObj(const std::filesystem::path& path,
                           const ObjImportOptions& options,
                           ObjMesh& output,
                           std::vector<ModelDiagnostic>& diagnostics);
} // namespace talvisota::content
