#include "talvisota/presentation/SceneRenderer.hpp"

#include <cmath>
#include <filesystem>
#include <limits>

namespace talvisota::presentation
{
namespace
{
bool IsFinite(const std::array<float, 16>& matrix) noexcept
{
    for (const float value : matrix)
        if (!std::isfinite(value))
            return false;
    return true;
}

bool IsFinite(const std::array<float, 3>& vector) noexcept
{
    return std::isfinite(vector[0]) && std::isfinite(vector[1]) &&
           std::isfinite(vector[2]);
}

bool IsInvertible(const std::array<float, 16>& matrix) noexcept
{
    const float a = matrix[0], b = matrix[4], c = matrix[8];
    const float d = matrix[1], e = matrix[5], f = matrix[9];
    const float g = matrix[2], h = matrix[6], i = matrix[10];
    const float determinant = a * (e * i - f * h) -
        b * (d * i - f * g) + c * (d * h - e * g);
    return std::isfinite(determinant) && std::abs(determinant) > 1.0e-8F;
}
} // namespace

bool ValidateSceneSnapshot(const SceneSnapshot& scene, std::string& error)
{
    if (!IsFinite(scene.camera.view) || !IsFinite(scene.camera.projection) ||
        !IsFinite(scene.camera.position) || !std::isfinite(scene.camera.elapsedSeconds)) {
        error = "Camera frame contains non-finite values.";
        return false;
    }

    for (std::size_t meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex) {
        const RenderMesh& mesh = scene.meshes[meshIndex];
        if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3U != 0U) {
            error = "Mesh " + std::to_string(meshIndex) + " must contain indexed triangles.";
            return false;
        }
        if (mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
            mesh.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
            error = "Mesh " + std::to_string(meshIndex) + " exceeds 32-bit index limits.";
            return false;
        }
        for (const RenderVertex& vertex : mesh.vertices) {
            if (!IsFinite(vertex.position) || !IsFinite(vertex.normal) ||
                !std::isfinite(vertex.u) || !std::isfinite(vertex.v)) {
                error = "Mesh " + std::to_string(meshIndex) + " contains non-finite vertex data.";
                return false;
            }
            const float normalLengthSquared = vertex.normal[0] * vertex.normal[0] +
                vertex.normal[1] * vertex.normal[1] + vertex.normal[2] * vertex.normal[2];
            if (normalLengthSquared <= 1.0e-12F) {
                error = "Mesh " + std::to_string(meshIndex) + " contains a zero-length normal.";
                return false;
            }
        }
        for (const std::uint32_t index : mesh.indices) {
            if (index >= mesh.vertices.size()) {
                error = "Mesh " + std::to_string(meshIndex) + " contains an out-of-range index.";
                return false;
            }
        }
    }

    for (std::size_t materialIndex = 0; materialIndex < scene.materials.size(); ++materialIndex) {
        const RenderMaterial& material = scene.materials[materialIndex];
        if (!IsFinite(material.baseColor) || material.baseColor[0] < 0.0F ||
            material.baseColor[1] < 0.0F || material.baseColor[2] < 0.0F ||
            !std::isfinite(material.roughness) || !std::isfinite(material.metallic) ||
            material.roughness < 0.0F || material.roughness > 1.0F ||
            material.metallic < 0.0F || material.metallic > 1.0F) {
            error = "Material " + std::to_string(materialIndex) + " has invalid PBR values.";
            return false;
        }
        const std::array<const std::string*, 5> texturePaths = {
            &material.baseColorTexture, &material.normalTexture, &material.roughnessTexture,
            &material.metallicTexture, &material.ambientOcclusionTexture
        };
        for (const std::string* texturePath : texturePaths) {
            if (texturePath->find('\0') != std::string::npos ||
                (!texturePath->empty() && !std::filesystem::path(*texturePath).is_absolute())) {
                error = "Material " + std::to_string(materialIndex) +
                    " contains a texture path that is not a resolved absolute file path.";
                return false;
            }
        }
    }

    for (std::size_t instanceIndex = 0; instanceIndex < scene.instances.size(); ++instanceIndex) {
        const RenderInstance& instance = scene.instances[instanceIndex];
        if (instance.meshIndex >= scene.meshes.size() ||
            instance.materialIndex >= scene.materials.size()) {
            error = "Instance " + std::to_string(instanceIndex) + " references a missing mesh or material.";
            return false;
        }
        if (!IsFinite(instance.world) || !IsInvertible(instance.world)) {
            error = "Instance " + std::to_string(instanceIndex) + " has an invalid or singular world transform.";
            return false;
        }
    }

    error.clear();
    return true;
}
} // namespace talvisota::presentation
