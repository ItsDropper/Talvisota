#pragma once

// Compatibility include for code that used the pre-domain-split path.
#include "talvisota/missions/MissionRuntime.hpp"

namespace talvisota::game
{
using missions::MissionStatus;
using missions::ObjectiveStatus;
using missions::InteractionResult;
using missions::ObjectiveDefinition;
using missions::MissionDefinition;
using missions::ObjectiveState;
using missions::MissionState;
using missions::ValidateMissionDefinition;
using missions::InitializeMission;
using missions::StartMission;
using missions::ApplyMissionInteraction;
using missions::FailMission;
using missions::RestartMission;
} // namespace talvisota::game
