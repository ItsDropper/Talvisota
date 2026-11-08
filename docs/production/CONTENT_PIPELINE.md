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

The `talvisota_texture_process <source> <output.ttex> <srgb|linear|normal>` command is available with the optional engine adapter. It decodes the source through VantaEngine WIC and writes a deterministic little-endian `TVTTEX01` container. `srgb` filters colour RGB in linear light; `linear` box-filters non-colour data without gamma conversion; `normal` is for tangent-space normal maps, normalizes decoded source vectors, averages unit vectors and renormalizes every mip, and filters alpha linearly. Normal maps are serialized with linear colour-space metadata. The container contains a version, dimensions, RGBA8 format, colour-space tag, mip count, and pixel bytes; it contains no timestamps, absolute paths, or host-endian metadata. The engine-backed validator reads `.ttex` files with the strict VantaEngine container reader, including dimension/mip-chain/size/trailing-byte validation.

## Typed PBR materials

Material assets use the strict, versioned `.tmat` format documented in [material-definition-v1.md](../../data/schemas/material-definition-v1.md). The parser validates finite numeric values, physically based parameter ranges, stable material identity, manifest ownership, and texture-slot references. A referenced texture must be a texture-kind asset, be present as a regular file, and be listed as a dependency of the material.

Base-colour and emissive texture RGB are sRGB; normal and ORM textures are linear. ORM channels are R=occlusion, G=roughness, B=metallic. These are authoring/import contracts; renderer consumption remains subject to the pinned engine's material API.

## Typed world definitions

World metadata uses the strict, versioned `.tworld` format documented in [world-definition-v1.md](../../data/schemas/world-definition-v1.md). Version 1 fixes the world coordinate convention to metres, right-handed, +X east, +Y up, and +Z north. Bounds must be finite and strictly ordered on every axis. Optional sea level must fall within vertical bounds. Optional `terrain_model_id` must resolve to a present model asset and be an explicit dependency of the world manifest record.

The repository includes a 64-metre Central Finland region with an OBJ/MTL terrain mesh, a 17x17 `.tregion` heightfield sampled from the same OBJ vertex grid, a snow albedo texture, and a typed snow material. The game-owned world runtime bilinearly samples elevation and surface normals from this grid; tests compare all 289 grid samples to the source mesh vertices. The same region composes eight original Norway spruce models and three snow-capped boulders through stable asset IDs, with independent authored collision volumes. The spruce source contains 13,320 positions, 13,320 cylindrical UV coordinates, and 19,992 triangles. The boulder is a deterministic four-level subdivided icosphere with multi-frequency displacement, 2,562 positions, 2,562 spherical UV coordinates, and 5,120 triangles, partitioned into rock and snow material groups. Engine-backed validation imports the models and MTL sidecars, and deterministic packaging includes and revalidates the complete content set. These Central Finland validation assets remain deliberately simple; the Sakkola environment below carries the current generated PBR material set and engine normal-map path.

The first historically grounded Karelian environment is Sakkola Kirkonkylä and the north shore of Suvanto. Its current authored region spans 4,096x4,096 metres and uses a 177x177 heightfield at approximately 23.2727-metre spacing, matched vertex-for-vertex to the terrain OBJ grid. It contains 3,094 tree placements across five species (960 Norway spruce, 947 Scots pine, 497 silver birch, 351 aspen, and 339 alder), 32 boulder placements, 35 structure placements (one church, 12 farmhouses, 11 barns, and 11 saunas), and 803 prop-category placements. The prop category includes 420 common-juniper placements, 300 winter-brush clumps, 28 fallen logs, 26 stumps, 12 fence sections, 12 reed placements, two woodpiles, a well, the frozen Suvanto surface, and the packed rural track. The forest is authored as a mixed stand with species-specific silhouettes and scale/yaw variation rather than a single repeated spruce mesh. The source reference is the National Land Survey of Finland's pre-war 1:20,000 topographic sheet 404206_SAKKOLA.tif; the current terrain elevations and shoreline are a hand-authored blockout informed by the historical setting, not a georeferenced extraction of that raster. See [Sakkola environment references](SACKKOLA_ENVIRONMENT_REFERENCE.md) for source notes and remaining historical/geospatial work.

The environment generator produces 41 separate 1024x1024 BMP maps: albedo, tangent-space normal, roughness, and ambient occlusion for ten material families (bark, pine needles, birch bark, aspen bark, alder bark, granite, snow, packed snow, ice, and reeds), plus a packed snow ORM map. `talvisota_environment_texture_gen` produces material-specific multi-scale maps from original source code and verifies periodic continuity at both UV boundaries for all ten procedural fields before writing. The packed-snow compression field uses independent integer U/V periods, so its anisotropic scale remains tileable. The `Talvisota.Environment.PbrMaps` CTest also verifies all 41 generated BMP files. Output is generated under `assets/textures/environment/` during the normal CMake build and before content validation/packaging. The generated BMPs are intentionally ignored by Git and recreated deterministically. This is a substantive procedural PBR first pass, not a claim that scanned or hand-painted final art has been completed.

## Typed mission, faction, character, and equipment definitions

The content library parses strict v1 `.tmission`, `.tfaction`, `.tcharacter`, and `.tequipment` records. Their required fields and reference types are documented in [authored-definitions-v1.md](../../data/schemas/authored-definitions-v1.md). The parser validates type/version headers, unique and known fields, required values, manifest identity/path ownership, expected referenced asset kinds, referenced file presence, and explicit manifest dependencies. Failed parsing leaves the caller's previous output unchanged. Generic JSON/TOML/YAML definitions fail closed until parsers are implemented.

## Packaging and provenance

On Windows x64/MSVC, `talvisota_content_package <manifest> <content-root> <package-dir>` validates typed definitions and engine-backed media, stages every present manifest asset, writes a normalized `content.tmanifest`, and writes a deterministic `package.tmanifest` containing the catalog digest plus stable ID, kind, relative path, byte count, SHA-256, source, and licence/permission metadata. Before replacing the destination, the staged package is reloaded from its own catalog; dependencies, typed definitions, model/texture/audio decoders, path containment, exact inventory coverage, byte counts, and hashes are revalidated. `talvisota_content_package --verify <package-dir>` performs the same full semantic and integrity verification. The inventory is not a cryptographic signature or proof of asset ownership.

Every manifest record requires source and licence/permission metadata. Keep source media immutable; write processed outputs into a separate build directory. A clean content build must validate all shipped definitions and references before packaging. External asset rights must be documented before an asset is committed. Optional manifest assets may be absent; when present, they are validated with the same strict rules as required assets. Engine-backed audio validation fully decodes bounded RIFF/WAVE PCM (8/16/24/32-bit), IEEE-float32, Ogg Vorbis, and FLAC streams. WAV chunk bounds, sample rate/channel/alignment limits, complete frames, and finite samples are checked. OGG and FLAC use vendored stb_vorbis and dr_flac decoders, run in bounded PCM chunks, and enforce encoded-size, duration, channel-count, and sample-rate limits.


## Renderer capture diagnostics

The Windows MSVC renderer-adapter CI job renders five genuine D3D12 Sakkola viewpoints for 30 frames each. Its `renderer.log` artifact records the submitted scene placement/draw/unique-mesh/material counts and per-view average/p95 wall-frame time plus average/p95 `Render()` CPU-call time. Wall-frame timing includes the sample's 16 ms pacing delay; `Render()` timing is CPU-observed call duration and may include driver/present blocking. Neither value is a GPU timestamp measurement. CI runner numbers are useful for regressions on the same runner class only; they do not establish target-PC performance or replace real-device D3D11/D3D12 profiling and visual review.
