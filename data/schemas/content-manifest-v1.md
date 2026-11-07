# Content manifest v1

The runtime content registry reads UTF-8 tab-separated manifests. The format is intentionally small, deterministic, reviewable in source control, and independent of third-party parser dependencies.

## File layout

The first non-empty, non-comment line must be:

    talvisota-content-manifest<TAB>1

Each subsequent non-empty, non-comment line is one asset record with exactly eight tab-separated fields:

    asset<TAB><stable-id><TAB><kind><TAB><relative-path><TAB><required|optional><TAB><dependencies|-><TAB><source><TAB><license>

Example:

    talvisota-content-manifest<TAB>1
    asset<TAB>material.forest.pine<TAB>material<TAB>materials/forest/pine.json<TAB>required<TAB>-<TAB>project<TAB>project
    asset<TAB>model.forest.pine<TAB>model<TAB>models/forest/pine.glb<TAB>required<TAB>material.forest.pine<TAB>project<TAB>project
    asset<TAB>audio.wind.forest<TAB>audio<TAB>audio/ambience/forest-wind.ogg<TAB>optional<TAB>-<TAB>project<TAB>project

<TAB> above denotes one literal tab character. # begins a comment when it is the first non-whitespace character on a line. Blank lines are ignored.

## Field rules

- **Stable ID:** lowercase ASCII; must start with a lowercase letter or digit. Remaining characters may be lowercase letters, digits, ., _, -, or :. IDs are persistent identity; paths are not.
- **Kind:** model, texture, audio, definition, or material.
- **Relative path:** resolved from the content root, never from the process working directory. Absolute paths and parent-directory traversal are rejected. Canonicalized paths must remain inside the content root, including after symlink resolution.
- **Required:** required fails validation when the file is missing or is not a regular file. optional allows the file to be absent, but does not waive path, kind, provenance, or dependency validation.
- **Dependencies:** - for none, otherwise comma-separated stable IDs. Every dependency must exist. Duplicate dependency IDs and dependency cycles are errors. The registry produces a deterministic dependency-first load order.
- **Source and license:** both are mandatory. Use project for original project assets. External assets must record a traceable source and an explicit licence/permission identifier; do not treat an empty field as permission.

Supported extensions:

| Kind | Extensions |
|---|---|
| model | .gltf, .glb, .obj |
| texture | .png, .jpg, .jpeg, .tga, .dds, .ktx2 |
| audio | .wav, .ogg, .flac |
| definition | .json, .toml, .yaml, .yml |
| material | .tmat, .json, .toml, .yaml, .yml |

## Failure and publication semantics

The loader reports the manifest path, one-based manifest line where applicable, field, and actionable message. It validates the complete manifest before publishing a new registry. A failed initial load publishes nothing; a failed reload leaves the previously published registry unchanged. Optional missing files remain in the registry so callers can distinguish an absent optional asset from an unknown ID.

The registry validates manifest structure and file references. It does not yet parse world/mission/character schemas, decode media, or perform model/texture/audio conversion; those are subsequent M3 tasks.
