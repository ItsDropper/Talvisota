# Shipped asset rights and provenance audit

**Audit scope:** all media and authored content currently registered by `content/catalog.tmanifest`, plus vendored decoder/parser code used by the asset pipeline.

## Registered source assets

| Asset ID | Source file | Origin | Rights basis |
|---|---|---|---|
| `world.central_finland` | `content/worlds/central-finland.tworld` | Original Talvisota project metadata | Project-owned |
| `material.environment.snow` | `content/materials/snow.tmat` | Original Talvisota project material definition | Project-owned |
| `model.terrain.central_finland` | `assets/models/terrain/central-finland.obj` | Original deterministic terrain mesh authored for this repository | Project-owned |
| `material.terrain.snow_mtl` | `assets/models/terrain/snow.mtl` | Original Talvisota material sidecar | Project-owned |
| `texture.environment.snow_albedo` | `assets/textures/snow-albedo.bmp` | Original small authored validation texture | Project-owned |

Every registered record uses `source=project` and `license=project`. The model, material sidecar, texture, and definitions were authored for this repository; no third-party media, stock imagery, scanned photographs, sound recordings, or externally licensed game assets are currently shipped.

The snow albedo image is a small pipeline-validation texture, not final photorealistic terrain artwork. The terrain mesh is a representative importer/package fixture and makes no claim about final game-world fidelity.

## Third-party code used by the pipeline

| Component | Pinned/source version | License basis | Use |
|---|---|---|---|
| cgltf (VantaEngine dependency) | 1.15, vendored in the pinned VantaEngine revision | MIT; notice is included in `engine/third_party/cgltf.h` | glTF 2.0/GLB parsing |
| dr_flac | 0.13.4 | Public domain or MIT-0, as stated in `third_party/dr_flac.h` | FLAC decoding for offline validation |
| stb_vorbis | 1.22 | Public domain, as stated in `third_party/stb_vorbis.c` | Ogg Vorbis decoding for offline validation |

The vendored decoder source files retain their upstream license notices. Their versions and license basis are summarized in `third_party/README.md`.

## Audit result

- All currently registered media and authored definitions have explicit manifest provenance.
- No externally sourced media asset is currently shipped, so there are no outstanding third-party media permission requests for the current manifest.
- Third-party code dependencies have their upstream license notices retained and are identified above.
- A new external asset must not be committed until its source, exact licence/permission basis, attribution requirements, and redistribution rights are reviewed and recorded in the manifest.

This audit is scoped to the current repository tree and manifest. It must be repeated whenever new media or third-party code is added.
