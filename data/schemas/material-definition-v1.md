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

This schema describes authored material intent. It does not claim the current renderer adapter uploads textures or implements alpha blending, normal mapping, or the complete material model yet. Renderer support remains a separate, explicitly gated capability.
