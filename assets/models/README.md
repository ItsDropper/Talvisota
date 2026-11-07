# Model assets

Talvisota stores model sources in the manifest and delegates generic geometry import to VantaEngine. The pinned engine provides CPU import paths for Wavefront OBJ/MTL and glTF 2.0/GLB static meshes, plus named MTL material extraction, multiple material-library support, per-material submesh assignment, and validation through `ModelAssetStore`.

Use metres and Talvisota's right-handed coordinate convention (+X east, +Y up, +Z north). Record origin/pivot, LODs, collision intent, and material dependencies. OBJ has no universal unit declaration; author OBJ coordinates in metres unless an explicit import conversion is added to the asset metadata. glTF uses its defined metre convention.

The engine-backed `talvisota_asset_validate` command imports registered models without creating a graphics device. External material textures must resolve inside the content root, be registered as texture assets, and be listed in the model's manifest dependencies. glTF data-URI images and GLB buffer-view images are retained as owned encoded payloads in CPU materials and decoded by the engine-backed validator without creating sidecar files.


## Initial Central Finland environment set

`environment/spruce.obj` is an original metre-scale Norway spruce with separate bark, needle, and snow material groups. `environment/boulder.obj` is an original irregular snow-capped boulder with rock and snow material groups. Both use registered MTL sidecars and are referenced by stable IDs in `content/catalog.tmanifest`. Their current geometry establishes the first environment-composition/import path; it is not final photorealistic art and does not replace later high-resolution, textured vegetation and rock assets.
