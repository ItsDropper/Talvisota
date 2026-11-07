# Content runtime

Owns manifest loading, schema validation, version compatibility, and stable-ID resolution. Treat external content as untrusted input.

The first runtime contract is ContentRegistry: it loads the versioned tab-separated manifest described in [content-manifest-v1.md](../../../data/schemas/content-manifest-v1.md), validates stable IDs, supported kind/extension pairs, root-contained paths, required/optional files, provenance, and dependency references, then publishes an immutable-by-interface registry with deterministic dependency-first load order. Failed loads do not replace an existing registry.

The content library also exposes typed material/world parsers and a bounded BMP texture decoder. The manifest registry itself does not decode media; PNG/JPEG/TGA/DDS/KTX2 decoding and model import remain unsupported. Mission, faction, character, and equipment definitions are not yet implemented.
