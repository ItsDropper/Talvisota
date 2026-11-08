#include "talvisota/game/PlayerController.hpp"

#include <algorithm>
#include <cmath>

namespace talvisota::game
{
namespace
{
constexpr float Pi = 3.14159265358979323846F;
constexpr float GroundEpsilon = 0.025F;

bool IsFinite(const std::array<float, 3>& value) noexcept
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}

bool IsFinite(const PlayerState& state) noexcept
{
    return IsFinite(state.position) && std::isfinite(state.yawRadians) &&
        std::isfinite(state.pitchRadians) && std::isfinite(state.verticalVelocity);
}

bool IntersectsPlayer(const world::WorldRegion& region, const PlayerConfig& config,
    const std::array<float, 3>& feet, bool& intersects, std::string& error)
{
    const std::array<float, 3> minimum{
        feet[0] - config.capsuleRadius,
        feet[1] + GroundEpsilon,
        feet[2] - config.capsuleRadius
    };
    const std::array<float, 3> maximum{
        feet[0] + config.capsuleRadius,
        feet[1] + config.standingHeight,
        feet[2] + config.capsuleRadius
    };
    return world::QueryCollision(region, minimum, maximum, intersects, error);
}

bool IsInsideHorizontalBounds(const world::WorldRegion& region,
    const PlayerConfig& config, const std::array<float, 3>& position) noexcept
{
    return position[0] - config.capsuleRadius >= region.boundsMin[0] &&
        position[0] + config.capsuleRadius <= region.boundsMax[0] &&
        position[2] - config.capsuleRadius >= region.boundsMin[2] &&
        position[2] + config.capsuleRadius <= region.boundsMax[2];
}
} // namespace

bool ValidatePlayerConfig(const PlayerConfig& config, std::string& error)
{
    if (!std::isfinite(config.walkSpeed) || config.walkSpeed <= 0.0F ||
        !std::isfinite(config.sprintSpeed) || config.sprintSpeed < config.walkSpeed ||
        !std::isfinite(config.gravity) || config.gravity <= 0.0F ||
        !std::isfinite(config.jumpSpeed) || config.jumpSpeed < 0.0F ||
        !std::isfinite(config.capsuleRadius) || config.capsuleRadius <= 0.0F ||
        !std::isfinite(config.standingHeight) ||
        config.standingHeight <= config.capsuleRadius * 2.0F ||
        !std::isfinite(config.eyeHeight) || config.eyeHeight <= 0.0F ||
        config.eyeHeight >= config.standingHeight ||
        !std::isfinite(config.maximumPitchRadians) ||
        config.maximumPitchRadians <= 0.0F || config.maximumPitchRadians >= Pi * 0.5F ||
        !std::isfinite(config.maximumFrameDeltaSeconds) ||
        config.maximumFrameDeltaSeconds <= 0.0F ||
        config.maximumFrameDeltaSeconds > 0.25F)
    {
        error = "Player configuration contains invalid movement, body, camera, or timestep values.";
        return false;
    }
    error.clear();
    return true;
}

bool InitializePlayer(const world::WorldRegion& region, const PlayerConfig& config,
    const std::array<float, 3>& spawn, PlayerState& output, std::string& error)
{
    if (!ValidatePlayerConfig(config, error))
        return false;
    if (!IsFinite(spawn) || !IsInsideHorizontalBounds(region, config, spawn))
    {
        error = "Player spawn is non-finite or outside the walkable region bounds.";
        return false;
    }

    float terrainHeight = 0.0F;
    std::array<float, 3> terrainNormal{};
    if (!world::SampleTerrainHeight(region, spawn[0], spawn[2],
            terrainHeight, terrainNormal, error))
        return false;

    PlayerState candidate;
    candidate.position = {spawn[0], terrainHeight, spawn[2]};
    candidate.yawRadians = 0.0F;
    candidate.pitchRadians = 0.0F;
    candidate.verticalVelocity = 0.0F;
    candidate.grounded = true;
    if (!std::isfinite(candidate.yawRadians))
    {
        error = "Player spawn yaw must be finite.";
        return false;
    }

    bool intersects = false;
    if (!IntersectsPlayer(region, config, candidate.position, intersects, error))
        return false;
    if (intersects)
    {
        error = "Player spawn intersects an authored world collision volume.";
        return false;
    }

    output = candidate;
    error.clear();
    return true;
}

bool UpdatePlayer(const world::WorldRegion& region, const PlayerConfig& config,
    const PlayerInput& input, const float deltaSeconds, PlayerState& state,
    std::string& error)
{
    if (!ValidatePlayerConfig(config, error))
        return false;
    if (!IsFinite(state) || !std::isfinite(input.forward) ||
        !std::isfinite(input.right) || !std::isfinite(input.lookYawRadians) ||
        !std::isfinite(input.lookPitchRadians) || !std::isfinite(deltaSeconds) ||
        deltaSeconds <= 0.0F)
    {
        error = "Player update received non-finite state/input or a non-positive timestep.";
        return false;
    }

    PlayerState candidate = state;
    const float step = std::min(deltaSeconds, config.maximumFrameDeltaSeconds);
    candidate.yawRadians = std::remainder(candidate.yawRadians + input.lookYawRadians, 2.0F * Pi);
    candidate.pitchRadians = std::clamp(candidate.pitchRadians + input.lookPitchRadians,
        -config.maximumPitchRadians, config.maximumPitchRadians);

    const float forwardInput = std::clamp(input.forward, -1.0F, 1.0F);
    const float rightInput = std::clamp(input.right, -1.0F, 1.0F);
    const float inputLength = std::hypot(forwardInput, rightInput);
    const float normalization = inputLength > 1.0F ? 1.0F / inputLength : 1.0F;
    const float forwardX = std::sin(candidate.yawRadians);
    const float forwardZ = std::cos(candidate.yawRadians);
    const float rightX = std::cos(candidate.yawRadians);
    const float rightZ = -std::sin(candidate.yawRadians);
    const float speed = input.sprint ? config.sprintSpeed : config.walkSpeed;
    const float moveX = (forwardX * forwardInput + rightX * rightInput) * normalization * speed * step;
    const float moveZ = (forwardZ * forwardInput + rightZ * rightInput) * normalization * speed * step;

    const auto tryHorizontalMove = [&](const std::size_t axis, const float amount) {
        if (amount == 0.0F)
            return true;
        auto proposed = candidate.position;
        proposed[axis] += amount;
        if (!IsInsideHorizontalBounds(region, config, proposed))
            return true;
        bool collision = false;
        if (!IntersectsPlayer(region, config, proposed, collision, error))
            return false;
        if (!collision)
            candidate.position = proposed;
        return true;
    };

    // Resolve axes independently so a blocked axis does not cancel wall sliding.
    if (!tryHorizontalMove(0U, moveX) || !tryHorizontalMove(2U, moveZ))
        return false;

    if (candidate.grounded && input.jumpPressed && config.jumpSpeed > 0.0F)
    {
        candidate.verticalVelocity = config.jumpSpeed;
        candidate.grounded = false;
    }
    if (!candidate.grounded)
    {
        candidate.verticalVelocity -= config.gravity * step;
        const float proposedY = candidate.position[1] + candidate.verticalVelocity * step;
        if (proposedY <= candidate.position[1])
        {
            float terrainHeight = 0.0F;
            std::array<float, 3> normal{};
            if (!world::SampleTerrainHeight(region, candidate.position[0],
                    candidate.position[2], terrainHeight, normal, error))
                return false;
            if (proposedY <= terrainHeight)
            {
                candidate.position[1] = terrainHeight;
                candidate.verticalVelocity = 0.0F;
                candidate.grounded = true;
            }
            else
            {
                candidate.position[1] = proposedY;
            }
        }
        else
        {
            auto proposed = candidate.position;
            proposed[1] = proposedY;
            bool collision = false;
            if (!IntersectsPlayer(region, config, proposed, collision, error))
                return false;
            if (collision)
            {
                candidate.verticalVelocity = 0.0F;
            }
            else
            {
                candidate.position[1] = proposedY;
            }
        }
    }
    else
    {
        float terrainHeight = 0.0F;
        std::array<float, 3> normal{};
        if (!world::SampleTerrainHeight(region, candidate.position[0],
                candidate.position[2], terrainHeight, normal, error))
            return false;
        candidate.position[1] = terrainHeight;
    }

    if (!IsFinite(candidate))
    {
        error = "Player simulation produced a non-finite state.";
        return false;
    }
    state = candidate;
    error.clear();
    return true;
}

PlayerCamera BuildPlayerCamera(const PlayerState& state,
    const PlayerConfig& config) noexcept
{
    const float cosPitch = std::cos(state.pitchRadians);
    const float sinPitch = std::sin(state.pitchRadians);
    const float sinYaw = std::sin(state.yawRadians);
    const float cosYaw = std::cos(state.yawRadians);
    return {
        {state.position[0], state.position[1] + config.eyeHeight, state.position[2]},
        {sinYaw * cosPitch, sinPitch, cosYaw * cosPitch}
    };
}
} // namespace talvisota::game
