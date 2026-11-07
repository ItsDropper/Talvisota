# Asset pipeline tools

## Headless content validation

Build the default project and run:

```text
talvisota_content_validate <manifest> <content-root>
```

This validates stable IDs, provenance, required files, dependency edges, typed materials, worlds, missions, factions, characters, and equipment. It does not decode media or initialize a graphics device.

## Engine-backed media validation and texture processing

On Windows x64 with MSVC, configure using `-DTALVISOTA_ENABLE_VANTA_ENGINE=ON`. This enables:

```text
talvisota_asset_validate <manifest> <content-root>
talvisota_texture_process <source-image> <output.ttex> <srgb|linear>
```

The validator imports OBJ/glTF/GLB models through VantaEngine, decodes registered texture sources through WIC, validates processed `.ttex` containers with the engine reader, and checks OBJ MTL sidecars and model texture references against manifest dependencies. It is CPU-only; it does not initialize a D3D device.

The texture processor uses VantaEngine WIC decoding and the engine's deterministic CPU mip processor. The `.ttex` output uses the versioned little-endian `TVTTEX01` format and omits timestamps, absolute paths, and host-specific metadata. Use sRGB for colour maps and linear for normal/ORM/data maps.
