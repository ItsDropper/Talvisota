#include "talvisota/core/GameSession.hpp"

#include <utility>

namespace talvisota::core
{
bool GameSession::TransitionTo(const GamePhase next) noexcept
{
    bool allowed = false;
    switch (m_phase)
    {
    case GamePhase::Cold:
        allowed = next == GamePhase::Initializing;
        break;
    case GamePhase::Initializing:
        allowed = next == GamePhase::Frontend || next == GamePhase::Failed ||
                  next == GamePhase::ShuttingDown;
        break;
    case GamePhase::Frontend:
        allowed = next == GamePhase::Loading || next == GamePhase::ShuttingDown;
        break;
    case GamePhase::Loading:
        allowed = next == GamePhase::Playing || next == GamePhase::Frontend ||
                  next == GamePhase::Failed || next == GamePhase::ShuttingDown;
        break;
    case GamePhase::Playing:
        allowed = next == GamePhase::Paused || next == GamePhase::Frontend ||
                  next == GamePhase::Failed || next == GamePhase::ShuttingDown;
        break;
    case GamePhase::Paused:
        allowed = next == GamePhase::Playing || next == GamePhase::Frontend ||
                  next == GamePhase::Failed || next == GamePhase::ShuttingDown;
        break;
    case GamePhase::Failed:
        allowed = next == GamePhase::ShuttingDown;
        break;
    case GamePhase::ShuttingDown:
        allowed = next == GamePhase::Stopped;
        break;
    case GamePhase::Stopped:
        allowed = false;
        break;
    }

    if (!allowed)
        return false;

    m_phase = next;
    return true;
}

bool GameSession::BeginInitialization() noexcept
{
    return TransitionTo(GamePhase::Initializing);
}

bool GameSession::FinishInitialization() noexcept
{
    return TransitionTo(GamePhase::Frontend);
}

bool GameSession::BeginLoading() noexcept
{
    return TransitionTo(GamePhase::Loading);
}

bool GameSession::FinishLoading() noexcept
{
    return TransitionTo(GamePhase::Playing);
}

bool GameSession::CancelLoading() noexcept
{
    return TransitionTo(GamePhase::Frontend);
}

bool GameSession::Pause() noexcept
{
    return TransitionTo(GamePhase::Paused);
}

bool GameSession::Resume() noexcept
{
    return TransitionTo(GamePhase::Playing);
}

bool GameSession::ReturnToFrontend() noexcept
{
    return TransitionTo(GamePhase::Frontend);
}

bool GameSession::Fail(const std::string_view reason)
{
    if (reason.empty() || m_phase == GamePhase::Cold ||
        m_phase == GamePhase::ShuttingDown || m_phase == GamePhase::Stopped ||
        m_phase == GamePhase::Failed)
        return false;

    std::string ownedReason(reason);
    if (!TransitionTo(GamePhase::Failed))
        return false;

    m_failureReason = std::move(ownedReason);
    return true;
}

bool GameSession::BeginShutdown() noexcept
{
    return TransitionTo(GamePhase::ShuttingDown);
}

bool GameSession::FinishShutdown() noexcept
{
    return TransitionTo(GamePhase::Stopped);
}
} // namespace talvisota::core
