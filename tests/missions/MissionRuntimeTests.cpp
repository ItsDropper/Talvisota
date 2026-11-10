#include "talvisota/content/AuthoredDefinition.hpp"
#include "talvisota/content/ContentRegistry.hpp"
#include "talvisota/missions/MissionContentAdapter.hpp"
#include "talvisota/missions/MissionRuntime.hpp"
#include "talvisota/game/MissionRuntime.hpp"
#include "talvisota/game/MissionContentAdapter.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <stdexcept>

static_assert(std::is_same_v<talvisota::game::MissionState,
    talvisota::missions::MissionState>);
static_assert(std::is_same_v<decltype(&talvisota::game::BuildMissionDefinition),
    decltype(&talvisota::missions::BuildMissionDefinition)>);

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

talvisota::missions::MissionDefinition MakeMission()
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

class TemporaryContent final
{
public:
    TemporaryContent()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("talvisota-mission-adapter-" + std::to_string(stamp));
        std::filesystem::create_directories(root_);
    }

    ~TemporaryContent()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    void Write(const std::filesystem::path& relativePath, const std::string& contents)
    {
        const auto path = root_ / relativePath;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output << contents;
        if (!output)
            throw std::runtime_error("Could not write temporary mission content fixture.");
    }

    [[nodiscard]] const std::filesystem::path& Root() const noexcept { return root_; }
    [[nodiscard]] std::filesystem::path ManifestPath() const { return root_ / "catalog.tmanifest"; }

private:
    std::filesystem::path root_;
};

void TestManifestParserToMissionAdapter()
{
    std::string error;
    using namespace talvisota::content;
    using namespace talvisota::missions;

    TemporaryContent content;
    content.Write("content/worlds/sakkola.tworld", "talvisota-world\t1\nid = world.sakkola\n");
    content.Write("content/worlds/sakkola-church-village.tregion", "fixture region\n");
    content.Write("content/characters/sakkola-infantryman.tcharacter", "fixture character\n");
    content.Write("content/factions/finnish.tfaction", "fixture faction\n");
    content.Write("content/missions/first-patrol.tmission",
        "talvisota-mission\t1\n"
        "id = mission.sakkola.first-patrol\n"
        "name = First Patrol\n"
        "world_id = world.sakkola\n"
        "region_id = region.sakkola.church_village\n"
        "player_character_id = character.sakkola_infantryman\n"
        "faction_id = faction.finnish\n"
        "next_mission_id = mission.sakkola.northern-watch\n"
        "objective = objective.inspect-well -> prop.well.farmstead\n"
        "objective = objective.inspect-woodpile -> prop.woodpile.west\n"
        "objective = objective.report-church -> church.sakkola\n");
    content.Write("content/missions/northern-watch.tmission",
        "talvisota-mission\t1\n"
        "id = mission.sakkola.northern-watch\n"
        "name = Northern Watch\n"
        "world_id = world.sakkola\n"
        "region_id = region.sakkola.church_village\n"
        "player_character_id = character.sakkola_infantryman\n"
        "faction_id = faction.finnish\n"
        "environment_profile = overcast\n"
        "objective = objective.watch -> prop.watch-post\n");
    content.Write("catalog.tmanifest",
        "talvisota-content-manifest\t1\n"
        "asset\tmission.sakkola.first-patrol\tdefinition\tcontent/missions/first-patrol.tmission\trequired\tworld.sakkola,region.sakkola.church_village,character.sakkola_infantryman,faction.finnish,mission.sakkola.northern-watch\tproject\tproject\n"
        "asset\tmission.sakkola.northern-watch\tdefinition\tcontent/missions/northern-watch.tmission\trequired\tworld.sakkola,region.sakkola.church_village,character.sakkola_infantryman,faction.finnish\tproject\tproject\n"
        "asset\tworld.sakkola\tdefinition\tcontent/worlds/sakkola.tworld\trequired\t-\tproject\tproject\n"
        "asset\tregion.sakkola.church_village\tdefinition\tcontent/worlds/sakkola-church-village.tregion\trequired\tworld.sakkola\tproject\tproject\n"
        "asset\tcharacter.sakkola_infantryman\tdefinition\tcontent/characters/sakkola-infantryman.tcharacter\trequired\tfaction.finnish\tproject\tproject\n"
        "asset\tfaction.finnish\tdefinition\tcontent/factions/finnish.tfaction\trequired\t-\tproject\tproject\n");

    ContentRegistry registry;
    std::vector<ContentDiagnostic> manifestDiagnostics;
    Check(LoadContentManifest(content.ManifestPath(), content.Root(), registry, manifestDiagnostics),
        "Sakkola mission manifest and dependency graph load");

    AuthoredDefinition authored;
    std::vector<AuthoredDefinitionDiagnostic> definitionDiagnostics;
    const auto missionPath = content.Root() / "content/missions/first-patrol.tmission";
    Check(LoadAuthoredDefinition(missionPath, registry, authored, definitionDiagnostics),
        "First Patrol parses through the real authored-definition loader");
    AuthoredDefinition successor;
    Check(LoadAuthoredDefinition(content.Root() / "content/missions/northern-watch.tmission",
        registry, successor, definitionDiagnostics) &&
        successor.nextMissionId.empty() && successor.environmentProfile == "overcast",
        "successor mission loads with its authored weather profile and terminal campaign state");

    const AuthoredDefinition preservedSuccessor=successor;
    content.Write("content/missions/northern-watch.tmission",
        "talvisota-mission\\t1\\n"
        "id = mission.sakkola.northern-watch\\n"
        "name = Northern Watch\\n"
        "world_id = world.sakkola\\n"
        "region_id = region.sakkola.church_village\\n"
        "player_character_id = character.sakkola_infantryman\\n"
        "faction_id = faction.finnish\\n"
        "environment_profile = volcanic\\n"
        "objective = objective.watch -> prop.watch-post\\n");
    Check(!LoadAuthoredDefinition(content.Root() / "content/missions/northern-watch.tmission",
        registry, successor, definitionDiagnostics),
        "unsupported mission weather profile is rejected");
    Check(successor.environmentProfile == preservedSuccessor.environmentProfile &&
        successor.id == preservedSuccessor.id,
        "invalid weather profile cannot partially replace a loaded mission");

    Check(authored.id == "mission.sakkola.first-patrol" &&
        authored.worldId == "world.sakkola" &&
        authored.regionId == "region.sakkola.church_village" &&
        authored.characterId == "character.sakkola_infantryman" &&
        authored.factionId == "faction.finnish" &&
        authored.nextMissionId == "mission.sakkola.northern-watch",
        "parsed First Patrol retains its authored world, region, character, and faction references");
    Check(authored.objectives.size() == 3U &&
        authored.objectives[0].id == "objective.inspect-well" &&
        authored.objectives[0].interactionTargetId == "prop.well.farmstead" &&
        authored.objectives[1].id == "objective.inspect-woodpile" &&
        authored.objectives[1].interactionTargetId == "prop.woodpile.west" &&
        authored.objectives[2].id == "objective.report-church" &&
        authored.objectives[2].interactionTargetId == "church.sakkola",
        "parsed authored objective sequence retains all three stable placement IDs");

    MissionDefinition gameplayDefinition;
    Check(BuildMissionDefinition(authored, gameplayDefinition, error),
        "parsed Sakkola mission converts through the gameplay adapter");
    Check(gameplayDefinition.id == "mission.sakkola.first-patrol" &&
        gameplayDefinition.objectives.size() == 3U &&
        gameplayDefinition.objectives[0].interactionTargetId == "prop.well.farmstead" &&
        gameplayDefinition.objectives[1].interactionTargetId == "prop.woodpile.west" &&
        gameplayDefinition.objectives[2].interactionTargetId == "church.sakkola",
        "manifest-to-runtime conversion preserves the exact ordered interaction contract");

    MissionState activeMission;
    Check(InitializeMissionFromAuthoredDefinition(authored, activeMission, error),
        "parsed manifest-backed First Patrol initializes as an active gameplay mission");
    Check(activeMission.missionId == "mission.sakkola.first-patrol" &&
        activeMission.status == MissionStatus::Active &&
        activeMission.objectives.size() == 3U &&
        activeMission.objectives.front().status == ObjectiveStatus::Active,
        "manifest-backed mission starts with its first authored objective active");

    const AuthoredDefinition preserved = authored;
    content.Write("content/missions/first-patrol.tmission",
        "talvisota-mission\t1\n"
        "id = mission.sakkola.first-patrol\n"
        "name = Invalid First Patrol\n"
        "world_id = world.sakkola\n"
        "region_id = region.sakkola.church_village\n"
        "player_character_id = character.sakkola_infantryman\n"
        "faction_id = faction.finnish\n"
        "objective = objective.duplicate -> prop.well.farmstead\n"
        "objective = objective.duplicate -> prop.woodpile.west\n");
    Check(!LoadAuthoredDefinition(missionPath, registry, authored, definitionDiagnostics),
        "duplicate authored objective IDs are rejected before adapter conversion");
    Check(authored.id == preserved.id && authored.objectives.size() == preserved.objectives.size(),
        "failed authored-definition reload preserves the previously published parsed mission");
}
}

int main()
{
    TestManifestParserToMissionAdapter();
    using namespace talvisota::missions;
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
