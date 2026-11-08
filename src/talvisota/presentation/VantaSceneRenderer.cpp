#include "talvisota/presentation/VantaSceneRenderer.hpp"

#include "vanta/renderer/D3D11Renderer.hpp"
#include "vanta/renderer/D3D12Renderer.hpp"
#include "vanta/renderer/SceneSubmission.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace talvisota::presentation
{
namespace
{
class VantaSceneRenderer final : public ISceneRenderer
{
public:
    explicit VantaSceneRenderer(const RendererBackend backend)
        : m_backend(backend)
    {
    }

    bool Initialize(void* nativeWindow, const int width, const int height,
        std::string& error) override
    {
        if (nativeWindow == nullptr || width <= 0 || height <= 0) {
            error = "Renderer initialization requires a native window and positive dimensions.";
            return false;
        }

        bool initialized = false;
        if (m_backend == RendererBackend::Direct3D12) {
            auto renderer = std::make_unique<vanta::D3D12Renderer>();
            initialized = renderer->Initialize(static_cast<HWND>(nativeWindow), width, height);
            if (initialized)
                m_d3d12 = std::move(renderer);
        } else {
            auto renderer = std::make_unique<vanta::D3D11Renderer>();
            initialized = renderer->Initialize(static_cast<HWND>(nativeWindow), width, height);
            if (initialized)
                m_d3d11 = std::move(renderer);
        }

        if (!initialized) {
            error = "VantaEngine could not initialize the requested Direct3D backend.";
            m_d3d11.reset();
            m_d3d12.reset();
            return false;
        }
        error.clear();
        return true;
    }

    bool Resize(const int width, const int height, std::string& error) override
    {
        if (width <= 0 || height <= 0) {
            error = "Renderer dimensions must be positive.";
            return false;
        }
        const bool resized = m_d3d12 ? m_d3d12->Resize(width, height) :
            (m_d3d11 && m_d3d11->Resize(width, height));
        if (!resized) {
            error = IsDeviceLost()
                ? "The graphics device was removed during resize."
                : "The renderer rejected the requested resize.";
            return false;
        }
        error.clear();
        return true;
    }

    bool ReadbackPresentedFrame(std::vector<std::uint8_t>& bgra,
        std::uint32_t& width, std::uint32_t& height, std::string& error) override
    {
        if (!m_d3d12) {
            error = "Presented-frame readback is currently implemented for the D3D12 backend only.";
            return false;
        }
        UINT nativeWidth = 0U, nativeHeight = 0U;
        const bool captured = m_d3d12->ReadbackPresentedFrame(bgra, nativeWidth, nativeHeight, error);
        width = nativeWidth;
        height = nativeHeight;
        return captured;
    }

    bool Render(const SceneSnapshot& snapshot, std::string& error) override
    {
        if (!m_d3d11 && !m_d3d12) {
            error = "Renderer must be initialized before rendering.";
            return false;
        }
        if (IsDeviceLost()) {
            error = "The graphics device has been removed; recreate the renderer before resuming.";
            return false;
        }
        if (!ValidateSceneSnapshot(snapshot, error))
            return false;

        // Static world resources are uploaded to SceneSubmission once. Rebuilding these
        // handles every frame caused every mesh/material/instance to be copied and recreated
        // on the CPU before the renderer could even begin its GPU work.
        const bool rebuildResources = m_meshes.size() != snapshot.meshes.size() ||
            m_materials.size() != snapshot.materials.size();
        if (rebuildResources) {
            m_scene.Clear();
            m_meshes.clear();
            m_materials.clear();
            m_instances.clear();
        }

        if (m_meshes.empty() && !snapshot.meshes.empty()) {
            m_meshes.reserve(snapshot.meshes.size());
            for (const RenderMesh& source : snapshot.meshes) {
                vanta::renderer::MeshDescription mesh;
                mesh.vertices.reserve(source.vertices.size());
                mesh.indices = source.indices;
                for (const RenderVertex& vertex : source.vertices) {
                    mesh.vertices.push_back({
                        {vertex.position[0], vertex.position[1], vertex.position[2]},
                        {vertex.normal[0], vertex.normal[1], vertex.normal[2]},
                        vertex.u, vertex.v
                    });
                }
                const auto handle = m_scene.CreateMesh(std::move(mesh));
                if (!handle.IsValid()) {
                    error = "VantaEngine rejected a game-owned mesh during scene translation.";
                    ResetSubmission();
                    return false;
                }
                m_meshes.push_back(handle);
            }
        }

        if (m_materials.empty() && !snapshot.materials.empty()) {
            m_materials.reserve(snapshot.materials.size());
            for (const RenderMaterial& source : snapshot.materials) {
                vanta::renderer::MaterialDescription material;
                material.baseColor = {
                    source.baseColor[0], source.baseColor[1], source.baseColor[2]
                };
                material.roughness = source.roughness;
                material.metallic = source.metallic;
                material.baseColorTexture = source.baseColorTexture;
                material.normalTexture = source.normalTexture;
                material.roughnessTexture = source.roughnessTexture;
                material.metallicTexture = source.metallicTexture;
                material.ambientOcclusionTexture = source.ambientOcclusionTexture;
                material.packedMetallicRoughness = source.packedMetallicRoughness;
                const auto handle = m_scene.CreateMaterial(std::move(material));
                if (!handle.IsValid()) {
                    error = "VantaEngine rejected a game-owned material during scene translation.";
                    ResetSubmission();
                    return false;
                }
                m_materials.push_back(handle);
            }
        }

        if (m_instances.size() != snapshot.instances.size()) {
            for (const auto handle : m_instances)
                (void)m_scene.DestroyInstance(handle);
            m_instances.clear();
            m_instances.reserve(snapshot.instances.size());
            for (const RenderInstance& source : snapshot.instances) {
                vanta::math::Mat4 world;
                std::copy(source.world.begin(), source.world.end(), world.elements);
                const auto handle = m_scene.CreateInstance(
                    m_meshes[source.meshIndex], m_materials[source.materialIndex], world);
                if (!handle.IsValid()) {
                    error = "VantaEngine rejected a game-owned scene instance.";
                    ResetSubmission();
                    return false;
                }
                m_instances.push_back(handle);
                if (!m_scene.SetVisible(handle, source.visible)) {
                    error = "VantaEngine rejected an instance visibility update.";
                    ResetSubmission();
                    return false;
                }
            }
        } else {
            for (std::size_t index = 0; index < snapshot.instances.size(); ++index) {
                const RenderInstance& source = snapshot.instances[index];
                vanta::math::Mat4 world;
                std::copy(source.world.begin(), source.world.end(), world.elements);
                if (!m_scene.SetTransform(m_instances[index], world) ||
                    !m_scene.SetVisible(m_instances[index], source.visible)) {
                    error = "VantaEngine rejected a persistent scene-instance update.";
                    ResetSubmission();
                    return false;
                }
            }
        }

        vanta::renderer::FrameView view;
        std::copy(snapshot.camera.view.begin(), snapshot.camera.view.end(), view.view.elements);
        std::copy(snapshot.camera.projection.begin(), snapshot.camera.projection.end(),
            view.projection.elements);
        view.cameraPosition = {
            snapshot.camera.position[0], snapshot.camera.position[1], snapshot.camera.position[2]
        };
        view.elapsedSeconds = snapshot.camera.elapsedSeconds;
        view.frameNumber = snapshot.camera.frameNumber;
        view.environment.enabled = snapshot.environment.enabled;
        view.environment.sunDirection = {snapshot.environment.sunDirection[0],
            snapshot.environment.sunDirection[1], snapshot.environment.sunDirection[2]};
        view.environment.sunColor = {snapshot.environment.sunColor[0],
            snapshot.environment.sunColor[1], snapshot.environment.sunColor[2]};
        view.environment.sunIntensity = snapshot.environment.sunIntensity;
        view.environment.ambientColor = {snapshot.environment.ambientColor[0],
            snapshot.environment.ambientColor[1], snapshot.environment.ambientColor[2]};
        view.environment.ambientIntensity = snapshot.environment.ambientIntensity;
        view.environment.fogColor = {snapshot.environment.fogColor[0],
            snapshot.environment.fogColor[1], snapshot.environment.fogColor[2]};
        view.environment.fogDensity = snapshot.environment.fogDensity;
        view.environment.fogStart = snapshot.environment.fogStart;
        view.environment.windDirectionX = snapshot.environment.windDirection[0];
        view.environment.windDirectionZ = snapshot.environment.windDirection[1];
        view.environment.windStrength = snapshot.environment.windStrength;
        const vanta::renderer::SceneFrame frame = m_scene.BuildFrame(view);

        const bool rendered = m_d3d12 ? m_d3d12->Render(frame) : m_d3d11->Render(frame);
        if (!rendered) {
            if (m_d3d12 && !m_d3d12->LastRenderError().empty())
                error = m_d3d12->LastRenderError();
            else
                error = IsDeviceLost()
                    ? "The graphics device was removed while rendering the submitted scene."
                    : "VantaEngine failed to render the submitted scene frame.";
            return false;
        }

        if (IsDeviceLost()) {
            error = "The graphics device was removed while rendering the submitted scene.";
            return false;
        }
        error.clear();
        return true;
    }

    bool IsDeviceLost() const noexcept override
    {
        return (m_d3d12 && m_d3d12->IsDeviceLost()) ||
               (m_d3d11 && m_d3d11->IsDeviceLost());
    }

private:
    void ResetSubmission() noexcept
    {
        m_scene.Clear();
        m_meshes.clear();
        m_materials.clear();
        m_instances.clear();
    }

    RendererBackend m_backend;
    std::unique_ptr<vanta::D3D11Renderer> m_d3d11;
    std::unique_ptr<vanta::D3D12Renderer> m_d3d12;
    vanta::renderer::SceneSubmission m_scene;
    std::vector<vanta::renderer::MeshHandle> m_meshes;
    std::vector<vanta::renderer::MaterialHandle> m_materials;
    std::vector<vanta::renderer::InstanceHandle> m_instances;
};
} // namespace

std::unique_ptr<ISceneRenderer> CreateVantaSceneRenderer(const RendererBackend backend)
{
    return std::make_unique<VantaSceneRenderer>(backend);
}
} // namespace talvisota::presentation
