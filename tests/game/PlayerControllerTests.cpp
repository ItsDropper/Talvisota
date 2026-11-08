#include "talvisota/game/PlayerController.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
int failures = 0;
void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

talvisota::world::WorldRegion MakeRegion()
{
    talvisota::world::WorldRegion region;
    region.id = "test-region";
    region.boundsMin = {-10.0F, -5.0F, -10.0F};
    region.boundsMax = {10.0F, 20.0F, 10.0F};
    region.terrain.rows = 3U;
    region.terrain.columns = 3U;
    region.terrain.originX = -10.0F;
    region.terrain.originZ = -10.0F;
    region.terrain.spacingX = 10.0F;
    region.terrain.spacingZ = 10.0F;
    region.terrain.elevations.assign(9U, 0.0F);
    return region;
}
}

int main()
{
    using namespace talvisota::game;
    auto region = MakeRegion();
    PlayerConfig config;
    PlayerState player;
    std::string error;

    Check(InitializePlayer(region, config, {0.0F, 0.0F, 0.0F}, player, error),
        "player initializes on sampled terrain");
    Check(player.grounded && player.position[1] == 0.0F,
        "spawn is grounded at terrain elevation");

    PlayerInput forward;
    forward.forward = 1.0F;
    Check(UpdatePlayer(region, config, forward, 0.5F, player, error),
        "forward movement update succeeds");
    Check(std::abs(player.horizontalVelocity[1] - config.groundAcceleration * 0.1F) < 0.0001F,
        "large frame delta is clamped and ground acceleration is applied");
    Check(player.position[2] > 0.0F, "accelerated movement advances the player");

    const float beforeDiagonal = player.position[2];
    PlayerInput diagonal;
    diagonal.forward = 1.0F;
    diagonal.right = 1.0F;
    Check(UpdatePlayer(region, config, diagonal, 0.1F, player, error),
        "diagonal movement update succeeds");
    const float diagonalDistance = std::hypot(player.position[0],
        player.position[2] - beforeDiagonal);
    Check(diagonalDistance > 0.0F && std::isfinite(diagonalDistance),
        "diagonal acceleration produces finite normalized movement");

    const auto beforeSprint = player.position;
    PlayerInput sprint;
    sprint.forward = 1.0F;
    sprint.sprint = true;
    Check(UpdatePlayer(region, config, sprint, 0.1F, player, error),
        "sprint movement update succeeds");
    const float sprintDistance = std::hypot(player.position[0] - beforeSprint[0],
        player.position[2] - beforeSprint[2]);
    Check(sprintDistance > 0.0F && player.horizontalVelocity[1] > config.walkSpeed,
        "sprint accelerates toward the higher sprint speed");

    PlayerInput release;
    Check(UpdatePlayer(region, config, release, 0.1F, player, error),
        "release input applies ground friction");
    Check(std::abs(player.horizontalVelocity[0]) < config.groundAcceleration * 0.1F + 0.001F,
        "ground friction reduces lateral momentum");

    PlayerInput crouch;
    crouch.crouch = true;
    Check(UpdatePlayer(region, config, crouch, 0.016F, player, error),
        "crouch update succeeds");
    Check(player.crouched && BuildPlayerCamera(player, config).position[1] <
        player.position[1] + config.eyeHeight, "crouch lowers the camera eye height");
    crouch.crouch = false;
    Check(UpdatePlayer(region, config, crouch, 0.016F, player, error),
        "standing update succeeds");
    Check(!player.crouched, "player returns to standing when overhead clearance is available");

    PlayerInput jump;
    jump.jumpPressed = true;
    Check(UpdatePlayer(region, config, jump, 0.016F, player, error),
        "jump update succeeds");
    Check(!player.grounded && player.position[1] > 0.0F,
        "jump leaves ground and applies vertical movement");

    PlayerInput look;
    look.lookPitchRadians = 10.0F;
    Check(UpdatePlayer(region, config, look, 0.016F, player, error),
        "look update succeeds");
    Check(std::abs(player.pitchRadians - config.maximumPitchRadians) < 0.0001F,
        "camera pitch is clamped before singularity");
    for (int frame = 0; frame < 120 && !player.grounded; ++frame)
        Check(UpdatePlayer(region, config, {}, 0.016F, player, error),
            "airborne simulation step succeeds");
    Check(player.grounded && std::abs(player.position[1]) < 0.0001F &&
        player.verticalVelocity == 0.0F,
        "jump arc lands on sampled terrain and clears vertical velocity");

    const auto camera = BuildPlayerCamera(player, config);
    Check(std::abs(camera.position[1] - (player.position[1] + config.eyeHeight)) < 0.0001F,
        "camera eye height is derived from player feet position");

    region.collision.push_back({"wall", {0.0F, 1.0F, 1.0F}, {1.0F, 2.0F, 0.5F}});
    PlayerState blocked;
    Check(InitializePlayer(region, config, {0.0F, 0.0F, 0.0F}, blocked, error),
        "player can spawn clear of collision volume");
    PlayerInput intoWall;
    intoWall.forward = 1.0F;
    Check(UpdatePlayer(region, config, intoWall, 0.1F, blocked, error),
        "collision-constrained movement succeeds");
    Check(blocked.position[2] < 0.1F,
        "authored collision volume blocks player movement");

    const PlayerState unchanged = blocked;
    PlayerInput invalid;
    invalid.forward = std::nanf("");
    Check(!UpdatePlayer(region, config, invalid, 0.016F, blocked, error),
        "non-finite input is rejected");
    Check(blocked.position == unchanged.position &&
        blocked.verticalVelocity == unchanged.verticalVelocity,
        "failed update leaves player state unchanged");

    PlayerConfig invalidConfig = config;
    invalidConfig.sprintSpeed = invalidConfig.walkSpeed - 1.0F;
    Check(!ValidatePlayerConfig(invalidConfig, error),
        "invalid movement configuration is rejected");

    if (failures != 0)
    {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Talvisota player-controller tests passed\n";
    return EXIT_SUCCESS;
}
