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
- The non-engine build retains a headless CLI shell for lifecycle and CI checks. With `TALVISOTA_ENABLE_VANTA_ENGINE=ON`, the primary `talvisota.exe` is the Win32 game runtime: it initializes application services, loads the authored Sakkola level, owns the player loop, submits frames through the game renderer adapter, and exposes the main menu/pause frontend. The renderer sample remains a separate diagnostics target.
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

**Implementation status (2026-11-10): implemented; milestone remains open for hardware verification.** VantaEngine exposes a renderer-independent scene contract consumed by both D3D11 and D3D12. Submitted geometry is validated, transformed into world space with inverse-transpose normal handling, uploaded through backend-owned frame resources, rendered in the visible pass, and included in cascaded shadow passes. D3D12 frame-slot upload buffers remain alive until the matching fence completes; D3D11 uses dynamic write-discard buffers. Both backends expose frame success and device-removal state. VantaEngine includes a separate game-owned scene sample. Talvisota has a backend-independent `ISceneRenderer` contract, snapshot validation, an optional adapter pinned to `22ca041bd80d6ecac867d0c7939a87aa18b809b1`, and a standalone Win32 sample. The adapter build is opt-in and requires Windows x64/MSVC. The last completed adapter build on the previous engine pin passed the game-owned renderer/content contract tests, asset validation, deterministic package build, package revalidation, and five-view D3D12 diagnostic capture in [CI run 42507908980](https://github.com/ItsDropper/Talvisota/actions/runs/42507908980). That run predates the current pin; M2 remains open until the current revision is verified on real D3D11 and D3D12 devices. CI success does not establish physical-device coverage. M2 remains open until real-device D3D11 and D3D12 smoke verification records transforms, materials, visibility, shadows, resize, and device-loss behavior.


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

Known M2 scope boundary: persistent per-mesh GPU caches remain deferred. Texture-backed submitted materials (base colour, tangent-space normal, roughness, metallic, and ambient occlusion) now have engine-side D3D11 and D3D12 paths, but real-device verification is still open. The scene upload path remains per-frame.

**Boundary:** M3 content schemas, manifest validation, provenance tracking, and offline asset processing can proceed independently. Do not build the real game world, content placement pipeline, or presentation features around renderer internals. Renderer-backed environment integration remains gated on M2 real-device verification.

## M3 — Content and asset pipeline

**Implementation status (2026-11-07): complete.**

M3 delivers the v1 content pipeline from authored source through typed validation, engine-backed import/decode, deterministic texture processing, catalog-bearing packaging, and semantic/integrity revalidation. The closure evidence below records the M3 implementation and validation gate; subsequent M4 work continues to extend the engine-backed environment material and texture-processing path.

Delivered:

- Stable asset IDs, strict UTF-8 manifest parsing, dependency graph validation, root-contained paths, and source/licence provenance.
- Typed v1 material, world, mission, faction, character, and equipment definitions. World terrain references must resolve to a present model and be explicit manifest dependencies.
- OBJ import with multiple MTL libraries, named material/submesh assignment, registered sidecar dependencies, and canonical root-containment checks.
- glTF 2.0/GLB mesh import, external images, base64 and percent-encoded data-URI images, and GLB buffer-view images. Embedded payloads remain owned by CPU materials; no sidecar extraction occurs.
- WIC external/embedded texture decoding, strict processed `.ttex` read-back, and deterministic CPU mip generation.
- Full bounded decoding of PCM/IEEE-float WAV, Ogg Vorbis, and FLAC, including format, duration, channel, sample-rate, frame-count, and finite-sample checks.
- Deterministic packages containing a normalized `content.tmanifest` and SHA-256 `package.tmanifest`. Package verification reloads the packaged catalog, validates dependencies and typed definitions, reimports/decodes packaged media, and verifies exact inventory/file correspondence.
- Original 64-metre terrain validation region with OBJ/MTL geometry, snow albedo source texture, a PBR material definition, and a world definition.
- End-to-end CI coverage for OBJ/MTL, embedded glTF image decode, BMP-to-mip-chain-to-`.ttex` processing, WAV/OGG/FLAC decoding, packaging, and package revalidation.
- Current shipped-media provenance and vendored-code licence audit: [ASSET_RIGHTS_AUDIT.md](ASSET_RIGHTS_AUDIT.md).

The terrain region is a pipeline validation asset, not the final photorealistic game environment. The embedded image path is imported and CPU-decoded here; GPU material upload/render integration belongs to the renderer milestones. Audio is decoded for content validation; runtime playback is a separate subsystem.

There is no historical content schema version to migrate. The current authored formats are v1-only; an empty migration framework would be scaffolding. A deterministic migration tool is required when a real v2 schema and supported v1-to-v2 transformation are defined.

**M3 closure gate:** passed in [Talvisota CI run 42406306492](https://github.com/ItsDropper/Talvisota/actions/runs/42406306492): Windows x64 Debug, Release, and MSVC renderer-adapter jobs all succeeded. The adapter job built the engine-backed tools, passed the renderer/content contract tests, validated the shipped assets, built the deterministic package, and fully verified the packaged catalog and media. M2 remains independently open for actual D3D11/D3D12 hardware verification.

### Screenshot-driven material correction (2026-11-08)

The first five-view D3D12 capture exposed two material defects that were not caught by the previous range checks: the snow albedo had only about 0.8 levels of per-channel standard deviation in the generated 8-bit map, leaving broad surfaces nearly featureless, while the ice's 13-cell Voronoi fracture network produced conspicuous multi-metre polygon tiles at its 64 m UV repeat. The environment generator now increases snow and packed-snow albedo modulation at drift, scuff, and granular scales, validates minimum snow albedo range, and tightens the ice fracture network to 37 primary and 89 secondary cells per repeat. This is a material correction only. The Sakkola heightfield is not flat: it spans 1.78–19.206 m, but its 177x177 grid covers 4,096 m at approximately 23.27 m per sample. The snow surface now lives in the game-owned terrain contract as the versioned `terrain_surface\tsnow_drift_v1` profile. The exact deterministic displacement function is shared by renderer vertex refinement, placement transforms, `SampleTerrainHeight`, player spawn, grounded movement, and landing; terrain normals include the displacement gradient. Regression tests cover profile parsing, source-mesh preservation, spawn alignment, and grounded traversal. Rendering and gameplay no longer maintain separate copies of the snow field. The low-detail, repeated tree silhouettes and broader environment composition also remain open. The pre-merge capture took 6.0–6.6 seconds per frame; a later capture after material-run merging reported 8,624 material-bound instances and 4.47–4.95 seconds per frame, but its renderer-adapter job was cancelled, so these are diagnostic artifacts rather than a successful CI performance result.

## M4 — World foundation and first representative environment

**Implementation status (2026-11-13): in progress.** The game-owned region composition slice is implemented: `.tregion` v1 defines bounded visual placements, sampled terrain heightfields, independent collision boxes, and directed navigation nodes/edges. The loader validates parent-world containment, stable asset references, explicit manifest dependencies, finite transforms, unique local IDs, complete heightfield grids, terrain bounds/elevations, collision extents, and navigation references. Terrain queries use bilinear interpolation and gradient-derived normals; the 17x17 Central Finland validation heightfield and the 177x177 Sakkola heightfield are regression-tested against their source OBJ vertex grids. Region load/reload is transactional and unload clears the active region. Placement transforms, AABB collision queries, terrain surface queries, and deterministic directed-graph shortest paths have headless coverage. The first fixed Karelian environment is **Sakkola Kirkonkylä and the north shore of Suvanto**.

The current Sakkola region spans 4,096x4,096 metres with a 177x177 terrain heightfield at approximately 23.2727-metre spacing, matched to the terrain OBJ vertex grid. It contains 3,094 vegetation placements across Scots pine (1,667), Norway spruce (140), additional pine (100), birch (497), aspen (351), and alder (339); 32 boulders; 48 structure-category placements (35 exterior buildings—one church, 12 farmhouses, 11 barns, and 11 saunas—plus 13 interior model placements); and 803 prop-category placements, including 420 common-juniper placements, 300 winter-brush clumps, 28 fallen logs, 26 stumps, 12 fence sections, 12 reed placements, two woodpiles, a well, the frozen Suvanto surface, and the packed rural track. Tree, boulder, fallen-log, stump, and shoreline-reed placement bases are aligned to the sampled terrain; tree, log, and stump collision bottoms are regression-tested against that same surface. Its composition is informed by the National Land Survey's pre-war 1:20,000 Sakkola sheet 404206_SAKKOLA.tif; the current elevations, shoreline curve, and precise layout remain authored approximations, not a georeferenced map extraction. See [Sakkola environment references](SACKKOLA_ENVIRONMENT_REFERENCE.md).

The world definition carries a validated winter presentation profile: low-angle sun direction and colour, ambient sky colour/intensity, fog colour/density/start distance, and normalized wind direction/strength. The profile is forwarded through the game scene contract into both VantaEngine D3D11 and D3D12 material and sky shading. The loaded region also snapshots that wind profile for CPU terrain sampling: `snow_drift_v1` now aligns ridge/drift anisotropy with the authored wind and adds bounded lee-slope accumulation from the bilinear heightfield gradient. Broad moraine relief and fine granular variation remain wind-independent, and frozen-water surfaces remain undisplaced. The same deterministic field drives terrain mesh refinement, `SampleTerrainHeight`, placement grounding, and traversal. This closes a missing world input in the previous isotropic field; it remains an art-directed surface response, not a snow-transport simulation or georeferenced terrain. VantaEngine accepts and samples submitted-scene base-colour, tangent-space normal, roughness, metallic, and ambient-occlusion textures on both backend paths; the D3D12 shader also adds material-aware procedural micro-relief where authored normal maps are absent. Its CPU texture processor now generates tangent-space normal mip chains by normalizing source vectors, averaging unit vectors, and renormalizing each level while filtering alpha linearly. Talvisota exposes this as the `normal` mode of `talvisota_texture_process`, distinct from generic linear-data filtering, and pins VantaEngine to `22ca041bd80d6ecac867d0c7939a87aa18b809b1`. CPU-only D3D11/D3D12 shader compilation is part of engine CI. The environment generator produces 41 deterministic 1024x1024 PBR maps across ten material families, and now verifies periodic continuity on both UV axes for every procedural field before writing. These remain procedural first-pass assets, not final scanned or hand-painted materials. The MSVC CI job captures seven genuine D3D12 renderer viewpoints (five exterior and two interior) after nine frames per view and archives screenshots plus `renderer.log`. The capture path writes per-view camera, visible-instance, LOD0-3 selection and estimated submitted-triangle diagnostics directly to `renderer.log`. It is a visual regression harness, not a frame-rate benchmark; it does not collect wall-frame timing or p95. The renderer-adapter sample contains a p95 timing harness, but CI does not run that sample. The capture workflow validates non-empty LOD/triangle diagnostics for both scenes. These are CPU-side scene-submission estimates, not GPU timestamp queries or a substitute for profiling on the target RTX 3060 and real D3D11/D3D12 hardware. Capture success is not a photorealism or performance acceptance gate. The Scots pine and granite-boulder generators publish four LODs with projected-screen-coverage hints; runtime selection now uses those authored thresholds and the active camera FOV rather than fixed world-distance cutoffs. The current Sakkola region contains 1,667 Scots pine placements and 32 boulders. The D3D12 capture job in run 42884472600 completed the MSVC renderer job and produced seven Sakkola and seven Taipale views. Its scene logs report 3,978 Sakkola placements/12,023 material-bound instances/104 meshes/87 materials and 698 Taipale placements/2,008 instances/82 meshes/65 materials. Visual review shows the Sakkola overview and Suvanto shoreline, and several Taipale riverfront/watch-post views, are mostly flat lake/sky; Taipale's forest and farmstead are sparse; snow remains visually uniform; and church/farmhouse interiors show strong repeating texture bands. These are observed M4 content/material/camera gaps, not LOD regressions. Successful full CI [run 42892103260](https://github.com/ItsDropper/Talvisota/actions/runs/42892103260) captured seven views for each scene and reported zero invalid LOD selections. Estimated visible-set triangle reductions reached 54-62% in the Sakkola overview/village/forest/farmstead/interior views, but were 0% at Suvanto shoreline; Taipale reductions exceeded 50% only in its forest and command-hut views, while the other five views remained at 0-0.69%. Taipale captures show only 15-47 visible instances per view, confirming that environment composition/terrain coverage—not LOD—is the immediate gap. The runtime keeps one material-bound instance per placement, maps each LOD by stable material-slot index, reuses identical material resources, and reports per-LOD visible counts plus a same-visible-set triangle baseline. This is a geometry-budget capability, not a verified frame-rate gain. M4 remains open for historical-map-derived terrain refinement, material-specific calibration and final authored PBR maps, renderer-backed visual review on real D3D11/D3D12 hardware, richer set dressing, streaming where justified, and documented performance/memory measurements. This is an environment foundation, not a photorealism claim.

### Frozen-river material scale correction (2026-11-12)

The D3D12 Taipale capture in [CI run 43083903709](https://github.com/ItsDropper/Talvisota/actions/runs/43083903709) exposed an oversized ice pattern on the river ribbon. Source inspection found that its 4.68 km curved centreline was mapped to only eight U repeats and its 120 m channel width to one V repeat. The mesh now derives UVs from cumulative centreline arc length and local channel width at the same 64 m physical repeat used by the Sakkola frozen-water surface. The headless environment asset contract verifies every cross-section and face/UV index.

The first post-scale D3D12 capture in [CI run 43274476710](https://github.com/ItsDropper/Talvisota/actions/runs/43274476710) still shows a strong magenta/cyan specular patchwork on the river ribbon. The generated albedo itself is blue-grey (mean RGB 200/219/234), so that colour cast is not authored into the source albedo. The generated roughness map averages 0.328; the D3D12 shader multiplies that by the MTL roughness factor 0.78, yielding an effective mean near 0.256, while the material's metallic factor is 0.12. This is the source of the overly reflective, tinted response. The material is now calibrated as a dielectric with a white base-colour factor, zero metallic, and an absolute roughness map (Pr=1). The deterministic generator enforces a 0.56–0.68 procedural mean and a 140–180/255 generated-BMP mean; the headless asset test checks the material factors and map bindings. The latest complete D3D12 capture in [CI run 43410604295](https://github.com/ItsDropper/Talvisota/actions/runs/43410604295) renders the calibrated ice as blue-grey in the Taipale viewpoints rather than the earlier magenta/cyan patchwork. This is a screenshot observation, not a physical-device or photorealism acceptance claim.

### Scots pine branch snow-load geometry (2026-11-13)

The first PBR review renders showed the branch snow as thin, one-sided strips rather than a load with visible thickness. The generator now emits closed shallow-volume caps on near-horizontal primary and secondary boughs. Each cap uses a seven-point rounded transverse profile, five longitudinal stations, a buried underside and two end faces (66 triangles per cap); a dedicated seeded random stream keeps the snow independent of the established branch and needle topology. The source generator checks that every snow edge has exactly two incident faces and rejects degenerate polygons. The Blender verifier repeats the closed-edge check after GLB export/import, welding duplicated seam vertices by position, and verifies exact per-LOD cap counts (120 / 80 / 42 / 18) and triangle budgets while preserving bark/needle floors. CI renders seven PBR review angles, including an upper-crown snow detail, and archives the images with the runtime model. This is still deterministic art-directed geometry, not a physical snow-transport model, photogrammetry, or final reference-calibrated tree art. M4 remains open pending runtime review, terrain/environment composition work, and licensed reference-driven asset replacement.

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
- The MSVC CI job captures five actual D3D12 renderer viewpoints as diagnostic artifacts. These captures must remain genuine renderer output; their presence or successful execution does not satisfy the photorealistic quality gate. Final visual acceptance still requires review against fixed references on supported hardware.

Do not attempt a huge open world at this stage. Prove a small region first.

## M5 — Core player experience

**Status (2026-11-10): in progress.** The renderer-independent player simulation includes acceleration, ground friction, reduced air control, terminal fall speed, jump/gravity, crouch posture and eye height, stand-up clearance checks, terrain grounding, and collision queries. The engine-enabled primary `talvisota.exe` loads the authored Sakkola world and First Patrol mission, synchronizes the player entity with first-person movement, advances mission simulation at a fixed 60 Hz, and routes E-key interactions through the tested gameplay API. The objective HUD shows the active task, progress, distance and interaction prompt; the title menu displays the authored mission briefing and supports keyboard/mouse start, continue, replay, pause, resume and return-to-menu. Mission interaction anchors are derived from authored collision bounds so the well, woodpile and church objectives can be approached from outside their collision volumes. Exact-head CI [run 42726140593](https://github.com/ItsDropper/Talvisota/actions/runs/42726140593) passed Windows Debug/Release builds and tests, the MSVC game/renderer build, a headless First Patrol smoke that verified all three anchors with `InitializePlayer` and completed the mission via `AdvanceSimulation`, renderer/content validation, deterministic package verification, playable package assembly, and five-view D3D12 capture. This proves the content-to-simulation path and packaged executable build, not a human interactive playthrough or visual acceptance. Remaining work: extract a maintainable `GameRuntime` composition from the Win32 entry point, remove duplicated scene-building logic shared with diagnostics, add configurable bindings, improve terrain/slope collision, and perform an interactive playthrough plus UI/visual review.

**Goal:** Implement the minimum complete first-person player loop.

Work:
- [x] Define player state and renderer-independent movement/camera simulation.
- [x] Exercise the player simulation in the Sakkola renderer sample with keyboard movement, sprint, jump, and look.
- [x] Add raw mouse look with keyboard fallback to the renderer sample.
- [x] Add acceleration, friction, air control, terminal velocity, crouch, and overhead clearance checks to the player simulation.
- [ ] Add configurable input bindings.
- [x] Integrate player/world ownership into the engine-enabled application game loop and connect it to the renderer contract.
- [x] Build the main menu and pause/resume UI inside the application-owned game executable.
- [ ] Add robust terrain following, step/slope policy, and collision coverage for vertical obstacles and ceilings.
- [x] Implement interaction, mission objectives, and player-state transitions (First Patrol runtime smoke passes all three authored objectives; human playthrough still pending).
- [ ] Establish save-safe state boundaries and deterministic tests for core rules.
- [ ] Integrate animation and sound through presentation events rather than embedding them in domain logic.

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
