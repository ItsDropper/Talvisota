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

The pinned VantaEngine revision supports Wavefront OBJ with multiple MTL sidecars and material/submesh assignment, glTF 2.0/GLB static meshes, WIC-supported external texture images, base64/percent-encoded glTF data-URI images, and GLB buffer-view images. Embedded image payloads remain owned by imported CPU materials; the importer does not extract mutable sidecar files. OBJ `mtllib` sidecars must be registered as `.mtl` material assets and declared as model dependencies; MTL texture maps must also be registered texture assets and declared as model dependencies. Multiple `mtllib` declarations and multiple libraries on one declaration are supported; `usemtl` is resolved into named CPU materials and submesh ranges. Embedded glTF image payloads are decoded through WIC during engine-backed validation. Texture source support follows installed WIC codecs; the Talvisota manifest accepts BMP, PNG, JPEG, and TIFF variants for this path.

## Deterministic texture processing

The `talvisota_texture_process <source> <output.ttex> <srgb|linear>` command is available with the optional engine adapter. It decodes the source through VantaEngine WIC, builds a full CPU mip chain, filters sRGB RGB channels in linear light, averages alpha and linear data channels without gamma conversion, and writes a deterministic little-endian `TVTTEX01` container. The output contains a version, dimensions, RGBA8 format, colour-space tag, mip count, and pixel bytes; it contains no timestamps, absolute paths, or host-endian metadata. The engine-backed validator reads `.ttex` files with the strict VantaEngine container reader, including dimension/mip-chain/size/trailing-byte validation.

## Typed PBR materials

Material assets use the strict, versioned `.tmat` format documented in [material-definition-v1.md](../../data/schemas/material-definition-v1.md). The parser validates finite numeric values, physically based parameter ranges, stable material identity, manifest ownership, and texture-slot references. A referenced texture must be a texture-kind asset, be present as a regular file, and be listed as a dependency of the material.

Base-colour and emissive texture RGB are sRGB; normal and ORM textures are linear. ORM channels are R=occlusion, G=roughness, B=metallic. These are authoring/import contracts; renderer consumption remains subject to the pinned engine's material API.

## Typed world definitions

World metadata uses the strict, versioned `.tworld` format documented in [world-definition-v1.md](../../data/schemas/world-definition-v1.md). Version 1 fixes the world coordinate convention to metres, right-handed, +X east, +Y up, and +Z north. Bounds must be finite and strictly ordered on every axis. Optional sea level must fall within vertical bounds. Optional `terrain_model_id` must resolve to a present model asset and be an explicit dependency of the world manifest record.

The repository includes an original 64-metre terrain validation region with OBJ/MTL geometry, a snow albedo texture, a `.tmat` material, and a `.tworld` reference. The material sidecar is outside the model directory and is resolved through a contained parent-relative path. This exercises content import and packaging; it is not the final photorealistic terrain art or runtime world loader.

## Typed mission, faction, character, and equipment definitions

The content library parses strict v1 `.tmission`, `.tfaction`, `.tcharacter`, and `.tequipment` records. Their required fields and reference types are documented in [authored-definitions-v1.md](../../data/schemas/authored-definitions-v1.md). The parser validates type/version headers, unique and known fields, required values, manifest identity/path ownership, expected referenced asset kinds, referenced file presence, and explicit manifest dependencies. Failed parsing leaves the caller's previous output unchanged. Generic JSON/TOML/YAML definitions fail closed until parsers are implemented.

## Packaging and provenance

On Windows x64/MSVC, `talvisota_content_package <manifest> <content-root> <package-dir>` validates typed definitions and engine-backed media, stages every present manifest asset, writes a normalized `content.tmanifest`, and writes a deterministic `package.tmanifest` containing the catalog digest plus stable ID, kind, relative path, byte count, SHA-256, source, and licence/permission metadata. Before replacing the destination, the staged package is reloaded from its own catalog; dependencies, typed definitions, model/texture/audio decoders, path containment, exact inventory coverage, byte counts, and hashes are revalidated. `talvisota_content_package --verify <package-dir>` performs the same full semantic and integrity verification. The inventory is not a cryptographic signature or proof of asset ownership.

Every manifest record requires source and licence/permission metadata. Keep source media immutable; write processed outputs into a separate build directory. A clean content build must validate all shipped definitions and references before packaging. External asset rights must be documented before an asset is committed. Optional manifest assets may be absent; when present, they are validated with the same strict rules as required assets. Engine-backed audio validation fully decodes bounded RIFF/WAVE PCM (8/16/24/32-bit), IEEE-float32, Ogg Vorbis, and FLAC streams. WAV chunk bounds, sample rate/channel/alignment limits, complete frames, and finite samples are checked. OGG and FLAC use vendored stb_vorbis and dr_flac decoders, run in bounded PCM chunks, and enforce encoded-size, duration, channel-count, and sample-rate limits.
