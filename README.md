# Talvisota

**Talvisota** is an in-development, historically grounded first-person game set during the Winter War (1939–1940). It is a separate game project built around VantaEngine.

The repository is being established with explicit boundaries between engine technology, game rules, historical content, and presentation. The goal is a maintainable codebase that can grow into a large project without turning the renderer, mission logic, or content data into one tightly coupled codebase.

> **Project status:** architecture and core lifecycle foundation. This repository does not yet contain a playable game or a game-owned rendering integration. VantaEngine's current public renderer still creates its own sample world; Talvisota will connect to it through a proper game-scene submission API rather than depending on renderer internals.

## Development plan

The authoritative milestone sequence, exit criteria, architecture rules, and immediate execution order are documented in [docs/production/DEVELOPMENT_PLAN.md](docs/production/DEVELOPMENT_PLAN.md). Work should follow those gates; do not skip ahead based on visual ambition alone.

## Project principles

- **Game owns the game.** Missions, world definitions, factions, characters, equipment, interaction rules, progression, and game-specific UI belong here.
- **Engine owns generic technology.** Rendering backends, platform abstraction, generic asset loading, timing, and reusable low-level systems belong in VantaEngine.
- **Data-driven content.** World and mission definitions should be authored as versioned data, validated before runtime, and kept separate from gameplay code.
- **Explicit ownership.** Systems own their resources and define initialization, failure, shutdown, and reload behavior.
- **Deterministic core logic.** Gameplay simulation should be testable without creating a graphics device or opening a window.
- **No hidden global state.** Dependencies are passed explicitly; service locators and mutable singleton managers are not the default architecture.
- **Measured performance.** Profile representative workloads before adding complexity or claiming performance improvements.
- **Research before representation.** Historical claims and references should be traceable; uncertainty should be documented rather than silently converted into certainty.

## Repository layout

```text
Talvisota/
├── .github/
│   └── workflows/
│       └── ci.yml
├── cmake/
│   └── TalvisotaCompilerOptions.cmake
├── docs/
│   ├── architecture/
│   ├── development/
│   ├── history/
│   └── production/
├── assets/
│   ├── audio/
│   ├── materials/
│   ├── models/
│   ├── textures/
│   └── ui/
├── content/
│   ├── factions/
│   ├── missions/
│   ├── props/
│   ├── characters/
│   └── worlds/
├── data/
│   ├── schemas/
│   └── localization/
├── src/
│   └── talvisota/
│       ├── app/
│       ├── core/
│       ├── gameplay/
│       ├── world/
│       ├── entities/
│       ├── combat/
│       ├── equipment/
│       ├── missions/
│       ├── presentation/
│       ├── audio/
│       ├── content/
│       └── platform/
├── tests/
│   ├── core/
│   ├── gameplay/
│   ├── content/
│   └── integration/
├── tools/
│   ├── asset_pipeline/
│   └── validation/
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
└── README.md
```

Directories that do not yet contain tracked files are documented here and will be added as real systems are implemented. No empty placeholder modules are compiled into the project.

## Build requirements

Current core foundation:
- CMake 3.25+
- C++23 compiler (MSVC is the primary target)
- Ninja on Windows, when using the supplied presets

Configure, build, and test:

```powershell
cmake --preset windows-ninja
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Release:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

The initial CI matrix builds and tests Debug and Release on Windows x64 with warnings treated as errors.

## Engine boundary

VantaEngine is maintained in a separate repository. Talvisota must consume public engine interfaces only. It must not include private D3D11/D3D12 implementation headers, reach into backend resource ownership, or copy engine implementation files into this repository.

The current engine runtime is not yet sufficient to render a game-owned scene: its renderers still build their procedural sample scene internally. The engine integration milestone is therefore a public scene submission and resource-lifetime API for both renderer backends. Until that exists, this repository keeps its game-domain foundation independent of renderer implementation details.

## Licensing

This repository uses the custom **Talvisota Source-Available Reference Licence** in `LICENSE`. Source inspection and independent inspiration are allowed under its terms. Copying, redistribution, modification, and reuse of project code or original assets are not granted.

This is **not an open-source licence**. Third-party components and assets remain governed by their own licences. Read [LICENSE](LICENSE) before using any material from this repository.
