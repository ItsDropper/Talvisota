# Talvisota

**Talvisota** is an in-development, historically grounded first-person game set during the Winter War (1939–1940). It is a separate game project built around VantaEngine.

The repository is being established with explicit boundaries between engine technology, game rules, historical content, and presentation. The goal is a maintainable codebase that can grow into a large project without turning the renderer, mission logic, or content data into one tightly coupled codebase.

> **Project status:** M0 and M1 are complete. M2's implementation is in place and its Windows Debug, Release, and MSVC adapter CI checks pass; M2 remains open pending real-device D3D11/D3D12 verification. M3 is complete. M4 has started with a game-owned, validated world-region format and transactional region load/reload/unload; renderer-backed world presentation remains gated on M2 hardware verification.

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
│   ├── app/
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

The initial CI matrix builds and tests Debug and Release on Windows x64 with warnings treated as errors. A separate MSVC job builds the pinned VantaEngine adapter and the game-owned scene sample.

## Application lifecycle boundary

The application composition root owns startup ordering and shutdown. Core configuration and logging do not depend on a window or graphics device. Partial initialization failures are reported and unwind through the same shutdown path.

The current crash diagnostic is a C++ terminate report. It is not an OS minidump, and it cannot recover from power loss or arbitrary forced process termination.

The default executable still initializes core services and exits. The optional renderer adapter sample is a separate executable built only when `TALVISOTA_ENABLE_VANTA_ENGINE=ON`.

## Run the Sakkola world locally

On Windows x64, install CMake 3.25+, Git, and Visual Studio 2022 with the **Desktop development with C++** workload and a Windows 10/11 SDK. From the repository root, run:

```powershell
.\Run-Talvisota.ps1
```

The first run configures the pinned VantaEngine revision, builds the renderer and required generated environment textures, then opens the authored Sakkola church-village / Suvanto world. The build is stored in `build/local-game`; subsequent runs reuse it and rebuild only what changed. The default backend is D3D12. To use D3D11 instead:

```powershell
.\Run-Talvisota.ps1 -Backend D3D11
```

For a Debug build:

```powershell
.\Run-Talvisota.ps1 -Configuration Debug
```

The engine-enabled executable launches the first-person Sakkola campaign, not the fixed-camera renderer sample. It opens on the Talvisota main menu with the First Patrol briefing. Completing First Patrol unlocks **Northern Watch**; completing Northern Watch unlocks **Sakkola Defensive Line**, a first authored infantry engagement in the same Sakkola church-village region. The defensive mission uses authored Finnish and Soviet infantry entities and the existing combat simulation. This is fictional connective storytelling in the Sakkola setting, not a claim that a documented battle took place at these exact positions.

**First Patrol:** inspect the farmstead well, check the western woodpile, then report to the Sakkola church. **Northern Watch:** reach the northern barn, check the eastern village boundary, then report to the church watch post. **Sakkola Defensive Line:** join the Finnish section, hold the approach through four ordered infantry objectives, then report to the church post. Mission actor objectives advance from combat defeat; if an authored combat target is defeated before its objective becomes active, its defeat is retained and consumed in campaign order. The combat simulation uses the Finnish 1939 weapon catalog, first-person aim selection, ammunition, reload timing, faction checks, and deterministic NPC fire cadence. CI's `--mission-smoke` traverses the linked campaign, including its combat objectives.

**Controls**
- **WASD:** move
- **Mouse:** look
- **Shift:** sprint
- **Ctrl:** crouch
- **Space:** jump
- **E:** interact / receive campaign orders
- **Left mouse button:** fire at the nearest hostile combatant along the aim ray
- **1 / 2 / 3:** equip Mosin M/27, Suomi KP/-31, or Lahti-Saloranta M/26
- **R:** reload
- **Esc:** pause/resume; Esc from the title menu exits

The game executable requires `TALVISOTA_ENABLE_VANTA_ENGINE=ON` (as configured by `Run-Talvisota.ps1`). The default non-engine `talvisota.exe` build remains a headless lifecycle/CLI shell for CI and core development.

## Optional VantaEngine renderer adapter

The game-facing contract is deliberately independent of the engine:

- `talvisota::presentation::SceneSnapshot` owns game-side mesh, material, instance, and camera data.
- `ISceneRenderer` exposes initialization, resize, render, and device-loss state without exposing Direct3D or VantaEngine types.
- `ValidateSceneSnapshot` rejects invalid indices, non-finite vertex/camera data, invalid PBR factors, missing references, and singular instance transforms.
- `VantaSceneRenderer` is the optional adapter. It translates the snapshot into VantaEngine's public scene-submission API and is pinned to one exact engine commit. Translation is persistent across submissions: meshes and materials are retranslated only when their storage identity or values change, and each submission otherwise validates the camera, environment, and instances and applies only the instance diffs that actually changed. Mesh vertex/index storage and material values must therefore not be mutated in place between submissions; replacing content requires replacing the storage so the adapter detects the change and retranslates it with the full validation diagnostics.

Build the standalone sample on Windows x64 with MSVC:

```powershell
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DTALVISOTA_ENABLE_VANTA_ENGINE=ON
cmake --build build-msvc --target talvisota_renderer_sample talvisota_renderer_contract_tests --parallel
ctest --test-dir build-msvc --output-on-failure -R Talvisota.Presentation.SceneRendererContract
.\build-msvc\talvisota_renderer_sample.exe
.\build-msvc\talvisota_renderer_sample.exe --backend=d3d11
```

The adapter is opt-in because VantaEngine currently uses the Windows SDK and MSVC's WRL headers. The normal MinGW core build remains independent of that dependency. The sample imports the authored Sakkola world definition and region, resolves the manifest assets and materials, builds the full placed environment scene, and renders it through either backend.

Current adapter limitations are explicit: game materials support scalar base colour, roughness, and metallic values only; texture-backed materials and persistent per-mesh GPU caches are not implemented yet. On the game side, per-frame translation cost is limited to camera/environment validation, instance validation, and diff-only instance sync; meshes and materials are retranslated only when their identity changes. The remaining per-frame geometry rebuild and upload happens inside VantaEngine and is out of this repository's scope. D3D12 retains each frame's upload buffers until its fence completes; D3D11 uses dynamic write-discard buffers. Real-device smoke testing is still required before M2 can be closed.

## Engine boundary

VantaEngine is maintained in a separate repository. Talvisota must consume public engine interfaces only. It must not include private D3D11/D3D12 implementation headers, reach into backend resource ownership, or copy engine implementation files into this repository.

The game-facing contract lives in `talvisota/presentation/SceneRenderer.hpp` and contains only game-owned snapshot types. The optional adapter in `VantaSceneRenderer.cpp` translates those snapshots to VantaEngine's public `SceneSubmission` API; no Talvisota game or domain header includes a Direct3D or VantaEngine backend implementation header. The adapter fetches VantaEngine at the exact revision recorded in `CMakeLists.txt`, never from a floating branch. Submitted game materials currently support scalar base colour, roughness, and metallic values; texture-backed material loading and persistent per-mesh GPU caches remain future work.

## Licensing

This repository uses the custom **Talvisota Source-Available Reference Licence** in `LICENSE`. Source inspection and independent inspiration are allowed under its terms. Copying, redistribution, modification, and reuse of project code or original assets are not granted.

This is **not an open-source licence**. Third-party components and assets remain governed by their own licences. Read [LICENSE](LICENSE) before using any material from this repository.
