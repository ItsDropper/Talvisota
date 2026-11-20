#pragma once

#include "talvisota/core/GamePhase.hpp"

#include <string>
#include <string_view>

namespace talvisota::core
{
// Owns the top-level application phase only. World, mission, entity, and renderer
// lifetimes are managed by their respective subsystems, not hidden in this class.
class GameSession final
{
public:
    [[nodiscard]] GamePhase Phase() const noexcept { return m_phase; }
    [[nodiscard]] const std::string& FailureReason() const noexcept { return m_failureReason; }

    [[nodiscard]] bool BeginInitialization() noexcept;
    [[nodiscard]] bool FinishInitialization() noexcept;
    [[nodiscard]] bool BeginLoading() noexcept;
    [[nodiscard]] bool FinishLoading() noexcept;
    [[nodiscard]] bool CancelLoading() noexcept;
    [[nodiscard]] bool Pause() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    [[nodiscard]] bool ReturnToFrontend() noexcept;
    [[nodiscard]] bool Fail(std::string_view reason);
    [[nodiscard]] bool BeginShutdown() noexcept;
    [[nodiscard]] bool FinishShutdown() noexcept;

private:
    [[nodiscard]] bool TransitionTo(GamePhase next) noexcept;

    GamePhase m_phase = GamePhase::Cold;
    std::string m_failureReason;
};
} // namespace talvisota::core
