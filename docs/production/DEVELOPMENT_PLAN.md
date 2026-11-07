# Talvisota Development Plan

**Status:** Approved development sequence  
**Project:** Talvisota — Winter War (1939–1940) first-person game  
**Engine:** VantaEngine, maintained separately  
**Planning rule:** A milestone is complete only when its exit criteria are demonstrated by code, tests, and a reproducible build or runtime check. A merged feature is not automatically complete.

---

## 1. Product target and non-negotiable constraints

### Product target

Build a historically grounded, single-player first-person game with convincing Finnish winter environments, a coherent campaign structure, readable gameplay, strong atmosphere, and stable performance. Historical credibility and visual quality are goals to validate, not claims to make prematurely.

### Non-negotiable architecture rules

1. **Talvisota owns game-specific behavior.** Missions, objectives, characters, factions, equipment definitions, game rules, world composition, progression, and game UI live here.
2. **VantaEngine owns reusable technology.** Rendering backends, generic GPU resource management, platform abstractions, generic asset import, and engine-level timing live in VantaEngine.
3. **No private renderer coupling.** Game and domain code may not include D3D11/D3D12 implementation headers, issue backend commands, or own backend GPU resources.
4. **No premature engine duplication.** Do not copy engine source into Talvisota to bypass a missing public API. Implement the missing capability in VantaEngine.
5. **No speculative systems.** Do not build multiplayer, mod support, complex scripting, editor tooling, or elaborate AI frameworks before the single-player vertical slice demonstrates a real need.
6. **No fake completeness.** No placeholder success paths, pretend asset loading, hard-coded demo behavior presented as a feature, or documentation claiming unverified support.
7. **Test without a GPU where possible.** Core rules, state transitions, content validation, mission logic, and deterministic simulation should run in headless tests.
8. **Measure before optimizing.** Every performance target must have a repeatable workload, hardware/software context, and captured measurements.
9. **Research historical claims.** Keep a source record for substantial historical representations. Clearly distinguish documented fact, uncertainty, and gameplay abstraction.
10. **Track provenance and licences.** Every external library, font, sound, model, texture, photograph, and reference asset needs a recorded source and licence. Online availability does not imply reuse permission.

### Initial release scope

Start with a focused, single-player experience. The first release should prove one complete, polished gameplay loop and a small amount of representative content—not a large unfinished campaign.

Multiplayer, user-generated content, modding, console support, advanced destruction, and expansive editor tooling are out of scope until separately justified.

---

## 2. Milestone sequence

## M0 — Repository and engineering foundation

**Status: Complete — verified**

**Goal:** Make the repository predictable to build, test, review, and extend.

Delivered:
- CMake configuration and Debug/Release presets.
- Compiler policy with strict warnings and warnings-as-errors.
- Windows x64 CI using Ninja, with configure, build, and CTest stages.
- Headless core library and lifecycle tests.
- README build instructions, dependency/licensing policy, and repository hygiene rules.

Exit evidence:
- CI run [42381337923](https://github.com/ItsDropper/Talvisota/actions/runs/42381337923) completed both Debug and Release jobs successfully, including configure, build, and test.
- The workflow uses the same CMake/Ninja build and CTest path documented for developers.
- Core lifecycle tests do not initialize a window or graphics device.

M0 is closed. Future additions to the engineering foundation remain subject to the same CI gate.

## M1 — Game application lifecycle and diagnostics

**Goal:** Create a real executable shell with reliable startup, runtime ownership, failure reporting, and shutdown.

Delivered:
- A single executable entry point and `Application` composition root.
- Explicit ownership of the application session, configuration, logger, and platform path/crash services.
- Severity-filtered, timestamped console/file logging.
- Versioned, strict key/value configuration with defaults created on first launch; unknown keys, duplicate keys, malformed values, and unsupported versions fail with line diagnostics.
- Per-user configuration/data paths for Windows, Linux, and macOS.
- A terminate handler that writes an exception-aware crash report.
- Ordered startup and shutdown, including cleanup after partial initialization failure.
- Headless tests for configuration validation, application startup, repeated lifecycle cycles, diagnostics creation, and initialization failure.
- A CLI `--help` path.

Scope boundary:
- This is an application shell, not a playable game loop. It deliberately exits after core services initialize because game-owned renderer integration is blocked on M2.
- Crash reporting currently covers C++ termination; it does not claim to produce OS minidumps or recover from arbitrary process termination.

Exit criteria:
- [x] Application startup and shutdown are exercised on the supported platform by headless tests.
- [x] Initialization failures return subsystem-specific diagnostics and enter the shutdown path.
- [x] Repeated independent startup/shutdown cycles are exercised by tests.
- [x] Invalid configuration is rejected with a clear diagnostic; missing configuration receives explicit defaults.
- [x] The application composition root is tested without a graphics device.
- [x] Windows Debug and Release CI pass with warnings-as-errors and the executable smoke test.

M1 is closed after the successful CI rerun following the strict-warning fix in `Config.cpp`.

## M2 — VantaEngine game-integration contract

**Goal:** Make the engine capable of rendering a scene owned by Talvisota through public APIs.

**Implementation status (2026-11-07): Implemented; milestone remains open for hardware verification.** VantaEngine exposes a renderer-independent scene contract consumed by both D3D11 and D3D12. Submitted geometry is validated, transformed into world space with inverse-transpose normal handling, uploaded through backend-owned frame resources, rendered in the visible pass, and included in cascaded shadow passes. D3D12 frame-slot upload buffers remain alive until the matching fence completes; D3D11 uses dynamic write-discard buffers. Both backends expose frame success and device-removal state. VantaEngine includes a separate game-owned scene sample. Talvisota has a backend-independent `ISceneRenderer` contract, snapshot validation, an optional adapter pinned to `694256a9d4724f2d273c8122e821fd99a6627fbf`, and a standalone Win32 sample. The adapter build is opt-in and requires Windows x64/MSVC. The pinned adapter build and game-owned contract test pass in CI run [42384865692](https://github.com/ItsDropper/Talvisota/actions/runs/42384865692). The implementation is in place; M2 remains open until real-device D3D11 and D3D12 smoke verification is recorded.


This is a cross-repository milestone. Engine-side implementation belongs in VantaEngine; game-side integration belongs here.

Required engine work:
- Public renderer-independent scene submission for meshes, instances, transforms, materials, and visibility.
- Explicit resource handles with ownership, validity, and destruction rules.
- A defined CPU asset-to-GPU upload lifecycle.
- Per-instance transforms in both visible and shadow rendering paths.
- A documented frame lifecycle, synchronization model, resize handling, and device-loss/error reporting.
- Game-owned camera and frame data.
- D3D12 and D3D11 parity for the public features Talvisota actually uses.
- A sample application that uses the same public API as an external game.

Talvisota-side work:
- Pin VantaEngine to an exact reviewed commit.
- Add a narrow engine adapter behind game-owned interfaces.
- Add an integration test/sample that submits a Talvisota-owned scene.
- Ensure game code never reaches through the adapter into backend internals.

Exit criteria:
- [x] Talvisota defines and validates a game-owned scene snapshot without VantaEngine or Direct3D types.
- [x] An optional adapter translates game-owned snapshots into the public VantaEngine scene API without private backend includes.
- [x] The adapter pins an exact VantaEngine commit rather than following a branch.
- [x] Both backends expose the same submitted-frame API, including visible and cascaded-shadow submission.
- [x] CPU resource handles, stale-handle rejection, resource replacement, visibility, and snapshot lifetime have unit coverage.
- [x] The pinned MSVC adapter configuration, renderer/sample build, and game-owned contract test pass in [CI run 42384865692](https://github.com/ItsDropper/Talvisota/actions/runs/42384865692).
- [ ] Real-device D3D11 and D3D12 smoke tests confirm transforms, materials, visibility, shadows, resize, and device-loss reporting.

Known M2 scope boundary: persistent per-mesh GPU caches and texture-backed game materials are deferred. The current upload path is per-frame and scalar-PBR only.

**Boundary:** M3 content schemas, manifest validation, provenance tracking, and offline asset processing can proceed independently. Do not build the real game world, content placement pipeline, or presentation features around renderer internals. Renderer-backed environment integration remains gated on M2 real-device verification.

## M3 — Content and asset pipeline

**Goal:** Make authored content loadable, traceable, validated, and safe to fail.

**Implementation status (2026-11-07): In progress.** The production foundation includes a versioned UTF-8 content manifest, stable-ID registry, kind/extension checks, root-contained path validation (including canonicalized symlink targets), required/optional file policy, mandatory source/licence metadata, dependency validation, deterministic dependency-first ordering, cycle detection, exact manifest/line/field diagnostics, and atomic registry publication. Typed v1 PBR material and world definition parsers now validate stable identity and manifest-path ownership. Materials validate linear base/emissive values, roughness/metallic ranges, normal and occlusion factors, alpha mode, sidedness, and texture-slot references restricted to declared texture dependencies. World definitions enforce a fixed metre-based right-handed coordinate system (+X east, +Y up, +Z north), finite ordered bounds, and optional sea level. Texture slots define sRGB versus linear interpretation and ORM channel packing in [material-definition-v1.md](../../data/schemas/material-definition-v1.md). Headless tests cover manifest, typed material, and typed world-definition success/failure paths. This is still an early M3 slice, not completion of the content or media pipeline.

Delivered so far:
- `Talvisota::Content` runtime library with no renderer or VantaEngine dependency.
- Stable IDs independent of source paths and deterministic dependency ordering.
- Fail-closed validation with no partial registry publication.
- Explicit external source and licence/permission metadata.
- CI-registered content registry tests.
- A command-line content validator with actionable manifest/material/world diagnostics, deterministic load-order output, and a CI check against the shipped content catalog.

A bounded BMP decoder and a true-colour TGA decoder are now available. BMP supports uncompressed 24-bit BGR and 32-bit BGRX BI_RGB; TGA supports raw type 2 and packet-RLE type 10 at 24/32 bits, with all origin combinations. Both normalize to RGBA8 and are exercised by malformed-input regression tests. The validator decodes registered BMP/TGA assets. An offline texture processor generates full mip chains and deterministic little-endian TVTTEX01 files, filtering sRGB RGB channels in linear light and preserving linear alpha semantics. Debug/Release CI covers decoder and deterministic-output tests. PNG/JPEG/DDS/KTX2 decoders, glTF/GLB import, MTL material/texture resolution, runtime GPU upload, and the broader content definition families remain outstanding. A bounded Wavefront OBJ geometry importer now validates and triangulates faces, resolves positive/negative indices, applies explicit source-unit scaling, preserves material grouping names, and generates missing flat normals; the content validator imports registered OBJ assets and fails explicitly on unsupported glTF/GLB.

Remaining M3 work includes glTF/GLB model decoding, MTL material/texture resolution, PNG/JPEG/DDS/KTX2 image decoding, mission/character/faction/equipment schemas and cross-reference validation, migrations/compatibility policy, processed-asset cataloguing, audio validation, and clean content packaging. The current M3 implementation remains incomplete. The material schema is authoring data only; it does not claim that the current renderer adapter supports texture-backed materials.

Work:
- Define stable IDs for assets and authored content; do not use file paths as persistent identity.
- Define versioned schemas for world, mission, character, faction, equipment, and material definitions.
- Build a manifest-based content registry with explicit dependency resolution.
- Add validation for missing references, duplicate IDs, unsupported formats, invalid values, and version mismatch.
- Define model scale, coordinate system, material, texture colour-space, and audio format conventions.
- Separate source assets from processed runtime assets.
- Build deterministic import/processing tools with actionable diagnostics.
- Add provenance and licence metadata for every external asset.
- Define loading policy for required versus optional assets and clean failure behavior.

Exit criteria:
- A clean content build validates all shipped definitions and references.
- Invalid content fails with exact file and field diagnostics.
- Asset IDs remain stable when files move.
- Importing the same source with the same tool version/settings produces reproducible output where the format permits it.
- Failed loads never publish partially initialized resources.
- Every shipped external asset has a recorded permission/licence basis.

## M4 — World foundation and first representative environment

**Goal:** Prove the game can own, load, traverse, and present a complete environment.

Work:
- Define world coordinates, units, orientation, region boundaries, and world metadata.
- Establish terrain and environment data formats.
- Add a world composition pipeline for terrain, vegetation, rocks, structures, and set dressing.
- Define collision and navigation data separately from visual geometry.
- Add world-region loading and unloading only when measured memory or traversal needs justify it.
- Establish environment lighting, sky, fog, snow, surface response, and weather presentation as game-configurable data using engine capabilities.
- Build one small representative environment that exercises the full pipeline.

Exit criteria:
- The environment is authored as game content, not embedded in renderer source.
- World loading reports missing or invalid dependencies clearly.
- Traversal, collision, and visual representation agree within documented tolerances.
- The environment can be loaded, unloaded, and reloaded without stale resources.
- Performance and memory are measured on a documented test configuration.
- The environment is visually reviewed in a repeatable capture setup.

Do not attempt a huge open world at this stage. Prove a small region first.

## M5 — Core player experience

**Goal:** Implement the minimum complete first-person player loop.

Work:
- Define player state and input mapping independently of the renderer.
- Implement movement, camera behavior, interaction, and player-state transitions.
- Establish separation between simulation state and presentation state.
- Add accessibility and input settings early enough to influence the architecture.
- Add save-safe state boundaries and deterministic tests for core rules.
- Integrate animation and sound through presentation events rather than embedding them in domain logic.

Exit criteria:
- The player can enter the environment, move, interact with intended objects, and reach a defined end state.
- Input is configurable and invalid bindings are handled safely.
- Core movement/state logic is testable without a renderer.
- Player state remains valid through pause, loading, and returning to the frontend.
- No required gameplay behavior depends on sample-scene code.

## M6 — Gameplay systems and mission framework

**Goal:** Build reusable systems only where the first mission requires them.

Work:
- Define mission lifecycle, objectives, triggers, completion, failure, and restart semantics.
- Define game entity identity and lifecycle.
- Add interaction and gameplay event contracts.
- Implement equipment/inventory rules required by the vertical slice.
- Implement combat as game rules and presentation with explicit state, validation, and tests; keep it independent from rendering and input devices.
- Add opponent behavior only to the level required by the designed encounter.
- Keep mission content data-driven while keeping complex rules in tested code.
- Add save/load only for state that the initial release actually needs to persist.

Exit criteria:
- One mission can start, progress, complete, fail, and restart reliably.
- Mission rules are not hard-coded into the application entry point or renderer.
- Invalid mission data is rejected before play.
- Gameplay systems have deterministic unit tests for their rules and transitions.
- The mission can be replayed from a clean state without stale entities or resources.

## M7 — First complete vertical slice

**Goal:** Deliver one short, end-to-end playable experience using production architecture.

The vertical slice must include:
- Application startup and frontend entry.
- A game-owned environment loaded through the real content pipeline.
- Player control and interaction.
- One complete mission flow with a beginning, middle, and end.
- Required characters/entities and authored objects.
- Representative materials, lighting, sound, and UI.
- Pause, restart, failure handling, and clean exit.
- Basic settings and a reliable save/config boundary if needed.
- Debug diagnostics and performance capture.

Exit criteria:
- A fresh build can be launched from documented steps.
- A tester unfamiliar with the code can complete the experience without developer intervention.
- No known progression blocker or data-loss issue remains.
- The full slice works from a clean checkout and clean user-data directory.
- A repeatable performance capture is recorded.
- Known limitations are documented honestly.

**This is the first point at which the project should be judged as a game rather than a collection of engine and gameplay systems.**

## M8 — Visual quality, audio, and performance production

**Goal:** Raise fidelity based on the proven vertical slice rather than speculative rendering features.

Work:
- Establish a visual reference target and measurable image-quality review process.
- Improve material authoring, physically based shading, texture quality, environment lighting, shadow quality, atmospheric depth, vegetation, terrain, and surface detail.
- Improve animation, character presentation, environmental audio, spatial mix, and UI feedback.
- Profile CPU, GPU, memory, streaming, and loading separately.
- Set performance budgets per frame and per subsystem for the agreed target hardware.
- Add scalable quality settings and graceful fallbacks.
- Use image captures and performance traces to verify improvements.

Exit criteria:
- Visual changes are reviewed against fixed reference scenes and captures.
- Performance targets are met on documented target hardware or remaining gaps are explicitly accepted.
- Quality settings produce predictable results and do not corrupt content or state.
- No major visual feature causes unbounded memory growth or persistent frame-time spikes.
- Audio and visual feedback remain coherent across gameplay states.

Do not promise photorealism based on shader complexity or screenshots alone. Validate lighting, materials, scale, animation, environment detail, and performance together.

## M9 — Campaign and content production

**Goal:** Expand from the proven slice without destabilizing the foundation.

Work:
- Define campaign structure and content standards.
- Build additional environments and missions through the validated pipeline.
- Maintain historical source records and uncertainty notes.
- Reuse systems, not entire mission scripts.
- Track asset completion, integration status, performance cost, and licence status.
- Add localization and accessibility coverage for all shipped UI and player-facing text.

Exit criteria:
- Every mission passes content validation and a full playthrough.
- Campaign progression and save compatibility are tested.
- Content quality and historical review have named acceptance criteria.
- Asset provenance and licence records are complete.
- New content does not regress the vertical-slice performance and stability baseline.

## M10 — Release hardening

**Goal:** Produce a reliable, supportable release candidate.

Work:
- Test clean installation, upgrade, repair, uninstall, and user-data preservation.
- Test supported hardware/driver combinations and quality settings.
- Run long-session stability and memory-growth tests.
- Verify save compatibility, corruption handling, and recovery behavior.
- Audit dependencies, licences, notices, and asset provenance.
- Verify crash diagnostics and privacy behavior.
- Freeze scope; only accept release blockers and high-priority fixes.
- Publish known issues, minimum/recommended requirements, and reproducible build information.

Exit criteria:
- Release candidate passes the full regression suite.
- No unresolved critical crash, progression blocker, save corruption, or security issue remains.
- Performance and hardware requirements are published from actual measurements.
- All shipped code and content have a clear rights/provenance record.
- Release package can be reproduced from a clean, documented process.

---

## 3. Parallel work tracks

### Historical research
- Maintain source records as described in docs/history/RESEARCH_STANDARDS.md.
- Prioritize research for content currently being built.
- Flag uncertainty and disputed interpretations.
- Do not delay all engineering for exhaustive research into content not yet scheduled.

### Art direction
- Define a visual target using reference boards and a small number of representative scenes.
- Keep reference images separate from shipped assets.
- Record source and usage rights for reference and production material.
- Validate art direction in the actual engine, not only in offline renders.

### Quality and testing
- Add tests alongside each system, not months later.
- Maintain a small repeatable runtime smoke-test route.
- Keep known issues and technical debt visible.
- Use profiling captures to make optimization decisions.

### Production planning
- Break milestones into small issues with explicit acceptance criteria.
- Each issue has one owner, one purpose, dependencies, and a test plan.
- Do not start work blocked by an unresolved upstream API decision.
- Keep a short decision record when changing architecture, dependencies, formats, or scope.

---

## 4. Architecture rules by layer

| Layer | Owns | Must not own |
|---|---|---|
| Core | lifecycle, time contracts, IDs, diagnostics interfaces | game mission rules, GPU objects |
| Gameplay | simulation orchestration and game rules | renderer, OS handles, file parsing details |
| Entities | actor identity and game components | GPU lifetime, renderer internals |
| World | world state, region composition, traversal data | Direct3D implementation |
| Missions | objectives, triggers, mission transitions | renderer commands, platform startup |
| Equipment/combat | game rules and state | input device polling, GPU resources |
| Content | schemas, manifests, validation, stable ID resolution | mission execution, GPU command submission |
| Presentation | camera/animation/UI mapping and render-facing data | authoritative game rules |
| Engine adapter | translation to public VantaEngine APIs | mission-specific rules |
| Platform/app | OS integration and composition root | hidden global gameplay state |

Dependency direction must remain explicit. Avoid circular module dependencies. If two systems need each other, introduce a small shared contract in the correct lower-level module rather than making them include each other's implementation.

---

## 5. Definition of done for every change

- [ ] Its owner and architectural layer are clear.
- [ ] Public interfaces have explicit inputs, outputs, ownership, and failure behavior.
- [ ] Normal, invalid, and boundary cases are handled.
- [ ] Regression tests cover the bug or behavior.
- [ ] No placeholder, fake success path, or silent fallback hides missing work.
- [ ] Resource creation and destruction are safe on partial failure.
- [ ] Debug and Release builds pass.
- [ ] Relevant CI jobs pass.
- [ ] Content and third-party licence implications are recorded.
- [ ] Documentation describes actual behavior and known limitations.
- [ ] Performance claims include repeatable measurements.
- [ ] The feature has been exercised in its real runtime path when one exists.

Not every change requires every test category, but any skipped category must be justified in the change description.

---

## 6. Explicitly deferred work

Do not begin these until the vertical slice proves the need and the core architecture can support them:

- Multiplayer, dedicated servers, matchmaking, and anti-cheat.
- User modding, player-facing scripting, and workshop integration.
- Large open-world streaming and procedural world generation.
- Complex destruction simulation.
- Advanced editor suites and custom visual scripting.
- Console ports and platform-specific certification work.
- Expensive rendering techniques not justified by measured image-quality gains.
- Large-scale campaign production before the first mission loop is stable.

Deferred does not mean rejected. These features must earn their complexity through a product requirement.

---

## 7. Immediate execution order

1. **M0 is closed:** the latest complete Debug/Release CI run passed.
2. **M1 is closed:** Debug/Release CI and headless application tests passed.
3. **Finish the M2 gate:** pass the pinned MSVC adapter CI job and run the Talvisota-owned sample on both D3D11 and D3D12 hardware paths.
4. **Continue M3 independently of the M2 hardware gate:** build typed content schemas, manifest-driven definitions, provenance, and deterministic offline asset processing. Do not integrate renderer-backed world loading until M2 real-device verification is recorded.
5. **After M3 content validation and the M2 hardware gate:** build one small game-owned environment to prove the asset pipeline, scene submission, world ownership, and runtime lifecycle together.
7. **Implement the minimum player loop and one mission:** keep scope narrow and complete it end-to-end.
8. **Capture and review the first vertical slice:** use runtime evidence to decide what visual, gameplay, and performance work comes next.

Do not jump ahead to a large map, detailed equipment simulation, campaign content, or photorealistic asset production. Those efforts would be built on unproven integration and content pipelines.

## 8. Decision gate: when to change this plan

The plan may change when:
- A measured technical constraint invalidates an assumption.
- A real playtest demonstrates a product requirement not covered here.
- Historical research changes a planned representation.
- A platform or licensing constraint requires a different implementation.
- A milestone's acceptance criteria prove too weak or too expensive.

A plan change must record:
1. The assumption that failed.
2. Evidence for the change.
3. The affected milestones and dependencies.
4. The new acceptance criteria.
5. Any work that should be removed rather than carried as permanent debt.

The objective is not to follow this document blindly. The objective is to make deliberate, evidence-backed decisions and avoid building expensive systems in the wrong order.
