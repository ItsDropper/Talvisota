#pragma once

// Compatibility include for code that used the pre-domain-split path.
#include "talvisota/missions/MissionContentAdapter.hpp"

namespace talvisota::game
{
using missions::BuildMissionDefinition;
using missions::InitializeMissionFromAuthoredDefinition;
} // namespace talvisota::game
