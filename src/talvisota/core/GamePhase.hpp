#pragma once

#include <cstdint>
#include <string_view>

namespace talvisota::core
{
enum class GamePhase : std::uint8_t
{
    Cold,
    Initializing,
    Frontend,
    Loading,
    Playing,
    Paused,
    ShuttingDown,
    Stopped,
    Failed
};

[[nodiscard]] constexpr std::string_view ToString(GamePhase phase) noexcept
{
    switch (phase)
    {
    case GamePhase::Cold: return "Cold";
    case GamePhase::Initializing: return "Initializing";
    case GamePhase::Frontend: return "Frontend";
    case GamePhase::Loading: return "Loading";
    case GamePhase::Playing: return "Playing";
    case GamePhase::Paused: return "Paused";
    case GamePhase::ShuttingDown: return "ShuttingDown";
    case GamePhase::Stopped: return "Stopped";
    case GamePhase::Failed: return "Failed";
    }
    return "Unknown";
}
} // namespace talvisota::core
