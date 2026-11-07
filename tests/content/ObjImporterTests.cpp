#include "talvisota/content/ObjImporter.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace talvisota::content;
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void TestTriangulatesConcavePolygonAndResolvesNegativeIndices()
{
    const std::string source =
        "v 0 0 0\n"
        "v 2 0 0\n"
        "v 2 2 0\n"
        "v 1 1 0\n"
        "v 0 2 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 1\n"
        "vt 0.5 0.5\n"
        "vt 0 1\n"
        "usemtl snow surface\n"
        "f -5/1 -4/2 -3/3 -2/4 -1/5\n";
    ObjMesh mesh;
    std::vector<ModelDiagnostic> diagnostics;
    Require(DecodeObj(source, "concave.obj", ObjImportOptions{0.5F, true}, mesh, diagnostics),
            "Valid concave OBJ polygon should import.");
    Require(mesh.indices.size() == 9U && mesh.submeshes.size() == 1U,
            "Concave pentagon should ear-clip into three triangles.");
    Require(mesh.submeshes[0].materialName == "snow surface",
            "Material names with spaces should be preserved.");
    for (const auto& vertex : mesh.vertices)
        Require(vertex.position[0] <= 1.0F,
                "Caller-provided source-unit scale should be applied.");
    Require(std::any_of(mesh.vertices.begin(), mesh.vertices.end(),
                        [](const ObjVertex& vertex) { return vertex.texCoord[1] == 1.0F; }),
            "Optional V-axis flip should be applied.");
}

void TestGeneratesFlatNormalsWhenAbsent()
{
    const std::string source =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n";
    ObjMesh mesh;
    std::vector<ModelDiagnostic> diagnostics;
    Require(DecodeObj(source, "triangle.obj", {}, mesh, diagnostics),
            "Triangle without explicit normals should import.");
    Require(mesh.vertices.size() == 3U && mesh.indices.size() == 3U,
            "Missing normals should not prevent geometry import.");
    for (const auto& vertex : mesh.vertices)
        Require(vertex.normal[2] > 0.99F, "Missing normals should be generated from face winding.");
}

void TestRejectsBadIndicesAndPreservesOutput()
{
    const std::string source =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 9\n";
    ObjMesh mesh;
    mesh.indices = {7U,8U,9U};
    std::vector<ModelDiagnostic> diagnostics;
    Require(!DecodeObj(source, "bad.obj", {}, mesh, diagnostics),
            "Out-of-range face indices must fail.");
    Require(!diagnostics.empty() && mesh.indices.size() == 3U && mesh.indices[0] == 7U,
            "Failed model import must preserve previously published output.");
}

void TestRejectsInvalidScale()
{
    ObjMesh mesh;
    std::vector<ModelDiagnostic> diagnostics;
    Require(!DecodeObj("v 0 0 0\n", "scale.obj", ObjImportOptions{0.0F, false}, mesh, diagnostics),
            "Zero source-unit scale must fail.");
}
}

int main()
{
    try
    {
        TestTriangulatesConcavePolygonAndResolvesNegativeIndices();
        TestGeneratesFlatNormalsWhenAbsent();
        TestRejectsBadIndicesAndPreservesOutput();
        TestRejectsInvalidScale();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "OBJ importer test failure: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All OBJ importer tests passed.\n";
    return 0;
}
