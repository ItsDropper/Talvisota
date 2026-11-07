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
