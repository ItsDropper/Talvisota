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

// Produces a deterministic package containing a normalized source catalog,
// source assets, and a SHA-256 inventory. The staged package is fully reimported,
// semantically validated, and integrity-checked before destination replacement.
[[nodiscard]] bool BuildAssetPackage(const ContentRegistry& registry,
                                     const std::filesystem::path& destination,
                                     AssetPackageSummary& output,
                                     std::vector<EngineAssetDiagnostic>& diagnostics);

// Revalidates the packaged catalog, dependency graph, typed definitions, media,
// every file digest/path, and exact correspondence between catalog and inventory.
[[nodiscard]] bool ValidateAssetPackage(const std::filesystem::path& packageRoot,
                                        AssetPackageSummary& output,
                                        std::vector<EngineAssetDiagnostic>& diagnostics);
} // namespace talvisota::content
