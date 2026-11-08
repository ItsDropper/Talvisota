#include "talvisota/content/AuthoredDefinition.hpp"
#include "talvisota/game/MissionContentAdapter.hpp"
#include "talvisota/game/MissionRuntime.hpp"

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

talvisota::game::MissionDefinition MakeMission()
{
    return {
        "mission.first-patrol",
        {
            {"objective.reach-outpost", "target.outpost-radio"},
            {"objective.report-contact", "target.field-telephone"},
            {"objective.return-to-command", "target.command-post"}
        }
    };
}
}

int main()
{
    using namespace talvisota::game;
    std::string error;
    MissionState mission;

    talvisota::content::AuthoredDefinition authored;
    authored.kind = talvisota::content::AuthoredDefinitionKind::Mission;
    authored.id = "mission.sakkola.first-patrol";
    authored.worldId = "world.sakkola";
    authored.regionId = "region.sakkola.church_village";
    authored.objectives = {
        {"objective.inspect-well", "prop.well.farmstead"},
        {"objective.inspect-woodpile", "prop.woodpile.west"},
        {"objective.report-church", "church.sakkola"}
    };
    MissionDefinition mapped;
    Check(BuildMissionDefinition(authored, mapped, error),
        "authored Sakkola mission converts to gameplay definition");
    Check(mapped.id == authored.id && mapped.objectives.size() == 3U &&
        mapped.objectives[0].interactionTargetId == "prop.well.farmstead" &&
        mapped.objectives[1].interactionTargetId == "prop.woodpile.west" &&
        mapped.objectives[2].interactionTargetId == "church.sakkola",
        "mission adapter preserves objective order and placement target IDs");

    MissionState authoredState;
    Check(InitializeMissionFromAuthoredDefinition(authored, authoredState, error),
        "authored Sakkola mission initializes and starts");
    Check(authoredState.missionId == authored.id &&
        authoredState.status == MissionStatus::Active &&
        authoredState.objectives.size() == 3U &&
        authoredState.objectives[0].status == ObjectiveStatus::Active,
        "authored mission publishes an active first objective");

    MissionDefinition preservedDefinition{"mission.preserved", {{"objective.keep", "target.keep"}}};
    auto nonMission = authored;
    nonMission.kind = talvisota::content::AuthoredDefinitionKind::Faction;
    Check(!BuildMissionDefinition(nonMission, preservedDefinition, error),
        "non-mission authored definitions cannot be converted");
    Check(preservedDefinition.id == "mission.preserved",
        "failed content conversion preserves definition output");
    auto incompleteMission = authored;
    incompleteMission.regionId.clear();
    MissionState preservedState;
    preservedState.missionId = "mission.preserved";
    preservedState.status = MissionStatus::Failed;
    Check(!InitializeMissionFromAuthoredDefinition(incompleteMission, preservedState, error),
        "authored mission without a region cannot initialize");
    Check(preservedState.missionId == "mission.preserved" &&
        preservedState.status == MissionStatus::Failed,
        "failed authored initialization preserves existing mission state");
    auto duplicateTargetMission = authored;
    duplicateTargetMission.objectives[1].interactionTargetId =
        duplicateTargetMission.objectives[0].interactionTargetId;
    Check(!BuildMissionDefinition(duplicateTargetMission, preservedDefinition, error),
        "duplicate authored interaction targets cannot enter gameplay runtime");

    const MissionDefinition definition = MakeMission();

    Check(ValidateMissionDefinition(definition, error), "valid mission definition is accepted");
    Check(InitializeMission(definition, mission, error), "mission initializes without starting");
    Check(mission.status == MissionStatus::Ready && mission.completedObjectives == 0U,
        "initialized mission is Ready with no completed objectives");
    Check(StartMission(mission, error), "Ready mission starts");
    Check(mission.status == MissionStatus::Active &&
        mission.objectives[0].status == ObjectiveStatus::Active,
        "starting activates the first objective");
    Check(!StartMission(mission, error), "active mission cannot be started twice");

    InteractionResult result = InteractionResult::MissionCompleted;
    Check(ApplyMissionInteraction(mission, "target.unknown", result, error),
        "unknown interaction target is a harmless no-op");
    Check(result == InteractionResult::Ignored && mission.completedObjectives == 0U,
        "unknown target does not advance mission");
    Check(ApplyMissionInteraction(mission, "target.field-telephone", result, error),
        "out-of-order target is a harmless no-op");
    Check(result == InteractionResult::Ignored && mission.completedObjectives == 0U,
        "out-of-order target cannot skip the active objective");

    Check(ApplyMissionInteraction(mission, "target.outpost-radio", result, error),
        "first objective interaction succeeds");
    Check(result == InteractionResult::ObjectiveCompleted &&
        mission.objectives[0].status == ObjectiveStatus::Completed &&
        mission.objectives[1].status == ObjectiveStatus::Active,
        "completing an objective activates exactly the next objective");
    Check(ApplyMissionInteraction(mission, "target.outpost-radio", result, error),
        "repeated completed interaction is harmless");
    Check(result == InteractionResult::Ignored && mission.completedObjectives == 1U,
        "repeated interaction cannot double-count objective completion");

    Check(ApplyMissionInteraction(mission, "target.field-telephone", result, error),
        "second objective interaction succeeds");
    Check(result == InteractionResult::ObjectiveCompleted &&
        mission.objectives[2].status == ObjectiveStatus::Active,
        "second objective advances ordered progression");
    Check(ApplyMissionInteraction(mission, "target.command-post", result, error),
        "final objective interaction succeeds");
    Check(result == InteractionResult::MissionCompleted &&
        mission.status == MissionStatus::Completed &&
        mission.completedObjectives == 3U,
        "final objective completes the mission");

    Check(!FailMission(mission, error), "completed mission cannot be failed");
    Check(RestartMission(mission, error), "completed mission can be restarted");
    Check(mission.status == MissionStatus::Active && mission.completedObjectives == 0U &&
        mission.objectives[0].status == ObjectiveStatus::Active,
        "restart resets progression and activates the first objective");

    Check(FailMission(mission, error), "active mission can fail");
    Check(mission.status == MissionStatus::Failed, "failure enters terminal Failed state");
    Check(!ApplyMissionInteraction(mission, "target.outpost-radio", result, error) ||
        result == InteractionResult::Ignored,
        "failed mission cannot progress through interactions");
    Check(RestartMission(mission, error), "failed mission can restart");

    MissionDefinition empty;
    Check(!ValidateMissionDefinition(empty, error), "empty mission definition is rejected");
    MissionDefinition duplicateObjective = MakeMission();
    duplicateObjective.objectives[1].id = duplicateObjective.objectives[0].id;
    Check(!ValidateMissionDefinition(duplicateObjective, error),
        "duplicate objective IDs are rejected");
    MissionDefinition duplicateTarget = MakeMission();
    duplicateTarget.objectives[1].interactionTargetId = duplicateTarget.objectives[0].interactionTargetId;
    Check(!ValidateMissionDefinition(duplicateTarget, error),
        "duplicate interaction target IDs are rejected");
    MissionDefinition emptyTarget = MakeMission();
    emptyTarget.objectives[0].interactionTargetId.clear();
    Check(!ValidateMissionDefinition(emptyTarget, error),
        "objective without an interaction target is rejected");

    MissionState unchanged = mission;
    MissionState invalidState = mission;
    invalidState.completedObjectives = 2U;
    Check(!ApplyMissionInteraction(invalidState, "target.outpost-radio", result, error),
        "internally inconsistent mission state is rejected");
    Check(invalidState.completedObjectives == 2U && invalidState.status == MissionStatus::Active,
        "invalid mission state is not partially mutated");
    Check(unchanged.completedObjectives == mission.completedObjectives,
        "valid mission state remains unchanged by invalid-state rejection");

    if (failures != 0)
    {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Talvisota mission-runtime tests passed\n";
    return EXIT_SUCCESS;
}
