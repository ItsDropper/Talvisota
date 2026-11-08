#pragma once

#include "talvisota/presentation/SceneRenderer.hpp"

#include <string>

namespace talvisota::presentation::detail
{
// Building blocks shared by ValidateSceneSnapshot and the renderer adapters.
// ValidateSceneSnapshot composes them; the Vanta adapter reuses them so per-frame
// validation can cover the camera, environment, and instances without re-walking
// large immutable mesh payloads on frames where scene content did not change.
//
// Every helper rejects the same inputs, in the same order, and with the same error
// messages as the corresponding section of ValidateSceneSnapshot.

[[nodiscard]] bool ValidateCameraFrame(const CameraFrame& camera, std::string& error);
[[nodiscard]] bool ValidateEnvironment(const RenderEnvironment& environment, std::string& error);
[[nodiscard]] bool ValidateMeshes(const SceneSnapshot& scene, std::string& error);
[[nodiscard]] bool ValidateMaterials(const SceneSnapshot& scene, std::string& error);
[[nodiscard]] bool ValidateInstances(const SceneSnapshot& scene, std::string& error);

} // namespace talvisota::presentation::detail
