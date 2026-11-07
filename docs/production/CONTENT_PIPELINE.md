# Content pipeline

## Separation of concerns

- `assets/` contains binary source and processed media: models, textures, materials, audio, and UI assets.
- `content/` contains authored game definitions that refer to assets by stable identifiers.
- `data/schemas/` contains versioned schemas used to validate authored definitions.
- `data/localization/` contains locale data, not hard-coded UI strings.
- `tools/asset_pipeline/` contains import, validation, conversion, and packaging tools.

Do not use filesystem paths as persistent gameplay identifiers. Content references should use stable IDs resolved through a manifest or asset registry.

## Validation requirements

Before content can ship, validation should check:

- Unique stable IDs and valid references.
- Schema and content-version compatibility.
- Missing files and case-sensitive path mismatches.
- Unsupported formats and invalid dimensions.
- Texture colour-space and channel conventions.
- Model scale, coordinate-system, and material consistency.
- Audio sample rate, channel count, and loudness constraints.
- Localization key completeness and fallback availability.
- Duplicate or unused assets where safe to detect.

Validation must fail with actionable file paths and field names. Do not silently substitute missing required content.

## Runtime loading

Runtime content loading must distinguish required and optional assets, report failures with context, and avoid publishing partially initialized resources. GPU resource lifetime remains owned by the engine adapter/renderer, not by mission definitions.


## Typed PBR material definitions

Material assets use the strict, versioned `.tmat` format documented in [material-definition-v1.md](../../data/schemas/material-definition-v1.md). The runtime parser validates finite numeric values, physically based parameter ranges, stable material identity, manifest ownership, and texture-slot references. A referenced texture must be a texture-kind asset, be present as a regular file, and be listed as a dependency of the material so deterministic load order remains meaningful.

The content validator parses every material record after validating the manifest. The shipped `content/catalog.tmanifest` is a real, minimal content catalog containing the initial snow surface material. It is a schema and validation fixture, not a claim of final visual quality or renderer texture support.

Colour-space conventions are explicit: base-colour and emissive texture RGB are sRGB; normal and ORM textures are linear. ORM channels are R=occlusion, G=roughness, B=metallic. These are authoring/import contracts; the current renderer adapter does not yet consume these texture slots.


## Typed world definitions

World metadata uses the strict, versioned `.tworld` format documented in [world-definition-v1.md](../../data/schemas/world-definition-v1.md). Version 1 fixes the world coordinate convention to metres, right-handed, +X east, +Y up, and +Z north. Bounds must be finite and strictly ordered on every axis. Optional sea level must fall within the vertical bounds.

The validator parses every manifest definition whose extension is `.tworld`. This is metadata validation only; terrain authoring, collision, vegetation placement, and runtime world loading remain later work.

## Texture source decoding (first supported format)

The content library exposes DecodeBmp and LoadBmp in TextureImporter.hpp. The implemented subset is Windows BMP with a BITMAPINFOHEADER-compatible DIB header, one plane, 24-bit BGR or 32-bit BGRX pixels, and BI_RGB (uncompressed) storage. Both positive-height bottom-up and negative-height top-down images are normalized to top-left-origin, tightly packed RGBA8. BI_RGB's fourth byte is reserved rather than a reliable alpha channel, so decoded pixels are opaque. Pixel RGB bytes remain sRGB-encoded; decoding does not perform a transfer-function conversion.

The decoder validates the signature, header and pixel offsets, dimensions, row padding, declared file/image sizes, source bounds, and decoded allocation size before publishing output. It preserves the caller's previous image on failure. Source reads are capped at 1 GiB and decoded RGBA8 output at 512 MiB. This is intentionally a narrow, tested decoder; manifest-recognized PNG/JPEG/TGA/DDS/KTX2 files are not yet decoded. Processed texture serialization, mip generation, and renderer upload remain future M3/M2 work.
