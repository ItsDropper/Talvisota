#include "talvisota/presentation/SceneValidation.hpp"

#include "talvisota/presentation/SceneRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>

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

namespace detail
{
bool ValidateCameraFrame(const CameraFrame& camera, std::string& error)
{
    if (!IsFinite(camera.view) || !IsFinite(camera.projection) ||
        !IsFinite(camera.position) || !std::isfinite(camera.elapsedSeconds)) {
        error = "Camera frame contains non-finite values.";
        return false;
    }
    return true;
}

bool ValidateEnvironment(const RenderEnvironment& environment, std::string& error)
{
    if (!environment.enabled)
        return true;

    const auto length3 = [](const std::array<float, 3>& v) {
        return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    };
    const float sunLength = length3(environment.sunDirection);
    const float windLength = std::hypot(environment.windDirection[0],
        environment.windDirection[1]);
    const auto validColor = [](const std::array<float, 3>& v) {
        return std::all_of(v.begin(), v.end(), [](float x) {
            return std::isfinite(x) && x >= 0.0F && x <= 16.0F;
        });
    };
    if (!validColor(environment.sunColor) || !validColor(environment.ambientColor) ||
        !validColor(environment.fogColor) || !std::isfinite(sunLength) ||
        std::abs(sunLength - 1.0F) > 0.02F || !std::isfinite(windLength) ||
        std::abs(windLength - 1.0F) > 0.02F ||
        !std::isfinite(environment.sunIntensity) || environment.sunIntensity < 0.0F ||
        environment.sunIntensity > 10.0F ||
        !std::isfinite(environment.ambientIntensity) || environment.ambientIntensity < 0.0F ||
        environment.ambientIntensity > 4.0F ||
        !std::isfinite(environment.fogDensity) || environment.fogDensity < 0.0F ||
        environment.fogDensity > 0.1F ||
        !std::isfinite(environment.fogStart) || environment.fogStart < 0.0F ||
        !std::isfinite(environment.windStrength) || environment.windStrength < 0.0F ||
        environment.windStrength > 1.0F) {
        error = "Scene environment contains invalid lighting, fog, or wind parameters.";
        return false;
    }
    return true;
}

bool ValidatePointLights(const SceneSnapshot& scene, std::string& error)
{
    if (scene.pointLights.size() > 8U)
    {
        error = "Scene exceeds the renderer's eight local point-light limit.";
        return false;
    }
    for (std::size_t index = 0U; index < scene.pointLights.size(); ++index)
    {
        const RenderPointLight& light = scene.pointLights[index];
        const auto validColor = std::all_of(light.color.begin(), light.color.end(),
            [](const float value) {
                return std::isfinite(value) && value >= 0.0F && value <= 1000.0F;
            });
        if (!IsFinite(light.position) || !validColor ||
            !std::isfinite(light.range) || light.range <= 0.0F || light.range > 10000.0F ||
            !std::isfinite(light.intensity) || light.intensity < 0.0F ||
            light.intensity > 100000.0F)
        {
            error = "Scene point light " + std::to_string(index) +
                " contains an invalid position, range, color, or intensity.";
            return false;
        }
    }
    return true;
}

bool ValidateMeshes(const SceneSnapshot& scene, std::string& error)
{
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
    return true;
}

bool ValidateMaterials(const SceneSnapshot& scene, std::string& error)
{
    for (std::size_t materialIndex = 0; materialIndex < scene.materials.size(); ++materialIndex) {
        const RenderMaterial& material = scene.materials[materialIndex];
        if (!IsFinite(material.baseColor) || material.baseColor[0] < 0.0F ||
            material.baseColor[1] < 0.0F || material.baseColor[2] < 0.0F ||
            !IsFinite(material.emissive) || material.emissive[0] < 0.0F ||
            material.emissive[1] < 0.0F || material.emissive[2] < 0.0F ||
            !std::isfinite(material.normalScale) || material.normalScale < 0.0F ||
            !std::isfinite(material.occlusionStrength) || material.occlusionStrength < 0.0F ||
            material.occlusionStrength > 1.0F || !std::isfinite(material.alphaCutoff) ||
            material.alphaCutoff < 0.0F || material.alphaCutoff > 1.0F ||
            (material.alphaMode != RenderAlphaMode::Opaque && material.alphaMode != RenderAlphaMode::Mask &&
             material.alphaMode != RenderAlphaMode::Blend) ||
            !std::isfinite(material.roughness) || !std::isfinite(material.metallic) ||
            material.roughness < 0.0F || material.roughness > 1.0F ||
            material.metallic < 0.0F || material.metallic > 1.0F) {
            error = "Material " + std::to_string(materialIndex) + " has invalid PBR values.";
            return false;
        }
        const std::array<const std::string*, 6> texturePaths = {
            &material.baseColorTexture, &material.normalTexture, &material.roughnessTexture,
            &material.metallicTexture, &material.ambientOcclusionTexture,
            &material.emissiveTexture
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
    return true;
}

bool ValidateInstances(const SceneSnapshot& scene, std::string& error)
{
    for (std::size_t instanceIndex = 0; instanceIndex < scene.instances.size(); ++instanceIndex) {
        const RenderInstance& instance = scene.instances[instanceIndex];
        if (instance.meshIndex >= scene.meshes.size() || instance.materialIndex >= scene.materials.size()) {
            error = "Instance " + std::to_string(instanceIndex) + " references a missing mesh or material.";
            return false;
        }
        if (!IsFinite(instance.world) || !IsInvertible(instance.world)) {
            error = "Instance " + std::to_string(instanceIndex) + " has an invalid or singular world transform.";
            return false;
        }
        if (!std::isfinite(instance.lodReferenceRadius) || instance.lodReferenceRadius < 0.0F) {
            error = "Instance " + std::to_string(instanceIndex) + " has an invalid LOD reference radius.";
            return false;
        }
        for (const float coordinate : instance.lodReferenceCenter) {
            if (!std::isfinite(coordinate)) {
                error = "Instance " + std::to_string(instanceIndex) + " has a non-finite LOD reference center.";
                return false;
            }
        }
        if (instance.lodCount > MaximumRenderLodLevels) {
            error = "Instance " + std::to_string(instanceIndex) + " exceeds the four-level render LOD limit.";
            return false;
        }
        if (instance.lodCount == 0U) continue;
        if (instance.lodCount < 2U || instance.lodReferenceRadius <= 1.0e-5F) {
            error = "LOD-enabled instance " + std::to_string(instanceIndex) +
                " requires at least two levels and a positive reference bound.";
            return false;
        }
        bool selectedVariantFound = false;
        float previousCoverage = std::numeric_limits<float>::infinity();
        for (std::size_t lodIndex = 0U; lodIndex < instance.lodCount; ++lodIndex) {
            const RenderLodVariant& variant = instance.lodVariants[lodIndex];
            if (variant.meshIndex >= scene.meshes.size() || variant.materialIndex >= scene.materials.size()) {
                error = "Instance " + std::to_string(instanceIndex) + " LOD " +
                    std::to_string(lodIndex) + " references a missing mesh or material.";
                return false;
            }
            if (!std::isfinite(variant.minimumScreenCoverage) ||
                variant.minimumScreenCoverage < 0.0F || variant.minimumScreenCoverage > 1.0F ||
                variant.minimumScreenCoverage >= previousCoverage) {
                error = "Instance " + std::to_string(instanceIndex) +
                    " LOD screen-coverage thresholds must be finite, in [0,1], and strictly descending.";
                return false;
            }
            previousCoverage = variant.minimumScreenCoverage;
            selectedVariantFound = selectedVariantFound ||
                (instance.meshIndex == variant.meshIndex && instance.materialIndex == variant.materialIndex);
        }
        if (instance.lodVariants[instance.lodCount - 1U].minimumScreenCoverage != 0.0F) {
            error = "Instance " + std::to_string(instanceIndex) +
                " LOD chain must provide a final zero-coverage fallback.";
            return false;
        }
        if (!selectedVariantFound) {
            error = "Instance " + std::to_string(instanceIndex) +
                " current mesh/material selection is not part of its LOD chain.";
            return false;
        }
    }
    return true;
}
} // namespace detail

bool SelectRenderLod(RenderInstance& instance, const float screenCoverage,
    std::size_t& selectedLod) noexcept
{
    selectedLod = 0U;
    if (instance.lodCount == 0U) return true;
    if (instance.lodCount > MaximumRenderLodLevels ||
        !std::isfinite(screenCoverage) || screenCoverage < 0.0F) return false;
    for (std::size_t lodIndex = 0U; lodIndex < instance.lodCount; ++lodIndex) {
        const RenderLodVariant& variant = instance.lodVariants[lodIndex];
        if (screenCoverage < variant.minimumScreenCoverage) continue;
        instance.meshIndex = variant.meshIndex;
        instance.materialIndex = variant.materialIndex;
        selectedLod = lodIndex;
        return true;
    }
    return false;
}

bool ValidateSceneSnapshot(const SceneSnapshot& scene, std::string& error)
{
    if (!detail::ValidateCameraFrame(scene.camera, error))
        return false;

    if (!detail::ValidateEnvironment(scene.environment, error))
        return false;

    if (!detail::ValidatePointLights(scene, error))
        return false;

    if (!detail::ValidateMeshes(scene, error))
        return false;

    if (!detail::ValidateMaterials(scene, error))
        return false;

    if (!detail::ValidateInstances(scene, error))
        return false;

    error.clear();
    return true;
}
} // namespace talvisota::presentation
