# Architecture overview

## Dependency direction

The intended dependency direction is inward-to-outward:

1. **Core** owns lifecycle, time, identifiers, deterministic utilities, and application-level contracts.
2. **Domain** owns game rules: actors, equipment, combat, missions, factions, and progression.
3. **World** owns spatial composition, world state, traversal, and streaming policy.
4. **Content** owns schemas, manifests, validation, and loading of authored data.
5. **Presentation** translates domain state into camera, animation, UI, audio, and render-facing data.
6. **Platform** owns OS-facing services and process integration.
7. **Engine adapters** translate game-owned render/audio/input requests to VantaEngine public APIs.

Core and domain logic must not depend on Direct3D, Win32 window handles, renderer classes, or engine-private headers.

## Runtime composition

The application composition root creates services in dependency order, injects them into systems, and tears them down in reverse order. A subsystem must not secretly create another global subsystem.

The top-level session phase is represented by `core::GameSession`. It governs lifecycle transitions only; it is not a gameplay state machine and does not own world or renderer resources.

## Game-owned modules

- `gameplay/`: simulation orchestration and rules shared across missions.
- `entities/`: actor identity, components, teams, and game-specific entity behavior.
- `combat/`: weapon-independent combat rules, damage resolution, hit evaluation, and status effects.
- `equipment/`: game-specific equipment definitions, inventory rules, and loadout validation.
- `missions/`: objectives, mission state, triggers, scripted events, and completion conditions.
- `world/`: world partitioning, traversal metadata, environment state, and streaming policy.
- `content/`: manifests, schema validation, versioning, and asset references.
- `presentation/`: camera behavior, animation state, HUD, menus, and presentation mapping.
- `audio/`: game event-to-audio mapping, mixing policy, and spatial-audio configuration.
- `platform/`: application entry, crash handling, file locations, and platform adapters.

Do not create a subsystem merely because a directory exists in the proposed hierarchy. Add it when it has a concrete responsibility and tests.

## Engine ownership

VantaEngine owns generic rendering and platform technology. Talvisota owns the data and decisions that describe what the game should render. The adapter boundary should accept game-owned render packets or scene snapshots and translate them to the engine's public API.

Never make mission code call a D3D command list, allocate GPU descriptors, or depend on a renderer's private mesh layout.
