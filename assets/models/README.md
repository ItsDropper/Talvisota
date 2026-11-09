# Model assets

Talvisota stores model sources in the manifest and delegates generic geometry import to VantaEngine. The pinned engine provides CPU import paths for Wavefront OBJ/MTL and glTF 2.0/GLB static meshes, plus named MTL material extraction, multiple material-library support, per-material submesh assignment, and validation through `ModelAssetStore`.

Use metres and Talvisota's right-handed coordinate convention (+X east, +Y up, +Z north). Record origin/pivot, LODs, collision intent, and material dependencies. OBJ has no universal unit declaration; author OBJ coordinates in metres unless an explicit import conversion is added to the asset metadata. glTF uses its defined metre convention.

The engine-backed `talvisota_asset_validate` command imports registered models without creating a graphics device. External material textures must resolve inside the content root, be registered as texture assets, and be listed in the model's manifest dependencies. glTF data-URI images and GLB buffer-view images are retained as owned encoded payloads in CPU materials and decoded by the engine-backed validator without creating sidecar files.


## Initial Central Finland environment set

`environment/spruce.obj` is an original metre-scale Norway spruce with separate bark, needle, and snow material groups, 13,320 positions, 13,320 cylindrical UV coordinates, and 19,992 triangles. `environment/boulder.obj` is an original displaced, irregular snow-capped granite boulder with rock and snow material groups, 2,562 positions, spherical UV coordinates, and 5,120 triangles. Its deterministic multi-frequency displacement adds broad silhouette breakup and smaller surface irregularity. Both use registered MTL sidecars and stable IDs in `content/catalog.tmanifest`. UVs establish a usable texture-coordinate basis, but these meshes still do not have authored normal maps, texture-backed PBR materials, or renderer-verified shading; they are higher-detail source geometry, not finished photorealistic production assets.

The shared granite boulder now also has an editable Blender authoring pipeline
under `tools/blender_asset_pipeline/generate_boulder.py`. It exports four
independent GLB LODs with granite and upward-facing wind-packed snow materials.
The source OBJ remains the shipped model until the generated asset passes
in-game material, placement, collision and runtime-LOD integration review.
