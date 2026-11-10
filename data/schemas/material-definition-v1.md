# Material definition v1

Material source files use the `.tmat` extension and UTF-8 text. The first non-empty, non-comment line is:

    talvisota-material<TAB>1

Every remaining record is one unique `key = value` assignment. Blank lines and lines whose first non-whitespace character is `#` are ignored. Unknown fields, duplicate fields, malformed values, non-finite numbers, and missing required fields are errors. A definition is published only after every check succeeds.

## Required fields

| Field | Type | Meaning |
|---|---|---|
| `id` | stable ID | Must identify the matching `material` record in the content manifest, whose path must be this file. |
| `base_color` | three floats | Linear RGB reflectance, each component in [0, 1]. |
| `roughness` | float | Perceptual roughness in [0, 1]. |
| `metallic` | float | Metallic workflow factor in [0, 1]. |

## Optional fields

| Field | Type | Default |
|---|---|---|
| `emissive` | three non-negative linear RGB floats | `0, 0, 0` |
| `normal_scale` | non-negative float | `1` |
| `occlusion_strength` | float in [0, 1] | `1` |
| `alpha_mode` | `opaque`, `mask`, or `blend` | `opaque` |
| `alpha_cutoff` | float in [0, 1] | `0.5` |
| `double_sided` | `true` or `false` | `false` |
| `base_color_texture` | texture stable ID | none |
| `normal_texture` | texture stable ID | none |
| `orm_texture` | texture stable ID | none |
| `emissive_texture` | texture stable ID | none |

Texture references must resolve to manifest assets of kind `texture`. The texture slots define colour-space interpretation: base-colour and emissive RGB data are sRGB; normal and packed occlusion/roughness/metallic (ORM) data are linear. ORM channel packing is R=occlusion, G=roughness, B=metallic. Importers must preserve these semantics; they must not infer colour space from filenames.

## Runtime renderer support

The current Talvisota scene bridge supports the metallic-roughness PBR core:

- Linear `base_color`, scalar `roughness`, and scalar `metallic`.
- Base-colour and tangent-space normal textures.
- A packed ORM texture, bound to all three engine texture slots. The channel contract is R=occlusion, G=roughness, B=metallic.
- Texture paths are resolved against the validated content registry and must resolve to regular files before scene submission.

The bridge deliberately rejects non-default emissive, normal-scale, occlusion-strength, alpha-mode, alpha-cutoff, and double-sided settings. The parser retains these authored fields for schema compatibility, but the active renderer does not yet implement them. This is a hard error rather than silently dropping material intent. In particular, `alpha_mode = blend` is not treated as working transparency.

Texture colour-space handling remains an engine-side concern: base-colour/emissive maps must be sampled as sRGB, while normal and ORM data must be sampled as linear values. A material definition being valid does not by itself guarantee that a backend has implemented the required texture format and shader behavior.
