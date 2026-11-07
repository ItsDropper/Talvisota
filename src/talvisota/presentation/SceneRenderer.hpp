#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace talvisota::presentation
{
struct RenderVertex final
{
    std::array<float, 3> position{};
    std::array<float, 3> normal{0.0F, 1.0F, 0.0F};
    float u = 0.0F;
    float v = 0.0F;
};

struct RenderMesh final
{
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct RenderMaterial final
{
    std::array<float, 3> baseColor{1.0F, 1.0F, 1.0F};
    float roughness = 0.8F;
    float metallic = 0.0F;
};

struct RenderInstance final
{
    std::uint32_t meshIndex = 0;
    std::uint32_t materialIndex = 0;
    // Column-major 4x4 matrix, column vectors, translation in column 3.
    std::array<float, 16> world{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F
    };
    bool visible = true;
};

struct CameraFrame final
{
    std::array<float, 16> view{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F
    };
    std::array<float, 16> projection{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F
    };
    std::array<float, 3> position{};
    float elapsedSeconds = 0.0F;
    std::uint64_t frameNumber = 0;
};

struct SceneSnapshot final
{
    CameraFrame camera;
    std::vector<RenderMesh> meshes;
    std::vector<RenderMaterial> materials;
    std::vector<RenderInstance> instances;
};

[[nodiscard]] bool ValidateSceneSnapshot(const SceneSnapshot& scene, std::string& error);

enum class RendererBackend
{
    Direct3D11,
    Direct3D12
};

// Game-owned contract. This interface deliberately exposes no VantaEngine, Direct3D,
// HWND, GPU handle, or backend command type.
class ISceneRenderer
{
public:
    virtual ~ISceneRenderer() = default;
    ISceneRenderer() = default;
    ISceneRenderer(const ISceneRenderer&) = delete;
    ISceneRenderer& operator=(const ISceneRenderer&) = delete;

    [[nodiscard]] virtual bool Initialize(
        void* nativeWindow, int width, int height, std::string& error) = 0;
    [[nodiscard]] virtual bool Resize(int width, int height, std::string& error) = 0;
    [[nodiscard]] virtual bool Render(const SceneSnapshot& scene, std::string& error) = 0;
    [[nodiscard]] virtual bool IsDeviceLost() const noexcept = 0;
};

} // namespace talvisota::presentation
