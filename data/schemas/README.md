# Schemas

Versioned schemas for world, mission, faction, character, equipment, material, and asset-manifest data. Schema changes require an explicit migration/compatibility policy.

Implemented and runtime-validated in M3:
- [Content manifest v1](content-manifest-v1.md): stable IDs, dependency graph, provenance, root-contained paths, and required/optional assets.
- [Material definition v1](material-definition-v1.md): linear PBR factors, texture-slot semantics, and manifest dependency validation.
- [World definition v1](world-definition-v1.md): fixed metre-based right-handed coordinate convention and finite world bounds.
- [Texture importer v1 capability note](texture-importer-v1.md): exact supported BMP decoding subset, pixel semantics, and safety limits.

Mission, faction, character, and equipment definition schemas and migration validation remain M3 work. The world schema defines metadata only; terrain, collision, vegetation, and runtime world loading are not implied.
