#pragma once

#include "talvisota/content/ContentRegistry.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace talvisota::content
{
struct WorldDefinition final
{
    std::string id;
    std::string name;
    // World-space bounds in metres, with +X east, +Y up, and +Z north.
    std::array<float, 3> boundsMin{};
    std::array<float, 3> boundsMax{};
    float seaLevel = 0.0F;
    bool hasSeaLevel = false;
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
