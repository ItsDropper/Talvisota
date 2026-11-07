# Content package inventory v1

A package is a directory containing the exact manifest-referenced asset files and a
UTF-8 tab-separated inventory named `package.tmanifest`.

## Encoding

- UTF-8, no byte-order mark.
- LF line endings.
- Tabs separate fields; records have no quoting or escaping.
- Paths use forward slashes and are relative to the package root.
- Asset records are ordered by the source manifest's deterministic dependency load order.

## Grammar

The first non-empty line is:

```text
talvisota-content-package<TAB>1
```

Each following non-empty line is exactly eight fields:

```text
asset<TAB>stable-id<TAB>kind<TAB>relative-path<TAB>byte-count<TAB>sha256<TAB>source<TAB>licence
```

Supported kinds are `model`, `texture`, `audio`, `definition`, and `material`.
The byte count is an unsigned decimal integer. SHA-256 is 64 lowercase hexadecimal
characters and covers the complete packaged file bytes. Source and licence/permission
fields are copied from the source manifest and must be non-empty and not `-`.

## Validation requirements

A verifier must reject:

- Unknown inventory versions, malformed records, duplicate IDs, or duplicate paths.
- Unsupported asset kinds, absolute paths, root names, parent traversal, or the reserved
  inventory path as an asset path.
- Missing files, non-regular files, symbolic links, or paths resolving outside the package.
- File-size or SHA-256 mismatches.
- Any unindexed regular file or inventory entry without a corresponding file.

The inventory detects accidental corruption and incomplete packaging. It is not signed and
does not establish publisher identity, licence ownership, or authenticity against an actor
who can rewrite both the assets and inventory.

## Determinism

The package builder copies source bytes without transcoding. It writes records in stable
dependency order and includes no timestamps, absolute source paths, or machine-specific
metadata. Directory timestamps and filesystem allocation details are not part of the
package format.
