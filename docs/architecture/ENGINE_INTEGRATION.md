# VantaEngine integration boundary

## Current status

VantaEngine is a separate C++23 project. Its reusable runtime target exists, but its current D3D11 and D3D12 renderer entry points still accept sample-camera parameters and construct a built-in procedural sample world during initialization.

That is not a sufficient game integration contract. Talvisota must not couple itself to those internals or treat the engine sample as the game's world.

## Required engine contract before rendering integration

The engine-side public API needs to provide:

- A renderer-independent scene submission contract for meshes, instances, transforms, materials, and visibility.
- Explicit GPU resource handles with generation/lifetime rules and safe destruction.
- A defined upload path from CPU asset data to renderer-owned GPU resources.
- Per-instance transforms in both colour and shadow passes.
- A documented frame lifecycle and synchronization contract.
- Resize/device-loss/error reporting that does not leak backend implementation details.
- A path for a game to own the main loop and pass camera/frame data to the renderer.
- Backend parity for the supported public feature set.

Talvisota will then implement a narrow adapter in its presentation/engine integration layer. Domain code will never depend directly on D3D11 or D3D12.

## Dependency policy

- Pin VantaEngine to a reviewed commit when the integration is introduced.
- Do not track a floating branch in a production build.
- Do not copy VantaEngine source files into this repository.
- Do not include private renderer headers from game code.
- Keep the engine's own licence and notices intact.
- Test the adapter against the exact pinned engine revision in CI.

The engine integration is deliberately not faked in the initial core foundation.
