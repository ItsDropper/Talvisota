# Model assets

Talvisota stores model sources in the manifest and delegates generic geometry import to VantaEngine. The pinned engine provides CPU import paths for Wavefront OBJ/MTL and glTF 2.0/GLB static meshes, plus material extraction and validation through `ModelAssetStore`.

Use metres and Talvisota's right-handed coordinate convention (+X east, +Y up, +Z north). Record origin/pivot, LODs, collision intent, and material dependencies. OBJ has no universal unit declaration; author OBJ coordinates in metres unless an explicit import conversion is added to the asset metadata. glTF uses its defined metre convention.

The engine-backed `talvisota_asset_validate` command imports registered models without creating a graphics device. External material textures must resolve inside the content root, be registered as texture assets, and be listed in the model's manifest dependencies. Embedded glTF data-URI and GLB images are not supported by the pinned importer and are rejected with an explicit diagnostic.
