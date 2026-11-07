#include "talvisota/presentation/SceneRenderer.hpp"

#include <cstdlib>
#include <cstdint>
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
    scene.materials.push_back({{0.2F, 0.5F, 0.8F}, 0.7F, 0.1F});
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
    assert(fakeRenderer.Initialize(&nativeWindowToken, 1280, 720, error));
    assert(fakeRenderer.Resize(800, 600, error));
    assert(fakeRenderer.Render(scene, error));
    assert(fakeRenderer.renderedFrames == 1U);
    fakeRenderer.deviceLost = true;
    assert(fakeRenderer.IsDeviceLost());

    assert(ValidateSceneSnapshot(scene, error));
    assert(error.empty());

    SceneSnapshot invalidIndex = ValidScene();
    invalidIndex.meshes[0].indices[2] = 99U;
    assert(!ValidateSceneSnapshot(invalidIndex, error));
    assert(!error.empty());

    SceneSnapshot zeroNormal = ValidScene();
    zeroNormal.meshes[0].vertices[0].normal = {0.0F, 0.0F, 0.0F};
    assert(!ValidateSceneSnapshot(zeroNormal, error));

    SceneSnapshot invalidMaterial = ValidScene();
    invalidMaterial.materials[0].roughness = 1.5F;
    assert(!ValidateSceneSnapshot(invalidMaterial, error));

    SceneSnapshot missingResource = ValidScene();
    missingResource.instances[0].meshIndex = 1U;
    assert(!ValidateSceneSnapshot(missingResource, error));

    SceneSnapshot singularTransform = ValidScene();
    singularTransform.instances[0].world[0] = 0.0F;
    singularTransform.instances[0].world[5] = 0.0F;
    singularTransform.instances[0].world[10] = 0.0F;
    assert(!ValidateSceneSnapshot(singularTransform, error));

    SceneSnapshot invalidCamera = ValidScene();
    invalidCamera.camera.elapsedSeconds = std::numeric_limits<float>::quiet_NaN();
    assert(!ValidateSceneSnapshot(invalidCamera, error));

    return 0;
}
