#pragma once

#include "talvisota/world/WorldRegion.hpp"

#include <array>
#include <string>

namespace talvisota::game
{
struct PlayerConfig final
{
    float walkSpeed = 4.2F;
    float sprintSpeed = 6.5F;
    float gravity = 24.0F;
    float jumpSpeed = 7.0F;
    float capsuleRadius = 0.34F;
    float standingHeight = 1.8F;
    float eyeHeight = 1.62F;
    float maximumPitchRadians = 1.55334306F;
    float maximumFrameDeltaSeconds = 0.1F;
};

struct PlayerInput final
{
    // Movement axes are signed and clamped to [-1, 1].
    float forward = 0.0F;
    float right = 0.0F;
    // Mouse-look deltas are expressed in radians for this simulation step.
    float lookYawRadians = 0.0F;
    float lookPitchRadians = 0.0F;
    bool sprint = false;
    bool jumpPressed = false;
};

struct PlayerState final
{
    // Position is the centre of the player's feet on the terrain surface.
    std::array<float, 3> position{};
    float yawRadians = 0.0F;
    float pitchRadians = 0.0F;
    float verticalVelocity = 0.0F;
    bool grounded = true;
};

struct PlayerCamera final
{
    std::array<float, 3> position{};
    std::array<float, 3> forward{0.0F, 0.0F, 1.0F};
};

[[nodiscard]] bool ValidatePlayerConfig(const PlayerConfig& config, std::string& error);
[[nodiscard]] bool InitializePlayer(const world::WorldRegion& region,
    const PlayerConfig& config, const std::array<float, 3>& spawn,
    PlayerState& output, std::string& error);
[[nodiscard]] bool UpdatePlayer(const world::WorldRegion& region,
    const PlayerConfig& config, const PlayerInput& input, float deltaSeconds,
    PlayerState& state, std::string& error);
[[nodiscard]] PlayerCamera BuildPlayerCamera(const PlayerState& state,
    const PlayerConfig& config) noexcept;
} // namespace talvisota::game
