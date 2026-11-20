#pragma once

#include "talvisota/presentation/SceneRenderer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace talvisota::presentation
{
class SnowfallSystem final
{
public:
    [[nodiscard]] bool Initialize(SceneSnapshot& scene,
        std::vector<float>& instanceMaxDrawDistances, std::size_t flakeCount,
        std::string& error);
    [[nodiscard]] bool Update(SceneSnapshot& scene,
        const std::array<float, 3>& cameraPosition, double elapsedSeconds,
        const std::array<float, 2>& windDirection, float windStrength,
        bool enabled, std::string& error) const;

    [[nodiscard]] std::size_t FlakeCount() const noexcept { return m_offsets.size(); }
    [[nodiscard]] bool IsInitialized() const noexcept { return !m_offsets.empty(); }

private:
    struct Offset final
    {
        float x = 0.0F;
        float yPhase = 0.0F;
        float z = 0.0F;
        float drift = 0.0F;
        float fallSpeed = 0.0F;
        float scale = 1.0F;
        float rotation = 0.0F;
        float phase = 0.0F;
    };

    std::uint32_t m_firstInstance = 0U;
    std::uint32_t m_meshIndex = 0U;
    std::uint32_t m_materialIndex = 0U;
    std::vector<Offset> m_offsets;
};
} // namespace talvisota::presentation
