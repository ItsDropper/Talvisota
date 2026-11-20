#pragma once

#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/MaterialDefinition.hpp"
#include "talvisota/presentation/SceneRenderer.hpp"

#include <string>

namespace talvisota::presentation
{
// Converts the authored core PBR material contract into the renderer-neutral scene
// material. Texture paths are canonical absolute paths; ORM is bound to all three
// channels so the engine can sample AO (R), roughness (G), and metallic (B).
[[nodiscard]] bool ApplyMaterialDefinition(
    const content::MaterialDefinition& definition,
    const content::ContentRegistry& registry,
    RenderMaterial& output,
    std::string& error);
} // namespace talvisota::presentation
