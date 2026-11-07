#pragma once

#include "talvisota/presentation/SceneRenderer.hpp"

#include <memory>

namespace talvisota::presentation
{
[[nodiscard]] std::unique_ptr<ISceneRenderer> CreateVantaSceneRenderer(RendererBackend backend);
} // namespace talvisota::presentation
