# Schemas

Versioned schemas for world, mission, faction, character, equipment, material, and asset-manifest data. Schema changes require an explicit migration/compatibility policy.

Implemented and runtime-validated in M3:
- [Content manifest v1](content-manifest-v1.md): stable IDs, dependency graph, provenance, root-contained paths, and required/optional assets.
- [Material definition v1](material-definition-v1.md): linear PBR factors, texture-slot semantics, and manifest dependency validation.
- [World definition v1](world-definition-v1.md): fixed metre-based right-handed coordinate convention and finite world bounds.

Mission, faction, character, and equipment definition schemas and migration validation remain M3 work. The world schema defines metadata only; terrain, collision, vegetation, and runtime world loading are not implied.

- [Authored definition formats v1](authored-definitions-v1.md): strict mission, faction, character, and equipment records with manifest dependency checks.
- [Compatibility policy](COMPATIBILITY.md): fail-closed version handling and staged, non-destructive migrations.
