#include "talvisota/presentation/TriangleSubdivisionBudget.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

using namespace talvisota::presentation;

namespace
{
void Require(const bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void TestSingleTriangle()
{
    constexpr std::array<std::uint32_t, 3> indices{0U, 1U, 2U};
    TriangleSubdivisionEstimate estimate;
    std::string error;
    Require(EstimateFourWayTriangleSubdivision(3U, indices, estimate, error),
        "A valid single triangle must be estimated.");
    Require(error.empty(), "Successful estimation must clear the error.");
    Require(estimate.sourceVertices == 3U && estimate.sourceIndices == 3U &&
        estimate.uniqueEdges == 3U && estimate.resultVertices == 6U &&
        estimate.resultIndices == 12U,
        "A single triangle must add three midpoint vertices and produce four child triangles.");
}

void TestSharedEdgesAreCountedOnce()
{
    constexpr std::array<std::uint32_t, 6> indices{0U, 1U, 2U, 2U, 1U, 3U};
    TriangleSubdivisionEstimate estimate;
    std::string error;
    Require(EstimateFourWayTriangleSubdivision(4U, indices, estimate, error),
        "Two adjacent triangles must be estimated.");
    Require(estimate.uniqueEdges == 5U && estimate.resultVertices == 9U &&
        estimate.resultIndices == 24U,
        "The shared diagonal must create one midpoint, not one midpoint per triangle.");
}

void TestRepeatedFacesReuseTheSameMidpoints()
{
    constexpr std::array<std::uint32_t, 6> indices{0U, 1U, 2U, 0U, 1U, 2U};
    TriangleSubdivisionEstimate estimate;
    std::string error;
    Require(EstimateFourWayTriangleSubdivision(3U, indices, estimate, error),
        "Repeated faces must be estimated.");
    Require(estimate.uniqueEdges == 3U && estimate.resultVertices == 6U &&
        estimate.resultIndices == 24U,
        "Repeated faces must share the same three cached edge midpoints.");
}

void TestDegenerateSelfEdgesMatchRuntimeCache()
{
    constexpr std::array<std::uint32_t, 3> indices{0U, 0U, 1U};
    TriangleSubdivisionEstimate estimate;
    std::string error;
    Require(EstimateFourWayTriangleSubdivision(2U, indices, estimate, error),
        "A triangle containing a repeated vertex must be estimated without undercounting.");
    Require(estimate.uniqueEdges == 2U && estimate.resultVertices == 4U &&
        estimate.resultIndices == 12U,
        "The (0,0) self-edge and the (0,1) edge must each allocate one midpoint.");
}

void TestVertexBudgetBoundary()
{
    constexpr std::array<std::uint32_t, 6> indices{0U, 1U, 2U, 2U, 1U, 3U};
    TriangleSubdivisionEstimate estimate;
    std::string error;
    Require(EstimateFourWayTriangleSubdivision(4U, indices, estimate, error),
        "The shared-edge estimate must succeed before budget checks.");
    Require(FitsVertexBudget(estimate, 9U, error) && error.empty(),
        "A result exactly at the vertex budget must be accepted.");
    Require(!FitsVertexBudget(estimate, 8U, error) && !error.empty(),
        "A result one vertex over budget must be rejected with a diagnostic.");
    Require(!FitsVertexBudget(estimate, 0U, error),
        "A zero vertex budget must be rejected.");
}

void TestInvalidInputsPreserveOutput()
{
    TriangleSubdivisionEstimate estimate{7U, 9U, 11U, 13U, 15U};
    const TriangleSubdivisionEstimate original = estimate;
    std::string error;
    constexpr std::array<std::uint32_t, 3> outOfRange{0U, 1U, 3U};
    Require(!EstimateFourWayTriangleSubdivision(3U, outOfRange, estimate, error),
        "Out-of-range source indices must be rejected.");
    Require(estimate.sourceVertices == original.sourceVertices &&
        estimate.sourceIndices == original.sourceIndices &&
        estimate.uniqueEdges == original.uniqueEdges &&
        estimate.resultVertices == original.resultVertices &&
        estimate.resultIndices == original.resultIndices,
        "Failed estimation must preserve the caller's previous output.");
    Require(error.find("triangle 0") != std::string::npos,
        "Out-of-range diagnostics must identify the affected triangle.");

    constexpr std::array<std::uint32_t, 2> incomplete{0U, 1U};
    Require(!EstimateFourWayTriangleSubdivision(2U, incomplete, estimate, error),
        "An incomplete triangle index list must be rejected.");
    constexpr std::array<std::uint32_t, 0> empty{};
    Require(!EstimateFourWayTriangleSubdivision(0U, empty, estimate, error),
        "An empty mesh must be rejected.");
}

void TestThirtyTwoBitVertexCapacity()
{
    constexpr std::array<std::uint32_t, 3> indices{0U, 1U, 2U};
    TriangleSubdivisionEstimate estimate;
    std::string error;
    const std::size_t maximumVertexCount = static_cast<std::size_t>(
        std::numeric_limits<std::uint32_t>::max());
    Require(!EstimateFourWayTriangleSubdivision(maximumVertexCount, indices, estimate, error),
        "A subdivision that exceeds 32-bit vertex capacity must fail before index truncation.");
    Require(error.find("32-bit vertex-index capacity") != std::string::npos,
        "Vertex-capacity overflow must have a specific diagnostic.");
}
} // namespace

int main()
{
    try
    {
        TestSingleTriangle();
        TestSharedEdgesAreCountedOnce();
        TestRepeatedFacesReuseTheSameMidpoints();
        TestDegenerateSelfEdgesMatchRuntimeCache();
        TestVertexBudgetBoundary();
        TestInvalidInputsPreserveOutput();
        TestThirtyTwoBitVertexCapacity();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "Triangle subdivision budget test failure: %s\n", exception.what());
        return 1;
    }
}
