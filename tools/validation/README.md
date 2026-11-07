# Validation tools

Validate manifests, asset references, schema versions, and packaging invariants. Identify exact files, one-based manifest lines, fields, and actionable failures.

The runtime ContentRegistry currently validates manifest structure, stable IDs, asset-kind extensions, path containment, required/optional file presence, provenance fields, dependency references, and cycles. Dedicated command-line packaging validation and domain-schema validation remain to be implemented.

## Command-line validator

Build target: `talvisota_content_validate`.

Usage:

```text
talvisota_content_validate <manifest-path> <content-root>
talvisota_content_validate --help
```

The command returns 0 only when the complete manifest is valid, 1 for invalid content or validation I/O failures, and 2 for invalid command-line usage. Diagnostics include the manifest path, one-based line, field, and reason. A successful run prints the validated asset count and deterministic dependency-first load order.
