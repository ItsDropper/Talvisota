# Asset pipeline tools

## Headless content validation

Build the default project and run:

```text
talvisota_content_validate <manifest> <content-root>
```

This validates stable IDs, provenance, required files, dependency edges, typed materials,
worlds, missions, factions, characters, and equipment. It does not decode media or
initialize a graphics device.

## Engine-backed media validation and texture processing

On Windows x64 with MSVC, configure using `-DTALVISOTA_ENABLE_VANTA_ENGINE=ON`. This
enables:

```text
talvisota_asset_validate <manifest> <content-root>
talvisota_texture_process <source-image> <output.ttex> <srgb|linear|normal>
talvisota_content_package <manifest> <content-root> <package-dir>
talvisota_content_package --verify <package-dir>
```

The validator imports OBJ/glTF/GLB models through the pinned VantaEngine revision,
including base64/percent-encoded glTF data-URI images and GLB buffer-view images. Embedded
payloads remain owned by CPU materials and are decoded through WIC without sidecar
extraction. External image sources are decoded through WIC; processed `.ttex` containers
are read with the engine's strict versioned reader. OBJ material libraries and texture
references must resolve inside the content root and be declared as manifest dependencies.

Audio validation fully decodes bounded PCM/IEEE-float WAV, Ogg Vorbis, and FLAC streams.
Ogg Vorbis and FLAC use the vendored stb_vorbis and dr_flac decoders. Encoded size,
duration, channel count, sample rate, complete frame count, and finite decoded samples are
checked. Validation is CPU-only and does not initialize D3D.

The texture processor uses WIC decoding and deterministic CPU mip generation. The
little-endian `TVTTEX01` output omits timestamps, absolute paths, and host-specific
metadata. Use `srgb` for colour maps, `linear` for ORM/roughness/AO and other data
maps, and `normal` for tangent-space normal maps. Normal mode normalizes source
vectors, averages and renormalizes each mip level, and filters alpha linearly.

## Procedural environment PBR maps

The default build runs `talvisota_environment_texture_gen` to create 41 deterministic
1024x1024 BMP maps: albedo, tangent-space normal, roughness, and AO for ten material
families, plus packed snow ORM. The generator checks periodic continuity on both UV
axes for all ten source fields before writing. Packed-snow compression uses independent
integer U/V periods, preserving its anisotropic detail without a wrap seam.
`Talvisota.Environment.PbrMaps` runs the generator's `--verify` path to validate all
41 BMP headers, dimensions, byte counts, pixel variation, and the snow/ice relief
ranges. Generated maps are build products and are recreated before content validation
and packaging.

## Deterministic packages

The package tool validates source definitions and media, stages every present manifest
asset, writes a normalized `content.tmanifest`, and creates `package.tmanifest` with
the catalog digest plus per-asset SHA-256, size, path, source, and licence records.
Before publishing, it reloads the staged catalog, validates dependency and typed
definitions, reimports/decodes packaged media, and checks exact catalog/inventory/file
correspondence. `--verify` performs the same semantic and integrity revalidation.

See [content-package-v1.md](../../data/schemas/content-package-v1.md) for the format
contract and [ASSET_RIGHTS_AUDIT.md](../../docs/production/ASSET_RIGHTS_AUDIT.md) for
the current shipped-media provenance and vendored-code licence audit.
