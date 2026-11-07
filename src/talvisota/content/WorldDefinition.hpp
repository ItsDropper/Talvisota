#pragma once

#include "talvisota/content/ContentRegistry.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace talvisota::content
{
struct WorldEnvironment final
{
    std::array<float, 3> sunDirection{-0.45F, 0.28F, -0.85F};
    std::array<float, 3> sunColor{1.0F, 0.88F, 0.72F};
    float sunIntensity = 2.4F;
    std::array<float, 3> ambientColor{0.32F, 0.42F, 0.56F};
    float ambientIntensity = 0.9F;
    std::array<float, 3> fogColor{0.55F, 0.66F, 0.78F};
    float fogDensity = 0.0035F;
    float fogStart = 40.0F;
    std::array<float, 2> windDirection{0.914F, 0.406F};
    float windStrength = 0.06F;
};

struct WorldDefinition final
{
    std::string id;
    std::string name;
    std::string terrainModelId;
    // World-space bounds in metres, with +X east, +Y up, and +Z north.
    std::array<float, 3> boundsMin{};
    std::array<float, 3> boundsMax{};
    float seaLevel = 0.0F;
    bool hasSeaLevel = false;
    WorldEnvironment environment{};
    bool hasEnvironment = false;
};

struct WorldDiagnostic final
{
    std::filesystem::path file;
    std::size_t line = 0;
    std::string field;
    std::string message;
};

// Loads a strict .tworld v1 definition registered as an AssetKind::Definition.
// The caller's output is replaced only after all schema and registry checks pass.
[[nodiscard]] bool LoadWorldDefinition(const std::filesystem::path& path,
                                       const ContentRegistry& registry,
                                       WorldDefinition& output,
                                       std::vector<WorldDiagnostic>& diagnostics);
} // namespace talvisota::content
