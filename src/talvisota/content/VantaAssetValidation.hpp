#pragma once

#include "talvisota/content/ContentRegistry.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace talvisota::content
{
struct EngineAssetDiagnostic final
{
    std::filesystem::path file;
    std::string field;
    std::string message;
};

struct EngineAssetSummary final
{
    std::size_t importedModels = 0;
    std::size_t decodedTextures = 0;
    std::size_t decodedAudio = 0;
};

// Uses VantaEngine CPU services to import registered OBJ/glTF/GLB models,
// decode WIC and processed textures, validate registered OBJ MTL dependencies,
// and decode bounded PCM/IEEE-float WAV audio. Model references must resolve
// inside the content root and be declared as manifest dependencies. No GPU is initialized.
[[nodiscard]] bool ValidateEngineAssets(const ContentRegistry& registry,
                                        EngineAssetSummary& output,
                                        std::vector<EngineAssetDiagnostic>& diagnostics);
} // namespace talvisota::content
