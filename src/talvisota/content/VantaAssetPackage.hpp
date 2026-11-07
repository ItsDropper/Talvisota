#pragma once

#include "talvisota/content/VantaAssetValidation.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace talvisota::content
{
struct AssetPackageSummary final
{
    std::size_t assetCount = 0;
    std::uint64_t totalBytes = 0;
};

// Produces a deterministic directory package with a SHA-256 inventory.
// The destination is replaced only after the staged package validates.
[[nodiscard]] bool BuildAssetPackage(const ContentRegistry& registry,
                                     const std::filesystem::path& destination,
                                     AssetPackageSummary& output,
                                     std::vector<EngineAssetDiagnostic>& diagnostics);

// Revalidates every packaged file, digest, path, and inventory entry.
[[nodiscard]] bool ValidateAssetPackage(const std::filesystem::path& packageRoot,
                                        AssetPackageSummary& output,
                                        std::vector<EngineAssetDiagnostic>& diagnostics);
} // namespace talvisota::content
