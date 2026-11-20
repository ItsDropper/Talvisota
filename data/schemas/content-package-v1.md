# Content package v1

A package is a directory containing manifest-referenced source assets, a normalized
source catalog named `content.tmanifest`, and a UTF-8 tab-separated integrity inventory
named `package.tmanifest`.

## Encoding

- UTF-8, no byte-order mark.
- LF line endings.
- Tabs separate fields; records have no quoting or escaping.
- Paths use forward slashes and are relative to the package root.
- The packaged catalog is normalized into stable-ID order; dependency lists are sorted.
- Inventory asset records are ordered by deterministic dependency load order.

## Inventory grammar

The first line is:

```text
talvisota-content-package<TAB>1
```

The second line identifies and hashes the packaged source catalog:

```text
catalog<TAB>byte-count<TAB>sha256
```

Each following non-empty line is exactly eight fields:

```text
asset<TAB>stable-id<TAB>kind<TAB>relative-path<TAB>byte-count<TAB>sha256<TAB>source<TAB>licence
```

Supported kinds are `model`, `texture`, `audio`, `definition`, and `material`.
The byte count is an unsigned decimal integer. SHA-256 is 64 lowercase hexadecimal
characters and covers the complete file bytes. Source and licence/permission fields are
copied from the source catalog and must be non-empty and not `-`.

The inventory's asset count and total byte count exclude the catalog and inventory
metadata files; they describe the packaged source assets only.

## Validation requirements

A verifier must reject:

- Unknown inventory versions, malformed records, duplicate IDs, or duplicate paths.
- Missing, malformed, or hash-mismatched source catalogs.
- A catalog that does not parse as a content manifest or whose present assets, kinds,
  paths, provenance, or licence fields differ from the inventory.
- Broken required assets, dependency references, or typed world/material/authored
  definitions.
- Model, texture, WAV, Ogg Vorbis, or FLAC media that fails engine-backed decoding.
- Unsupported asset kinds, absolute paths, root names, parent traversal, or the reserved
  catalog/inventory paths as asset paths.
- Missing files, non-regular files, symbolic links, or paths resolving outside the package.
- File-size or SHA-256 mismatches.
- Any unindexed regular file or inventory entry without a corresponding file.

The package verifier reimports and decodes the packaged bytes; it does not rely solely on
the source tree's earlier validation. The inventory detects accidental corruption and
incomplete packaging. It is not signed and does not establish publisher identity, licence
ownership, or authenticity against an actor who can rewrite both the assets and inventory.

## Determinism

The package builder copies source asset bytes without transcoding. It serializes the
catalog in stable-ID order and writes asset inventory records in stable dependency order.
It includes no timestamps, absolute source paths, or machine-specific metadata. Directory
timestamps and filesystem allocation details are not part of the package format.
