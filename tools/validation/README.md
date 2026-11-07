# Validation tools

Validate manifests, asset references, schema versions, and packaging invariants. Identify exact files, one-based manifest lines, fields, and actionable failures.

The runtime ContentRegistry currently validates manifest structure, stable IDs, asset-kind extensions, path containment, required/optional file presence, provenance fields, dependency references, and cycles. Dedicated command-line packaging validation and domain-schema validation remain to be implemented.
