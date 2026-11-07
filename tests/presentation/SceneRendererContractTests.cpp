#include "talvisota/presentation/SceneRenderer.hpp"

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <string>
#include <limits>

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

    bool IsDeviceLost() const noexcept override { return deviceLost; }

    bool initialized = false;
    bool deviceLost = false;
    std::uint32_t renderedFrames = 0;
};

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
    scene.materials.push_back(std::move(material));
    scene.instances.push_back({0U, 0U});
    return scene;
}
}

int main()
{
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

    SceneSnapshot invalidCamera = ValidScene();
    invalidCamera.camera.elapsedSeconds = std::numeric_limits<float>::quiet_NaN();
    Require(!ValidateSceneSnapshot(invalidCamera, error));

    return 0;
}
