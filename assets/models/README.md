# Model assets

The current model importer accepts Wavefront OBJ geometry. OBJ has no universal unit declaration, so the import API requires an explicit `unitsToMeters` multiplier (the content validator currently assumes source coordinates are metres). Talvisota's axis convention is right-handed, +X east, +Y up, +Z north. Set the source asset to that convention before import; the importer does not guess axis conversions.

The importer handles positions (including homogeneous w), texture coordinates, normals, positive and negative face indices, material-use groups, and material-library names. Polygon faces are ear-clipped in their dominant 2D projection so concave polygons do not receive incorrect triangle fans. Missing normals are generated per face; supplied normals are normalized. An optional V-coordinate flip is explicit in import settings.

The importer currently preserves `mtllib` and `usemtl` names but does not parse MTL material files or resolve their texture maps. glTF/GLB import, tangent generation, skeletal data, LODs, collision extraction, and runtime GPU upload remain unsupported. Manifest recognition of glTF/GLB is not a claim that those formats are imported.
