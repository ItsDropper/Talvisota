# Game content

Authored game definitions, not engine code or raw media. Use stable identifiers and schema validation; asset references resolve through the content registry.

Every shipped asset must be represented in a versioned manifest with a stable ID, kind, content-root-relative path, required/optional policy, dependencies, source, and licence/permission basis. See [manifest v1](../data/schemas/content-manifest-v1.md). Do not use file paths as persistent identity.
