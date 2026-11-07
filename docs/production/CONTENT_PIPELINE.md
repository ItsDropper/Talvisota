# Content pipeline

## Separation of concerns

- `assets/` contains source and processed media: models, textures, materials, audio, and UI assets.
- `content/` contains authored game definitions that refer to assets by stable identifiers.
- `data/schemas/` contains versioned schemas used to validate authored definitions.
- `tools/asset_pipeline/` contains game-specific orchestration and packaging commands.
- Generic model import, image decoding, CPU asset validation, mip processing, and GPU resource ownership belong to VantaEngine.

Do not use filesystem paths as persistent gameplay identifiers. Content references use stable IDs resolved through the Talvisota manifest.

## Headless content validation

`talvisota_content_validate <manifest> <content-root>` validates the manifest, path containment, provenance, dependency graph, typed PBR materials, typed world definitions, and typed mission/faction/character/equipment definitions. It does not claim to decode media.

## Engine-backed media validation

On Windows x64/MSVC, configure with `TALVISOTA_ENABLE_VANTA_ENGINE=ON`. The optional `talvisota_asset_validate` command uses VantaEngine's `ModelAssetStore`, OBJ/glTF/GLB importers, and WIC texture decoder. It validates imported geometry and materials, decodes registered texture assets, and checks that model-referenced textures remain inside the content root, resolve to manifest texture assets, and are declared as model dependencies. This path is CPU-only and does not initialize a graphics device.

The pinned VantaEngine revision supports Wavefront OBJ, glTF 2.0/GLB static meshes and WIC-supported external texture images. OBJ `mtllib` sidecars must be registered as `.mtl` material assets and declared as model dependencies; MTL texture maps must also be registered texture assets and declared as model dependencies. Multiple `mtllib` declarations and multiple libraries on one declaration are supported; `usemtl` is resolved into named CPU materials and submesh ranges. Embedded glTF data-URI/GLB images remain unsupported and are rejected by the engine importer. Texture source support follows installed WIC codecs; the Talvisota manifest accepts BMP, PNG, JPEG, and TIFF variants for this path.

## Deterministic texture processing

The `talvisota_texture_process <source> <output.ttex> <srgb|linear>` command is available with the optional engine adapter. It decodes the source through VantaEngine WIC, builds a full CPU mip chain, filters sRGB RGB channels in linear light, averages alpha and linear data channels without gamma conversion, and writes a deterministic little-endian `TVTTEX01` container. The output contains a version, dimensions, RGBA8 format, colour-space tag, mip count, and pixel bytes; it contains no timestamps, absolute paths, or host-endian metadata. The engine-backed validator reads `.ttex` files with the strict VantaEngine container reader, including dimension/mip-chain/size/trailing-byte validation.

## Typed PBR materials

Material assets use the strict, versioned `.tmat` format documented in [material-definition-v1.md](../../data/schemas/material-definition-v1.md). The parser validates finite numeric values, physically based parameter ranges, stable material identity, manifest ownership, and texture-slot references. A referenced texture must be a texture-kind asset, be present as a regular file, and be listed as a dependency of the material.

Base-colour and emissive texture RGB are sRGB; normal and ORM textures are linear. ORM channels are R=occlusion, G=roughness, B=metallic. These are authoring/import contracts; renderer consumption remains subject to the pinned engine's material API.

## Typed world definitions

World metadata uses the strict, versioned `.tworld` format documented in [world-definition-v1.md](../../data/schemas/world-definition-v1.md). Version 1 fixes the world coordinate convention to metres, right-handed, +X east, +Y up, and +Z north. Bounds must be finite and strictly ordered on every axis. Optional sea level must fall within vertical bounds.

This is metadata validation only; terrain authoring, collision, vegetation placement, and runtime world loading remain later work.

## Typed mission, faction, character, and equipment definitions

The content library parses strict v1 `.tmission`, `.tfaction`, `.tcharacter`, and `.tequipment` records. Their required fields and reference types are documented in [authored-definitions-v1.md](../../data/schemas/authored-definitions-v1.md). The parser validates type/version headers, unique and known fields, required values, manifest identity/path ownership, expected referenced asset kinds, referenced file presence, and explicit manifest dependencies. Failed parsing leaves the caller's previous output unchanged. Generic JSON/TOML/YAML definitions fail closed until parsers are implemented.

## Packaging and provenance

On Windows x64/MSVC, `talvisota_content_package <manifest> <content-root> <package-dir>` validates typed definitions and engine-backed media, stages every present manifest asset, and writes a deterministic tab-separated inventory containing stable ID, kind, relative path, byte count, SHA-256, source, and licence/permission metadata. The package is published only after the staged directory passes full inventory and digest verification. `talvisota_content_package --verify <package-dir>` rechecks paths, file sizes, SHA-256 digests, and unindexed files. This is an integrity check, not a cryptographic signature or proof of asset ownership.

Every manifest record requires source and licence/permission metadata. Keep source media immutable; write processed outputs into a separate build directory. A clean content build must validate all shipped definitions and references before packaging. External asset rights must be documented before an asset is committed. Optional manifest assets may be absent; when present, they are validated with the same strict rules as required assets. Engine-backed audio validation decodes bounded RIFF/WAVE PCM (8/16/24/32-bit) and IEEE-float32 streams, checks chunk bounds, sample rate/channel/alignment limits, complete frames, and finite float samples. OGG and FLAC remain fail-closed until dedicated decoders are integrated.
