#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace talvisota::game
{
enum class MissionStatus
{
    Ready,
    Active,
    Completed,
    Failed
};

enum class ObjectiveStatus
{
    Pending,
    Active,
    Completed
};

enum class InteractionResult
{
    Ignored,
    ObjectiveCompleted,
    MissionCompleted
};

struct ObjectiveDefinition final
{
    std::string id;
    std::string interactionTargetId;
};

struct MissionDefinition final
{
    std::string id;
    std::vector<ObjectiveDefinition> objectives;
};

struct ObjectiveState final
{
    std::string id;
    std::string interactionTargetId;
    ObjectiveStatus status = ObjectiveStatus::Pending;
};

struct MissionState final
{
    std::string missionId;
    MissionStatus status = MissionStatus::Ready;
    std::vector<ObjectiveState> objectives;
    std::size_t completedObjectives = 0U;
};

// Mission definitions are immutable input. Objective order is the authored progression
// order; each objective is completed by interacting with its unique target ID.
[[nodiscard]] bool ValidateMissionDefinition(const MissionDefinition& definition,
    std::string& error);

// Initializes a Ready state. The first objective becomes Active; the caller may then
// publish the state to the game loop. Output is unchanged when validation fails.
[[nodiscard]] bool InitializeMission(const MissionDefinition& definition,
    MissionState& output, std::string& error);

// Starts a Ready mission. Re-starting an active or terminal mission is rejected.
[[nodiscard]] bool StartMission(MissionState& state, std::string& error);

// Applies a stable interaction-target ID. Unknown, out-of-order, or already completed
// targets are harmless no-ops. The next objective activates only after the current one
// completes. Completing the final objective completes the mission atomically.
[[nodiscard]] bool ApplyMissionInteraction(MissionState& state,
    std::string_view interactionTargetId, InteractionResult& result, std::string& error);

// Fails an active mission. Terminal and Ready states cannot be failed.
[[nodiscard]] bool FailMission(MissionState& state, std::string& error);

// Resets a mission to its initial active objective, including after completion or failure.
[[nodiscard]] bool RestartMission(MissionState& state, std::string& error);
} // namespace talvisota::game
