# World definition v1

World source files use the `.tworld` extension and UTF-8 text. The first non-empty, non-comment line is:

    talvisota-world<TAB>1

Subsequent records use unique `key = value` assignments. Blank lines and lines whose first non-whitespace character is `#` are ignored. Unknown or duplicate fields, missing required fields, non-finite numbers, unsupported coordinate conventions, and invalid bounds are errors.

## Required fields

| Field | Value | Meaning |
|---|---|---|
| `id` | stable ID | Must identify the matching `definition` record in the manifest, whose path must be this file. |
| `name` | non-empty text | Human-readable world name. |
| `unit` | `meter` | All coordinates and bounds are metres. |
| `up_axis` | `y` | Positive Y is up. |
| `north_axis` | `+z` | Positive Z is north; positive X is east. |
| `handedness` | `right` | Right-handed world coordinate system. |
| `bounds_min` | three finite floats | Minimum X, Y, Z bounds. |
| `bounds_max` | three finite floats | Maximum X, Y, Z bounds; each component must exceed the corresponding minimum. |

## Optional fields

| Field | Type | Default |
|---|---|---|
| `sea_level` | finite float in metres | Not defined; when supplied it must lie within the vertical world bounds. |

The coordinate convention is fixed for v1 rather than configurable per world. This prevents silent axis conversion and unit-scale mismatches between authored terrain, meshes, collision, camera movement, and environmental simulation. A future incompatible convention requires a new schema version and explicit migration.

This schema defines world identity, coordinate space, and bounds only. It does not claim that terrain, vegetation, collision, streaming, or renderer-backed world loading is implemented.
