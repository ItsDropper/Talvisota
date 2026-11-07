# Talvisota

**Talvisota** is an in-development, historically grounded first-person game set during the Winter War (1939–1940). It is a separate game project built around VantaEngine.

The repository is being established with explicit boundaries between engine technology, game rules, historical content, and presentation. The goal is a maintainable codebase that can grow into a large project without turning the renderer, mission logic, or content data into one tightly coupled codebase.

> **Project status:** M0 engineering foundation is complete. M1 provides a native application shell, configuration, logging, user paths, and terminate diagnostics. The project is not yet playable and does not yet submit a game-owned scene to VantaEngine. The interactive runtime is intentionally deferred until the M2 engine integration contract exists.

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

Run the application shell after building:

```powershell
.\build\windows-ninja\talvisota.exe --help
.\build\windows-ninja\talvisota.exe
```

The application creates a versioned `config.ini`, a timestamped log, and a terminate-report location under the current user's application-data directories. A custom configuration file can be selected with `--config <file>`. Unknown options and invalid configuration are reported as errors.

The initial CI matrix builds and tests Debug and Release on Windows x64 with warnings treated as errors.

## Application lifecycle boundary

The application composition root owns startup ordering and shutdown. Core configuration and logging do not depend on a window or graphics device. Partial initialization failures are reported and unwind through the same shutdown path.

The current crash diagnostic is a C++ terminate report. It is not an OS minidump, and it cannot recover from power loss or arbitrary forced process termination.

The executable currently initializes core services and exits. It does not pretend to run a game loop. Interactive runtime work starts after the public game-owned scene API is ready in M2.

## Engine boundary

VantaEngine is maintained in a separate repository. Talvisota must consume public engine interfaces only. It must not include private D3D11/D3D12 implementation headers, reach into backend resource ownership, or copy engine implementation files into this repository.

The current engine runtime is not yet sufficient to render a game-owned scene: its renderers still build their procedural sample scene internally. The engine integration milestone is therefore a public scene submission and resource-lifetime API for both renderer backends. Until that exists, this repository keeps its game-domain foundation independent of renderer implementation details.

## Licensing

This repository uses the custom **Talvisota Source-Available Reference Licence** in `LICENSE`. Source inspection and independent inspiration are allowed under its terms. Copying, redistribution, modification, and reuse of project code or original assets are not granted.

This is **not an open-source licence**. Third-party components and assets remain governed by their own licences. Read [LICENSE](LICENSE) before using any material from this repository.
