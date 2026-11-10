# Model assets

Talvisota stores model sources in the manifest and delegates generic geometry import to VantaEngine. The pinned engine provides CPU import paths for Wavefront OBJ/MTL and glTF 2.0/GLB static meshes, plus named MTL material extraction, multiple material-library support, per-material submesh assignment, and validation through `ModelAssetStore`.

Use metres and Talvisota's right-handed coordinate convention (+X east, +Y up, +Z north). Record origin/pivot, LODs, collision intent, and material dependencies. OBJ has no universal unit declaration; author OBJ coordinates in metres unless an explicit import conversion is added to the asset metadata. glTF uses its defined metre convention.

The engine-backed `talvisota_asset_validate` command imports registered models without creating a graphics device. External material textures must resolve inside the content root, be registered as texture assets, and be listed in the model's manifest dependencies. glTF data-URI images and GLB buffer-view images are retained as owned encoded payloads in CPU materials and decoded by the engine-backed validator without creating sidecar files.


## Initial Central Finland environment set

`environment/spruce.obj` is an original metre-scale Norway spruce with separate bark, needle, and snow material groups, 13,320 positions, 13,320 cylindrical UV coordinates, and 19,992 triangles. `environment/boulder.obj` is an original displaced, irregular snow-capped granite boulder with rock and snow material groups, 2,562 positions, spherical UV coordinates, and 5,120 triangles. Its deterministic multi-frequency displacement adds broad silhouette breakup and smaller surface irregularity. Both use registered MTL sidecars and stable IDs in `content/catalog.tmanifest`. UVs establish a usable texture-coordinate basis, but these meshes still do not have authored normal maps, texture-backed PBR materials, or renderer-verified shading; they are higher-detail source geometry, not finished photorealistic production assets.

The Sakkola runtime now consumes the Blender-generated GLB assets from
`tools/blender_asset_pipeline`. The Scots pine generator exports four
independent GLB LODs with bark, needle and snow materials; the granite boulder
generator exports four LODs with granite and wind-packed snow materials. CI
publishes the generated LOD0 runtime models and injects them into both Windows
validation and the playable Sakkola package. Generate the same files locally
with the documented Blender commands, writing
`scots_pine_lod0.glb` and `granite_boulder_lod0.glb` into
`assets/models/environment/generated/`.

In `sakkola-church-village.tregion`, all 32 boulder placements now resolve to
the Blender granite boulder. The Blender Scots pine replaces the former pine
and spruce placements (1,907 of 3,094 trees); birch, aspen, alder and juniper
remain distinct, preserving the mixed Central Finnish forest. Tree height is
varied deterministically per placement in the scene transform, independently
of horizontal scale, so the same world is reproduced across runs and machines.
The four LOD files are generated and validated; current Sakkola scene binding
uses LOD0 until runtime screen-coverage LOD selection is implemented.
