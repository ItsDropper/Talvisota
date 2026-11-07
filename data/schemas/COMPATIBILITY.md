# Content schema compatibility policy

## Current policy

- The manifest and authored definition formats are explicitly versioned.
- Readers accept only the versions they implement. An unknown version or unknown field is an error, not a warning or silent fallback.
- A parser validates the complete definition before publishing output. Failed loads preserve the caller's previous object.
- Stable IDs are independent of source paths. Moving a file requires changing only its manifest path, not its ID or references.
- References must resolve to an asset of the expected kind and be declared in the owning record's dependency list.
- The source files are authoritative. Runtime processing never rewrites source content.

## Migration rules

No automatic in-place migration is performed. When a future schema version is introduced, its migration must be a separate, deterministic tool that:

1. Reads a specifically supported source version.
2. Writes to a separate staging/output tree; it never edits the source tree.
3. Preserves stable IDs, provenance, and dependency semantics unless an explicit reviewed migration says otherwise.
4. Emits a report for every transformed file and every field it cannot map.
5. Runs the current validators on the complete migrated tree.
6. Publishes the new tree only after all validation succeeds.

Unsupported historical versions remain rejected until a tested migration exists. Do not claim backward compatibility merely because a parser ignores fields.
