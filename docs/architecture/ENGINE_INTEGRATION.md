# VantaEngine integration boundary

## Current implementation

VantaEngine is a separate C++23 project and owns the reusable renderer, GPU resource lifecycle, platform abstractions, and generic asset import/processing APIs.

The public scene-submission contract is implemented for both D3D11 and D3D12:

- Renderer-independent mesh, instance, transform, material, and visibility submission.
- Generation-checked CPU resource handles and geometry validation.
- World-space transform processing with inverse-transpose normal handling.
- Backend-owned per-frame upload resources.
- Visible geometry and cascaded-shadow submission.
- D3D12 upload-buffer lifetime tied to the matching frame fence; D3D11 dynamic write-discard uploads.
- Frame result and device-removal reporting.
- A separate engine sample using the public scene API.

Talvisota owns the game-side boundary:

- `ISceneRenderer` and validated game-owned scene snapshots.
- An optional `VantaSceneRenderer` adapter that translates snapshots to the public engine API without private backend headers.
- An exact VantaEngine dependency pin: `76f5d8ca907a3b928a6320b6b4290e8a55366db7`.
- A standalone Win32 renderer sample that exercises the game-owned adapter.

## CI and hardware verification

The Windows x64/MSVC adapter job in [Talvisota CI run 42507908980](https://github.com/ItsDropper/Talvisota/actions/runs/42507908980) successfully configured and built the pinned engine integration, passed the renderer and content contract tests, validated shipped assets, built and revalidated the deterministic content package, and captured five genuine D3D12 renderer viewpoints as diagnostic artifacts.

These hosted-runner results establish compilation, contract tests, CPU asset-pipeline validation, packaging, and successful sample capture. They do not establish that the same paths have been exercised on physical D3D11 and D3D12 devices.

**M2 remains open** until real-device smoke tests confirm transforms, materials, visibility, shadows, resize, and device-loss reporting on both backends. Do not treat a successful hosted CI run or screenshot artifact as a substitute for that gate.

## Ownership and dependency policy

- Talvisota owns missions, world composition, content definitions, and game presentation decisions.
- VantaEngine owns generic rendering, GPU resource management, platform abstractions, and generic model/texture processing.
- Game/domain code must not include D3D11/D3D12 implementation headers, submit backend commands directly, or own backend GPU resources.
- Pin VantaEngine to an exact reviewed commit; do not use a floating branch in production builds.
- Do not copy VantaEngine source into Talvisota or bypass a missing public API with private renderer coupling.
- Keep the engine's licence and notices intact.
- Test the adapter against the exact pinned revision in CI.
