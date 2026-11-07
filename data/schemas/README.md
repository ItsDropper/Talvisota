# Schemas

Versioned schemas for world, mission, faction, character, equipment, material, and
asset-manifest data. Schema changes require an explicit compatibility policy.

Implemented v1 contracts:

- [Content manifest v1](content-manifest-v1.md): stable IDs, dependency graph, provenance,
  root-contained paths, and required/optional assets.
- [Material definition v1](material-definition-v1.md): linear PBR factors, texture-slot
  semantics, and manifest dependency validation.
- [World definition v1](world-definition-v1.md): fixed metre-based right-handed coordinate
  convention, finite bounds, and an explicit present terrain-model dependency.
- [Authored definitions v1](authored-definitions-v1.md): strict mission, faction, character,
  and equipment records with manifest dependency checks.
- [Content package v1](content-package-v1.md): normalized source catalog, SHA-256 inventory,
  and semantic/integrity revalidation.

See [Compatibility policy](COMPATIBILITY.md) for fail-closed version handling and
non-destructive migration policy. No historical schema version is currently supported;
a migration executable will be introduced with a real v2 schema and a defined v1-to-v2
transformation rather than shipping an empty migration framework.
