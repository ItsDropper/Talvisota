# Game source modules

Dependencies point toward core contracts. Domain rules must not depend on graphics APIs or OS handles.

- app/: process entry and composition root.
- core/: lifecycle and foundational runtime contracts.
- gameplay/: simulation orchestration and shared rules.
- world/: game-specific world state and streaming policy.
- entities/: actors and game-specific components.
- combat/: damage and combat rules.
- equipment/: inventory and loadout rules.
- missions/: objectives and mission state.
- presentation/: camera, animation, HUD, and render-facing translation.
- audio/: game event and spatial-audio policy.
- content/: manifest validation and authored-data loading.
- platform/: OS-facing services.

Entities, combat, and missions are separate build targets. Gameplay composes those domain modules; it does not own their implementations. Compatibility headers remain in the former include locations while downstream consumers migrate.

Only add code when a module has a concrete contract and tests.