#include "talvisota/presentation/VantaSceneRenderer.hpp"

#include "talvisota/presentation/SceneValidation.hpp"

#include "vanta/renderer/D3D11Renderer.hpp"
#include "vanta/renderer/D3D12Renderer.hpp"
#include "vanta/renderer/SceneSubmission.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

namespace talvisota::presentation
{
// Translation performance model (see README "Optional VantaEngine renderer adapter"):
//
// A snapshot contains meshes, materials, and instances. Meshes and materials are immutable
// after creation in the engine's public scene model ("instances own only references and
// transforms"), so re-translating them every frame would copy and re-validate tens of
// megabytes of vertex/index data and churn every resource slot per frame.
//
// The adapter therefore keeps a persistent translation:
//  * Per frame it validates the camera, environment, and instances (cheap, unchanged
//    error contract) and applies only instance diffs through SetResources, SetTransform,
//    and SetVisible; an instance-count change rebuilds only the instance slots while mesh
//    and material resources stay untouched.
//  * Meshes and materials are (re)translated only when their game-owned identity changes:
//    storage pointers, element counts, or scalar/texture values. On identity change the
//    full snapshot validation runs before any engine resource is rebuilt, so every new or
//    replaced payload is rejected with the exact ValidateSceneSnapshot diagnostics.
//
// Mesh byte payloads are not hashed per frame. Callers must not mutate mesh vertex/index
// storage in place between submissions without replacing the storage; snapshots translated
// from freshly authored content (region load, model import) always carry new storage and
// are picked up through validation. This mirrors the engine contract that mesh resources
// are immutable once created.
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

        // Camera and environment are validated every frame; they may change freely between
        // submissions and only feed the engine frame view, not any persistent resource.
        if (!detail::ValidateCameraFrame(snapshot.camera, error))
            return false;
        if (!detail::ValidateEnvironment(snapshot.environment, error))
            return false;
        if (!detail::ValidatePointLights(snapshot, error))
            return false;

        if (SceneResourcesMatch(snapshot)) {
            if (!detail::ValidateInstances(snapshot, error))
                return false;
            if (SyncInstances(snapshot, error) == SceneSyncResult::Failed) {
                ResetScene();
                return false;
            }
        } else {
            // Identical validation order to ValidateSceneSnapshot: the diagnostics for
            // meshes, materials, and instances are reported exactly as before.
            if (!detail::ValidateMeshes(snapshot, error) ||
                !detail::ValidateMaterials(snapshot, error))
                return false;
            if (!detail::ValidateInstances(snapshot, error))
                return false;
            if (!TranslateScene(snapshot, error)) {
                ResetScene();
                return false;
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
        view.pointLightCount = static_cast<std::uint32_t>(snapshot.pointLights.size());
        for (std::size_t index = 0U; index < snapshot.pointLights.size(); ++index)
        {
            const RenderPointLight& source = snapshot.pointLights[index];
            view.pointLights[index] = {
                {source.position[0], source.position[1], source.position[2]},
                source.range,
                {source.color[0], source.color[1], source.color[2]},
                source.intensity
            };
        }
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
    // Identity of the immutable engine resources translated from a snapshot. Storage
    // identity is defined by element storage and size, matching the engine-side model
    // where mesh/material resources are deduplicated and cached by pointer identity.
    struct MeshIdentity final
    {
        const RenderVertex* vertices = nullptr;
        std::size_t vertexCount = 0U;
        const std::uint32_t* indices = nullptr;
        std::size_t indexCount = 0U;

        [[nodiscard]] bool operator==(const MeshIdentity&) const noexcept = default;
    };

    struct MaterialIdentity final
    {
        std::array<float, 3> baseColor{};
        float roughness = 0.0F;
        float metallic = 0.0F;
        std::array<float, 3> emissive{};
        float normalScale = 1.0F;
        float occlusionStrength = 1.0F;
        float alphaCutoff = 0.5F;
        RenderAlphaMode alphaMode = RenderAlphaMode::Opaque;
        bool doubleSided = false;
        std::array<std::string, 6> texturePaths;
        bool packedMetallicRoughness = false;
        bool terrain = false;
        bool snowSurface = false;

        static MaterialIdentity Of(const RenderMaterial& material)
        {
            return MaterialIdentity{material.baseColor, material.roughness, material.metallic,
                material.emissive, material.normalScale, material.occlusionStrength,
                material.alphaCutoff, material.alphaMode, material.doubleSided,
                {material.baseColorTexture, material.normalTexture, material.roughnessTexture,
                    material.metallicTexture, material.ambientOcclusionTexture, material.emissiveTexture},
                material.packedMetallicRoughness, material.terrain, material.snowSurface};
        }

        // Zero-allocation comparison against the translated state. Materials are small,
        // so their full values are compared every frame; this catches scalar, flag, or
        // texture reference changes (including in-place mutation) without any bookkeeping.
        [[nodiscard]] bool Matches(const RenderMaterial& material) const
        {
            return baseColor == material.baseColor && roughness == material.roughness &&
                metallic == material.metallic && emissive == material.emissive &&
                normalScale == material.normalScale && occlusionStrength == material.occlusionStrength &&
                alphaCutoff == material.alphaCutoff && alphaMode == material.alphaMode &&
                doubleSided == material.doubleSided &&
                packedMetallicRoughness == material.packedMetallicRoughness &&
                terrain == material.terrain && snowSurface == material.snowSurface &&
                texturePaths[0] == material.baseColorTexture &&
                texturePaths[1] == material.normalTexture &&
                texturePaths[2] == material.roughnessTexture &&
                texturePaths[3] == material.metallicTexture &&
                texturePaths[4] == material.ambientOcclusionTexture &&
                texturePaths[5] == material.emissiveTexture;
        }
    };

    // Last-submitted per-instance state used for the cheap per-frame diff sync.
    struct InstanceState final
    {
        vanta::renderer::InstanceHandle handle{};
        std::uint32_t meshIndex = 0U;
        std::uint32_t materialIndex = 0U;
        std::array<float, 16> world{};
        bool visible = true;
    };

    enum class SceneSyncResult { Synchronized, Failed };

    [[nodiscard]] bool SceneResourcesMatch(const SceneSnapshot& snapshot) const
    {
        if (m_meshIdentity.size() != snapshot.meshes.size() ||
            m_materialIdentity.size() != snapshot.materials.size() ||
            m_meshHandles.size() != snapshot.meshes.size() ||
            m_materialHandles.size() != snapshot.materials.size())
            return false;

        for (std::size_t meshIndex = 0; meshIndex < snapshot.meshes.size(); ++meshIndex) {
            const RenderMesh& mesh = snapshot.meshes[meshIndex];
            const MeshIdentity identity{mesh.vertices.data(), mesh.vertices.size(),
                mesh.indices.data(), mesh.indices.size()};
            if (identity != m_meshIdentity[meshIndex] ||
                !m_scene.Contains(m_meshHandles[meshIndex]))
                return false;
        }

        for (std::size_t materialIndex = 0; materialIndex < snapshot.materials.size();
             ++materialIndex) {
            // Materials are small, so their full values are compared every frame; this
            // catches scalar or texture-reference changes without any bookkeeping.
            if (!m_materialIdentity[materialIndex].Matches(snapshot.materials[materialIndex]) ||
                !m_scene.Contains(m_materialHandles[materialIndex]))
                return false;
        }
        return true;
    }

    void ResetScene() noexcept
    {
        m_scene.Clear();
        m_meshHandles.clear();
        m_materialHandles.clear();
        m_instanceHandles.clear();
        m_instanceStates.clear();
        m_meshIdentity.clear();
        m_materialIdentity.clear();
    }

    [[nodiscard]] bool TranslateScene(const SceneSnapshot& snapshot, std::string& error)
    {
        // A full rebuild is the rare path (game content load/replace); it is the only
        // operation that reaches Clear() and engine resource re-creation.
        ResetScene();

        m_meshIdentity.reserve(snapshot.meshes.size());
        m_meshHandles.reserve(snapshot.meshes.size());
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
                return false;
            }
            m_meshIdentity.push_back(MeshIdentity{source.vertices.data(), source.vertices.size(),
                source.indices.data(), source.indices.size()});
            m_meshHandles.push_back(handle);
        }

        m_materialIdentity.reserve(snapshot.materials.size());
        m_materialHandles.reserve(snapshot.materials.size());
        for (const RenderMaterial& source : snapshot.materials) {
            vanta::renderer::MaterialDescription material;
            material.baseColor = {
                source.baseColor[0], source.baseColor[1], source.baseColor[2]
            };
            material.roughness = source.roughness;
            material.metallic = source.metallic;
            material.emissive = {source.emissive[0], source.emissive[1], source.emissive[2]};
            material.normalScale = source.normalScale;
            material.occlusionStrength = source.occlusionStrength;
            material.alphaCutoff = source.alphaCutoff;
            material.alphaMode = source.alphaMode == RenderAlphaMode::Mask
                ? vanta::renderer::MaterialAlphaMode::Mask : source.alphaMode == RenderAlphaMode::Blend
                    ? vanta::renderer::MaterialAlphaMode::Blend : vanta::renderer::MaterialAlphaMode::Opaque;
            material.doubleSided = source.doubleSided;
            material.baseColorTexture = source.baseColorTexture;
            material.normalTexture = source.normalTexture;
            material.roughnessTexture = source.roughnessTexture;
            material.metallicTexture = source.metallicTexture;
            material.ambientOcclusionTexture = source.ambientOcclusionTexture;
            material.emissiveTexture = source.emissiveTexture;
            material.packedMetallicRoughness = source.packedMetallicRoughness;
            material.terrain = source.terrain;
            material.snowSurface = source.snowSurface;
            const auto handle = m_scene.CreateMaterial(std::move(material));
            if (!handle.IsValid()) {
                error = "VantaEngine rejected a game-owned material during scene translation.";
                return false;
            }
            m_materialIdentity.push_back(MaterialIdentity::Of(source));
            m_materialHandles.push_back(handle);
        }

        return RebuildInstances(snapshot, error) == SceneSyncResult::Synchronized;
    }

    // (Re)creates the engine instance slots from the snapshot. Used both after a full
    // mesh/material translation and for an instance-count change (spawn/despawn), where
    // existing mesh and material resources stay untouched.
    [[nodiscard]] SceneSyncResult RebuildInstances(const SceneSnapshot& snapshot, std::string& error)
    {
        for (const auto handle : m_instanceHandles)
            (void)m_scene.DestroyInstance(handle);
        m_instanceHandles.clear();
        m_instanceStates.clear();
        m_instanceHandles.reserve(snapshot.instances.size());
        m_instanceStates.reserve(snapshot.instances.size());
        for (const RenderInstance& source : snapshot.instances) {
            vanta::math::Mat4 world;
            std::copy(source.world.begin(), source.world.end(), world.elements);
            const auto handle = m_scene.CreateInstance(m_meshHandles[source.meshIndex],
                m_materialHandles[source.materialIndex], world);
            if (!handle.IsValid()) {
                error = "VantaEngine rejected a game-owned scene instance.";
                return SceneSyncResult::Failed;
            }
            InstanceState state;
            state.handle = handle;
            state.meshIndex = source.meshIndex;
            state.materialIndex = source.materialIndex;
            std::copy(source.world.begin(), source.world.end(), state.world.begin());
            state.visible = source.visible;
            m_instanceHandles.push_back(handle);
            m_instanceStates.push_back(state);
            // CreateInstance already stores and validates the world transform; only the
            // default-visible flag can differ from the snapshot here.
            if (!m_scene.SetVisible(handle, source.visible)) {
                error = "VantaEngine rejected an instance visibility update.";
                return SceneSyncResult::Failed;
            }
        }
        return SceneSyncResult::Synchronized;
    }

    // Applies only the instance fields that changed since the previous submission. The
    // engine validates and rejects every call, so this path stays as safe as a rebuild
    // while doing no engine work at all when the game-owned instances are unchanged.
    [[nodiscard]] SceneSyncResult SyncInstances(const SceneSnapshot& snapshot, std::string& error)
    {
        if (m_instanceHandles.size() != snapshot.instances.size() ||
            m_instanceStates.size() != snapshot.instances.size()) {
            // An instance-count change (spawn/despawn) rebuilds only the instance slots;
            // the persistent mesh and material resources remain valid and untouched.
            return RebuildInstances(snapshot, error);
        }

        for (std::size_t instanceIndex = 0; instanceIndex < snapshot.instances.size();
             ++instanceIndex) {
            const RenderInstance& source = snapshot.instances[instanceIndex];
            const InstanceState& state = m_instanceStates[instanceIndex];
            const bool resourcesChanged =
                source.meshIndex != state.meshIndex || source.materialIndex != state.materialIndex;
            const bool worldChanged =
                std::memcmp(source.world.data(), state.world.data(), sizeof(state.world)) != 0;
            const bool visibilityChanged = source.visible != state.visible;
            if (!resourcesChanged && !worldChanged && !visibilityChanged)
                continue;

            bool updated = true;
            if (resourcesChanged)
                updated = m_scene.SetResources(state.handle, m_meshHandles[source.meshIndex],
                    m_materialHandles[source.materialIndex]) && updated;
            if (worldChanged) {
                vanta::math::Mat4 world;
                std::copy(source.world.begin(), source.world.end(), world.elements);
                updated = m_scene.SetTransform(state.handle, world) && updated;
            }
            if (visibilityChanged)
                updated = m_scene.SetVisible(state.handle, source.visible) && updated;

            if (!updated) {
                error = "VantaEngine rejected a persistent scene-instance update.";
                return SceneSyncResult::Failed;
            }

            InstanceState newState;
            newState.handle = state.handle;
            newState.meshIndex = source.meshIndex;
            newState.materialIndex = source.materialIndex;
            std::copy(source.world.begin(), source.world.end(), newState.world.begin());
            newState.visible = source.visible;
            m_instanceStates[instanceIndex] = newState;
        }
        return SceneSyncResult::Synchronized;
    }

    RendererBackend m_backend;
    std::unique_ptr<vanta::D3D11Renderer> m_d3d11;
    std::unique_ptr<vanta::D3D12Renderer> m_d3d12;
    vanta::renderer::SceneSubmission m_scene;
    // Persistent translation state; index i corresponds to snapshot element i.
    std::vector<vanta::renderer::MeshHandle> m_meshHandles;
    std::vector<vanta::renderer::MaterialHandle> m_materialHandles;
    std::vector<vanta::renderer::InstanceHandle> m_instanceHandles;
    std::vector<MeshIdentity> m_meshIdentity;
    std::vector<MaterialIdentity> m_materialIdentity;
    std::vector<InstanceState> m_instanceStates;
};
} // namespace

std::unique_ptr<ISceneRenderer> CreateVantaSceneRenderer(const RendererBackend backend)
{
    return std::make_unique<VantaSceneRenderer>(backend);
}
} // namespace talvisota::presentation
