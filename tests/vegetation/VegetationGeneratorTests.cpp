#include "talvisota/vegetation/Species.hpp"
#include "talvisota/vegetation/Mesh.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>

using namespace talvisota::vegetation;

namespace
{
void Validate(const Species species, const std::uint64_t seed)
{
    SpeciesBuild build{};
    build.seed = seed;
    build.height = 16.0F;
    const auto a = GenerateTree(species, build);
    const auto b = GenerateTree(species, build);
    assert(a.height > 0.0F);
    assert(a.trunkRadius > 0.0F);
    assert(a.segments.size() > 100);
    assert(a.segments.size() == b.segments.size());
    assert(a.chains.size() == b.chains.size());
    assert(std::abs(a.height - b.height) < 1.0e-6F);
    for (std::size_t i = 0; i < a.segments.size(); ++i)
    {
        const auto& x = a.segments[i];
        const auto& y = b.segments[i];
        assert(x.parentIndex == y.parentIndex);
        assert(x.start == y.start);
        assert(x.end == y.end);
        assert(x.startRadius > 0.0F);
        assert(x.endRadius > 0.0F);
        assert(x.endRadius <= x.startRadius);
        assert(std::isfinite(x.start[0]) && std::isfinite(x.start[1]) &&
            std::isfinite(x.start[2]));
        if (x.parentIndex >= 0)
            assert(static_cast<std::size_t>(x.parentIndex) < a.segments.size());
    }
    const auto mesh = BuildTreeMesh(a, seed, 9);
    const auto meshAgain = BuildTreeMesh(b, seed, 9);
    assert(!mesh.vertices.empty() && !mesh.indices.empty() && !mesh.sections.empty());
    assert(mesh.vertices.size() == meshAgain.vertices.size());
    assert(mesh.indices == meshAgain.indices);
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        assert(mesh.vertices[i].position == meshAgain.vertices[i].position);
        assert(mesh.vertices[i].normal == meshAgain.vertices[i].normal);
    }
    for (const auto& section : mesh.sections)
    {
        assert(section.indexCount > 0U && section.indexCount % 3U == 0U);
        assert(static_cast<std::size_t>(section.firstIndex) + section.indexCount <= mesh.indices.size());
    }
    for (const auto index : mesh.indices)
        assert(index < mesh.vertices.size());
    for (const auto& vertex : mesh.vertices)
    {
        assert(std::isfinite(vertex.position[0]) && std::isfinite(vertex.position[1]) &&
            std::isfinite(vertex.position[2]));
        const float normalLength = Length(vertex.normal);
        assert(normalLength > 0.99F && normalLength < 1.01F);
    }
    const auto snow = BuildSnowMesh(a);
    if (species == Species::Birch)
        assert(snow.indices.empty());
    else
    {
        assert(!snow.indices.empty() && snow.sections.front().materialGroup == kSnowMeshMaterialGroup);
        for (const auto index : snow.indices)
            assert(index < snow.vertices.size());
        for (const auto& vertex : snow.vertices)
            assert(std::isfinite(vertex.position[0]) && std::isfinite(vertex.position[1]) &&
                std::isfinite(vertex.position[2]));
    }
    const auto measurements = MeasureTrunk(a);
    assert(measurements.height == a.height);
    assert(measurements.buttRadius > measurements.tipRadius);
}
}

int main()
{
    Validate(Species::Spruce, 0x12345678u);
    Validate(Species::Pine, 0x12345678u);
    Validate(Species::Birch, 0x12345678u);
    const auto pine = GenerateTree(Species::Pine, SpeciesBuild{ .seed = 1 });
    const auto birch = GenerateTree(Species::Birch, SpeciesBuild{ .seed = 1 });
    assert(pine.segments.size() != birch.segments.size());
    return 0;
}
