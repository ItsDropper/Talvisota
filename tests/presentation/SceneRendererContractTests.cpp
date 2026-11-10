#include "talvisota/presentation/SceneRenderer.hpp"
#include "talvisota/presentation/MaterialRenderBridge.hpp"
#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/content/MaterialDefinition.hpp"
#include "talvisota/presentation/CaptureValidation.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <limits>
#include <vector>

using namespace talvisota::presentation;

namespace
{

void Require(const bool condition)
{
    if (!condition)
        std::abort();
}

class FakeSceneRenderer final : public ISceneRenderer
{
public:
    bool Initialize(void* nativeWindow, int width, int height, std::string& error) override
    {
        if (nativeWindow == nullptr || width <= 0 || height <= 0) {
            error = "invalid initialization";
            return false;
        }
        initialized = true;
        error.clear();
        return true;
    }

    bool Resize(int width, int height, std::string& error) override
    {
        if (!initialized || width <= 0 || height <= 0) {
            error = "invalid resize";
            return false;
        }
        error.clear();
        return true;
    }

    bool Render(const SceneSnapshot& scene, std::string& error) override
    {
        if (!initialized || !ValidateSceneSnapshot(scene, error))
            return false;
        ++renderedFrames;
        return true;
    }

    bool ReadbackPresentedFrame(std::vector<std::uint8_t>& bgra,
        std::uint32_t& width, std::uint32_t& height, std::string& error) override
    {
        bgra.clear();
        width = 0U;
        height = 0U;
        error = "Fake renderer does not provide a GPU framebuffer.";
        return false;
    }

    bool IsDeviceLost() const noexcept override { return deviceLost; }

    bool initialized = false;
    bool deviceLost = false;
    std::uint32_t renderedFrames = 0;
};


void TestTypedPbrMaterialBridge()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "talvisota-material-bridge-tests";
    std::error_code filesystemError;
    std::filesystem::remove_all(root, filesystemError);
    std::filesystem::create_directories(root / "materials", filesystemError);
    std::filesystem::create_directories(root / "textures", filesystemError);
    Require(!filesystemError);

    {
        std::ofstream manifest(root / "catalog.tmanifest", std::ios::binary);
        manifest << "talvisota-content-manifest\t1\n"
                 << "asset\tmaterial.test.snow\tmaterial\tmaterials/snow.tmat\trequired\ttexture.test.base,texture.test.normal,texture.test.orm,texture.test.emissive\tproject\tproject\n"
                 << "asset\ttexture.test.base\ttexture\ttextures/snow-albedo.bmp\trequired\t-\tproject\tproject\n"
                 << "asset\ttexture.test.normal\ttexture\ttextures/snow-normal.bmp\trequired\t-\tproject\tproject\n"
                 << "asset\ttexture.test.orm\ttexture\ttextures/snow-orm.bmp\trequired\t-\tproject\tproject\n"
                 << "asset\ttexture.test.emissive\ttexture\ttextures/snow-emissive.bmp\trequired\t-\tproject\tproject\n";
        Require(static_cast<bool>(manifest));
        for (const char* filename : {"snow-albedo.bmp", "snow-normal.bmp", "snow-orm.bmp", "snow-emissive.bmp"})
        {
            std::ofstream texture(root / "textures" / filename, std::ios::binary);
            texture << "test texture payload";
            Require(static_cast<bool>(texture));
        }
        std::ofstream material(root / "materials/snow.tmat", std::ios::binary);
        material << "talvisota-material\t1\n"
                 << "id = material.test.snow\n"
                 << "base_color = 0.82,0.88,0.96\n"
                 << "roughness = 0.91\n"
                 << "metallic = 0.04\n"
                 << "base_color_texture = texture.test.base\n"
                 << "normal_texture = texture.test.normal\n"
                 << "orm_texture = texture.test.orm\n"
                 << "emissive_texture = texture.test.emissive\n";
        Require(static_cast<bool>(material));
    }

    talvisota::content::ContentRegistry registry;
    std::vector<talvisota::content::ContentDiagnostic> contentDiagnostics;
    Require(talvisota::content::LoadContentManifest(
        root / "catalog.tmanifest", root, registry, contentDiagnostics));

    talvisota::content::MaterialDefinition definition;
    std::vector<talvisota::content::MaterialDiagnostic> materialDiagnostics;
    Require(talvisota::content::LoadMaterialDefinition(
        root / "materials/snow.tmat", registry, definition, materialDiagnostics));

    RenderMaterial renderMaterial;
    std::string error;
    Require(ApplyMaterialDefinition(definition, registry, renderMaterial, error));
    Require(error.empty());
    Require(std::abs(renderMaterial.baseColor[0] - 0.82F) < 0.0001F);
    Require(std::abs(renderMaterial.baseColor[1] - 0.88F) < 0.0001F);
    Require(std::abs(renderMaterial.roughness - 0.91F) < 0.0001F);
    Require(std::abs(renderMaterial.metallic - 0.04F) < 0.0001F);
    Require(renderMaterial.packedMetallicRoughness);
    Require(std::filesystem::path(renderMaterial.baseColorTexture).is_absolute());
    Require(std::filesystem::path(renderMaterial.normalTexture).is_absolute());
    Require(!renderMaterial.roughnessTexture.empty());
    Require(renderMaterial.roughnessTexture == renderMaterial.metallicTexture);
    Require(renderMaterial.roughnessTexture == renderMaterial.ambientOcclusionTexture);
    Require(std::filesystem::path(renderMaterial.roughnessTexture).is_absolute());
    Require(std::filesystem::path(renderMaterial.emissiveTexture).is_absolute());
    SceneSnapshot translatedMaterialScene;
    translatedMaterialScene.materials.push_back(renderMaterial);
    std::string sceneValidationError;
    Require(ValidateSceneSnapshot(translatedMaterialScene, sceneValidationError));
    Require(sceneValidationError.empty());

    definition.emissive = {0.1F, 0.0F, 0.0F};
    definition.normalScale = 0.65F;
    definition.occlusionStrength = 0.4F;
    definition.alphaMode = talvisota::content::MaterialAlphaMode::Mask;
    definition.alphaCutoff = 0.33F;
    definition.doubleSided = true;
    Require(ApplyMaterialDefinition(definition, registry, renderMaterial, error));
    Require(renderMaterial.emissive == definition.emissive);
    Require(std::abs(renderMaterial.normalScale - 0.65F) < 0.0001F);
    Require(std::abs(renderMaterial.occlusionStrength - 0.4F) < 0.0001F);
    Require(renderMaterial.alphaMode == RenderAlphaMode::Mask);
    Require(std::abs(renderMaterial.alphaCutoff - 0.33F) < 0.0001F);
    Require(renderMaterial.doubleSided);

    definition.alphaMode = talvisota::content::MaterialAlphaMode::Blend;
    Require(ApplyMaterialDefinition(definition, registry, renderMaterial, error));
    Require(renderMaterial.alphaMode == RenderAlphaMode::Blend);
    definition.baseColorTexture = "texture.test.missing";
    const RenderMaterial beforeFailure = renderMaterial;
    Require(!ApplyMaterialDefinition(definition, registry, renderMaterial, error));
    Require(!error.empty());
    Require(renderMaterial.baseColor == beforeFailure.baseColor);
    Require(renderMaterial.emissive == beforeFailure.emissive);

    std::filesystem::remove_all(root, filesystemError);
    Require(!filesystemError);
}

SceneSnapshot ValidScene()
{
    SceneSnapshot scene;
    RenderMesh mesh;
    mesh.vertices = {
        {{-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        {{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        {{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 0.5F, 1.0F}
    };
    mesh.indices = {0U, 1U, 2U};
    scene.meshes.push_back(mesh);
    RenderMaterial material;
    material.baseColor = {0.2F, 0.5F, 0.8F};
    material.roughness = 0.7F;
    material.metallic = 0.1F;
    scene.materials.push_back(material);
    RenderInstance instance;
    instance.meshIndex=0U;
    instance.materialIndex=0U;
    scene.instances.push_back(instance);
    return scene;
}
}

int main()
{
    TestTypedPbrMaterialBridge();
    std::string error;
    SceneSnapshot scene = ValidScene();
    FakeSceneRenderer fakeRenderer;
    Require(!fakeRenderer.Render(scene, error));
    int nativeWindowToken = 1;
    Require(fakeRenderer.Initialize(&nativeWindowToken, 1280, 720, error));
    Require(fakeRenderer.Resize(800, 600, error));
    Require(fakeRenderer.Render(scene, error));
    Require(fakeRenderer.renderedFrames == 1U);
    fakeRenderer.deviceLost = true;
    Require(fakeRenderer.IsDeviceLost());

    Require(ValidateSceneSnapshot(scene, error));
    Require(error.empty());
    // The original fixed sky coordinate landed on the church facade in the
    // close village view. A broad top-quarter sample must tolerate that while
    // still rejecting a warm frame or a BGRA/RGBA channel-order regression.
    constexpr std::uint32_t captureWidth = 32U;
    constexpr std::uint32_t captureHeight = 32U;
    std::vector<std::uint8_t> skyFrame(captureWidth * captureHeight * 4U, 255U);
    for (std::uint32_t y = 0U; y < captureHeight / 4U; ++y) {
        for (std::uint32_t x = 0U; x < captureWidth; ++x) {
            const std::size_t pixel = (static_cast<std::size_t>(y) * captureWidth + x) * 4U;
            skyFrame[pixel] = 220U;
            skyFrame[pixel + 1U] = 212U;
            skyFrame[pixel + 2U] = 204U;
        }
    }
    const std::size_t formerlySampledPixel =
        (static_cast<std::size_t>(captureHeight / 8U) * captureWidth + captureWidth / 4U) * 4U;
    skyFrame[formerlySampledPixel] = 38U;
    skyFrame[formerlySampledPixel + 1U] = 54U;
    skyFrame[formerlySampledPixel + 2U] = 78U;
    PresentedSkyRegionStats skyStats;
    Require(ValidatePresentedSkyRegion(skyFrame, captureWidth, captureHeight, skyStats, error));
    Require(skyStats.sampledPixels == captureWidth * (captureHeight / 4U));
    Require(skyStats.blueDominantSampleCount == skyStats.sampledPixels - 1U);
    Require(skyStats.meanBlue > skyStats.meanRed);
    Require(error.empty());

    std::vector<std::uint8_t> warmFrame = skyFrame;
    for (std::uint32_t y = 0U; y < captureHeight / 4U; ++y) {
        for (std::uint32_t x = 0U; x < captureWidth; ++x) {
            const std::size_t pixel = (static_cast<std::size_t>(y) * captureWidth + x) * 4U;
            warmFrame[pixel] = 38U;
            warmFrame[pixel + 1U] = 54U;
            warmFrame[pixel + 2U] = 78U;
        }
    }
    Require(!ValidatePresentedSkyRegion(warmFrame, captureWidth, captureHeight, skyStats, error));
    Require(!error.empty());

    std::vector<std::uint8_t> swappedChannels = skyFrame;
    for (std::uint32_t y = 0U; y < captureHeight / 4U; ++y) {
        for (std::uint32_t x = 0U; x < captureWidth; ++x) {
            const std::size_t pixel = (static_cast<std::size_t>(y) * captureWidth + x) * 4U;
            std::swap(swappedChannels[pixel], swappedChannels[pixel + 2U]);
        }
    }
    Require(!ValidatePresentedSkyRegion(swappedChannels, captureWidth, captureHeight, skyStats, error));
    Require(!error.empty());

    Require(!ValidatePresentedSkyRegion(std::span<const std::uint8_t>{},
        captureWidth, captureHeight, skyStats, error));
    Require(!error.empty());

    SceneSnapshot invalidIndex = ValidScene();
    invalidIndex.meshes[0].indices[2] = 99U;
    Require(!ValidateSceneSnapshot(invalidIndex, error));
    Require(!error.empty());

    SceneSnapshot zeroNormal = ValidScene();
    zeroNormal.meshes[0].vertices[0].normal = {0.0F, 0.0F, 0.0F};
    Require(!ValidateSceneSnapshot(zeroNormal, error));

    SceneSnapshot invalidMaterial = ValidScene();
    invalidMaterial.materials[0].roughness = 1.5F;
    Require(!ValidateSceneSnapshot(invalidMaterial, error));

    SceneSnapshot texturedMaterial = ValidScene();
    texturedMaterial.materials[0].normalTexture =
        (std::filesystem::temp_directory_path() / "talvisota-normal.png").string();
    Require(ValidateSceneSnapshot(texturedMaterial, error));

    SceneSnapshot packedOrmMaterial = ValidScene();
    const auto ormPath = std::filesystem::temp_directory_path() / "talvisota-snow-orm.png";
    packedOrmMaterial.materials[0].roughnessTexture = ormPath.string();
    packedOrmMaterial.materials[0].ambientOcclusionTexture = ormPath.string();
    packedOrmMaterial.materials[0].packedMetallicRoughness = true;
    Require(ValidateSceneSnapshot(packedOrmMaterial, error));

    SceneSnapshot unresolvedTexture = ValidScene();
    unresolvedTexture.materials[0].normalTexture = "textures/normal.png";
    Require(!ValidateSceneSnapshot(unresolvedTexture, error));

    SceneSnapshot missingResource = ValidScene();
    missingResource.instances[0].meshIndex = 1U;
    Require(!ValidateSceneSnapshot(missingResource, error));

    SceneSnapshot singularTransform = ValidScene();
    singularTransform.instances[0].world[0] = 0.0F;
    singularTransform.instances[0].world[5] = 0.0F;
    singularTransform.instances[0].world[10] = 0.0F;
    Require(!ValidateSceneSnapshot(singularTransform, error));

    SceneSnapshot winterEnvironment = ValidScene();
    winterEnvironment.environment.enabled = true;
    winterEnvironment.environment.sunDirection = {-0.45F, 0.28F, -0.85F};
    winterEnvironment.environment.windDirection = {0.914F, 0.406F};
    winterEnvironment.environment.sunIntensity = 2.4F;
    winterEnvironment.environment.fogDensity = 0.0014F;
    winterEnvironment.environment.fogStart = 75.0F;
    Require(ValidateSceneSnapshot(winterEnvironment, error));

    SceneSnapshot invalidEnvironment = winterEnvironment;
    invalidEnvironment.environment.sunDirection = {0.0F, 0.0F, 0.0F};
    Require(!ValidateSceneSnapshot(invalidEnvironment, error));

    SceneSnapshot invalidCamera = ValidScene();
    invalidCamera.camera.elapsedSeconds = std::numeric_limits<float>::quiet_NaN();
    Require(!ValidateSceneSnapshot(invalidCamera, error));

    SceneSnapshot localLighting = ValidScene();
    localLighting.pointLights.push_back({{2.0F, 3.0F, 4.0F}, 24.0F, {1.0F, 0.5F, 0.2F}, 120.0F});
    Require(ValidateSceneSnapshot(localLighting, error));
    localLighting.pointLights.front().range = 0.0F;
    Require(!ValidateSceneSnapshot(localLighting, error));

    return 0;
}
