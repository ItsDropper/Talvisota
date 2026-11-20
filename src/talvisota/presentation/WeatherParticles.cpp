#include "talvisota/presentation/WeatherParticles.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace talvisota::presentation
{
namespace
{
[[nodiscard]] float UnitFloat(std::uint32_t value) noexcept
{
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return static_cast<float>(value & 0x00ffffffU) / 16777215.0F;
}

[[nodiscard]] float WrapCentered(const float value, const float halfExtent) noexcept
{
    const float period = halfExtent * 2.0F;
    float wrapped = std::fmod(value + halfExtent, period);
    if (wrapped < 0.0F) wrapped += period;
    return wrapped - halfExtent;
}

[[nodiscard]] RenderMesh MakeSnowflakeMesh()
{
    RenderMesh mesh;
    mesh.vertices = {
        {{0.0F, 0.045F, 0.0F}, {0.0F, 1.0F, 0.0F}, 0.5F, 0.0F},
        {{0.0F, -0.045F, 0.0F}, {0.0F, -1.0F, 0.0F}, 0.5F, 1.0F},
        {{-0.025F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}, 0.0F, 0.5F},
        {{0.025F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1.0F, 0.5F},
        {{0.0F, 0.0F, -0.025F}, {0.0F, 0.0F, -1.0F}, 0.5F, 0.0F},
        {{0.0F, 0.0F, 0.025F}, {0.0F, 0.0F, 1.0F}, 0.5F, 1.0F}
    };
    mesh.indices = {
        0U, 5U, 3U, 0U, 2U, 5U, 0U, 4U, 2U, 0U, 3U, 4U,
        1U, 3U, 5U, 1U, 5U, 2U, 1U, 2U, 4U, 1U, 4U, 3U
    };
    return mesh;
}
} // namespace

bool SnowfallSystem::Initialize(SceneSnapshot& scene,
    std::vector<float>& instanceMaxDrawDistances, const std::size_t flakeCount,
    std::string& error)
{
    if (!m_offsets.empty())
    {
        error = "Snowfall system is already initialized.";
        return false;
    }
    if (flakeCount == 0U || flakeCount > 4096U ||
        scene.instances.size() != instanceMaxDrawDistances.size() ||
        scene.meshes.size() >= std::numeric_limits<std::uint32_t>::max() ||
        scene.materials.size() >= std::numeric_limits<std::uint32_t>::max() ||
        scene.instances.size() > std::numeric_limits<std::uint32_t>::max() - flakeCount)
    {
        error = "Snowfall initialization exceeds scene, culling, or particle-count limits.";
        return false;
    }

    const auto meshIndex = static_cast<std::uint32_t>(scene.meshes.size());
    const auto materialIndex = static_cast<std::uint32_t>(scene.materials.size());
    const auto firstInstance = static_cast<std::uint32_t>(scene.instances.size());

    std::vector<Offset> offsets;
    offsets.reserve(flakeCount);
    for (std::size_t index = 0U; index < flakeCount; ++index)
    {
        const auto seed = static_cast<std::uint32_t>(index) + 0x9e3779b9U;
        offsets.push_back({
            (UnitFloat(seed) * 2.0F - 1.0F) * 18.0F,
            UnitFloat(seed ^ 0x68bc21ebU) * 24.0F,
            (UnitFloat(seed ^ 0x02e5be93U) * 2.0F - 1.0F) * 24.0F,
            UnitFloat(seed ^ 0x967a889bU) * 2.0F - 1.0F,
            0.45F + UnitFloat(seed ^ 0x51ed270bU) * 1.05F,
            0.35F + UnitFloat(seed ^ 0x94d049bbU) * 0.65F,
            UnitFloat(seed ^ 0x369dea0fU) * 6.28318530718F,
            UnitFloat(seed ^ 0xdb4f0b91U) * 6.28318530718F
        });
    }

    scene.meshes.push_back(MakeSnowflakeMesh());
    RenderMaterial snowMaterial;
    snowMaterial.baseColor = {0.88F, 0.94F, 1.0F};
    snowMaterial.roughness = 1.0F;
    snowMaterial.metallic = 0.0F;
    scene.materials.push_back(std::move(snowMaterial));

    for (std::size_t index = 0U; index < flakeCount; ++index)
    {
        RenderInstance instance;
        instance.meshIndex = meshIndex;
        instance.materialIndex = materialIndex;
        instance.visible = false;
        scene.instances.push_back(std::move(instance));
        instanceMaxDrawDistances.push_back(60.0F);
    }

    m_firstInstance = firstInstance;
    m_meshIndex = meshIndex;
    m_materialIndex = materialIndex;
    m_offsets = std::move(offsets);
    error.clear();
    return true;
}

bool SnowfallSystem::Update(SceneSnapshot& scene,
    const std::array<float, 3>& cameraPosition, const double elapsedSeconds,
    const std::array<float, 2>& windDirection, const float windStrength,
    const bool enabled, std::string& error) const
{
    if (m_offsets.empty() || !std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 ||
        !std::isfinite(windStrength) || windStrength < 0.0F || windStrength > 1.0F ||
        !std::isfinite(windDirection[0]) || !std::isfinite(windDirection[1]))
    {
        error = "Snowfall update requires initialized particles and finite weather parameters.";
        return false;
    }
    for (const float coordinate : cameraPosition)
    {
        if (!std::isfinite(coordinate))
        {
            error = "Snowfall camera position must be finite.";
            return false;
        }
    }
    const std::size_t end = static_cast<std::size_t>(m_firstInstance) + m_offsets.size();
    if (end > scene.instances.size() || m_meshIndex >= scene.meshes.size() ||
        m_materialIndex >= scene.materials.size())
    {
        error = "Snowfall scene resources were removed or reordered after initialization.";
        return false;
    }
    for (std::size_t index = 0U; index < m_offsets.size(); ++index)
    {
        const RenderInstance& instance = scene.instances[static_cast<std::size_t>(m_firstInstance) + index];
        if (instance.meshIndex != m_meshIndex || instance.materialIndex != m_materialIndex)
        {
            error = "Snowfall instance resources were reordered after initialization.";
            return false;
        }
    }

    const float time = static_cast<float>(std::fmod(elapsedSeconds, 100000.0));
    for (std::size_t index = 0U; index < m_offsets.size(); ++index)
    {
        const Offset& offset = m_offsets[index];
        // Snow crystals fall at varied terminal speeds; horizontal transport and
        // turbulent gusts scale with the authored wind instead of moving in calm air.
        const float gust = (std::sin(time * 0.31F + offset.phase) * 0.72F +
            std::sin(time * 0.83F + offset.phase * 1.7F) * 0.28F) * windStrength * 0.34F;
        const float driftTime = time * windStrength * 1.8F;
        const float lateralDrift = offset.drift * windStrength * 2.2F;
        const float x = cameraPosition[0] + WrapCentered(
            offset.x + windDirection[0] * driftTime + lateralDrift * driftTime * 0.12F + gust,
            20.0F);
        const float fallCycle = std::fmod(time * offset.fallSpeed + offset.yPhase, 24.0F);
        const float y = cameraPosition[1] + 12.0F - fallCycle;
        const float z = cameraPosition[2] + WrapCentered(
            offset.z + windDirection[1] * driftTime + lateralDrift * driftTime * 0.08F +
                std::sin(time * 0.24F + offset.phase * 0.73F) * windStrength * 0.55F,
            26.0F);
        const float angle = offset.rotation + time * (0.18F + offset.scale * 0.12F) + gust * 0.35F;
        const float cosine = std::cos(angle) * offset.scale;
        const float sine = std::sin(angle) * offset.scale;

        RenderInstance& instance = scene.instances[static_cast<std::size_t>(m_firstInstance) + index];
        instance.world = {cosine, 0.0F, -sine, 0.0F,
            0.0F, offset.scale, 0.0F, 0.0F,
            sine, 0.0F, cosine, 0.0F, x, y, z, 1.0F};
        instance.visible = enabled;
    }
    error.clear();
    return true;
}
} // namespace talvisota::presentation
