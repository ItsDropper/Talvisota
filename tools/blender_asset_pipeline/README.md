# Blender environment asset pipeline

This is Talvisota's deterministic Blender asset-authoring pipeline. Its first
reference asset is a mature, open-grown Finnish Scots pine (*Pinus sylvestris*).
The generator creates editable Blender source and runtime GLBs; it does not
claim that procedural output is equivalent to photogrammetry or a real scan.

## Requirements

- Blender 3.6+ with its bundled glTF 2.0 exporter enabled.
- The repository's generated environment PBR maps in
  `assets/textures/environment`.
- Python is bundled with Blender. The CI job installs NumPy for the Blender
  glTF exporter when the distribution package does not include it.

## Generate

From the repository root:

    blender --background --python tools/blender_asset_pipeline/generate_tree.py -- --output build/generated/trees --texture-root assets/textures/environment --seed 1939

The generator writes:

- `scots_pine.blend`: editable source scene containing all requested LODs.
- `scots_pine_lod0.glb` through `scots_pine_lod3.glb`: separate runtime
  exports, with embedded source texture images.
- `scots_pine.asset.json`: species, coordinate, material, LOD and geometry
  budget metadata.

The default source proportions are 18 m high with a broad 12 m crown envelope.
Use `--height`, `--radius`, and `--seed` to generate controlled variants.
Reusing the same Blender version, input maps, seed and arguments produces the
same procedural topology and geometry counts. Blender/exporter versions may
change binary GLB bytes, so CI validates semantic output and budgets instead.

## Biological construction model

The target is a mature, broad-crowned, open-grown Scots pine—not a symmetric
conical nursery tree.

- A tapered, slightly wandering trunk establishes the clear lower bole.
- The lower bole and upper trunk/scaffold use separate material slots: grey-brown
  plated bark below, orange-yellow smoother bark above. Both currently reuse
  the available bark source maps with distinct colour treatment; final close-up
  fidelity still depends on scanned bark imagery and calibrated material data.
- Irregularly spaced and rotated whorls establish the major scaffold limbs.
- Scaffold limbs are long, thick, tapered and slightly drooping; their lengths
  peak in the lower-middle crown and shorten toward the top.
- Secondary branches split from the scaffold at non-uniform positions and
  angles.
- Fine terminal shoots are distributed along the full secondary limbs, not
  concentrated near branch attachments.
- Each terminal shoot carries multiple radial fascicle sprays. Individual
  paired needles are generated at 10.5–14.5 cm scale as tapered, double-sided
  blade strips; dense sprays overlap to form a continuous but irregular crown.
- Primary branch lengths, azimuths, vertical positions, secondary splits and
  terminal-shoot directions vary independently. The crown should not resolve
  into evenly spaced horizontal whorl rings.
- Snow accents are separate geometry and are currently a sparse structural
  approximation, not a validated snow-load simulation.

This is an evolving structural model. Its branching distribution, needle
clustering, bark zoning, natural damage and snow deformation still require
reference-driven art iteration. The generated PBR maps are deterministic
procedural source maps, not captured photogrammetry textures. For a genuinely
photorealistic final asset, replace or augment them with licensed high-resolution
photographic/photogrammetric material captures and validate renders against
reference photographs from multiple angles.


## Photogrammetry ingestion (the photorealism path)

For a genuinely photorealistic hero tree, use a real, licensed photogrammetry
or multi-view reconstruction export as the source of truth. The scan-ingestion
path preserves captured geometry and image textures; it does not synthesize a
replacement mesh or invent texture detail.

Accepted source formats are GLB/glTF, FBX and OBJ. The source must contain
mesh geometry, UV coordinates, material assignments and at least one
image-textured material. The default validation also rejects scans below
1,000 triangles. The original source SHA-256, source size, scale, per-mesh vertex/triangle
counts, UV-layer presence and degenerate-UV-face counts, texture names,
texture dimensions/channel counts/color spaces/packing state, and per-LOD
triangle budgets are recorded in the manifest. Texture resolution is reported
as supplied: 4K or larger maps are recommended for close-up hero trees; the
pipeline never upscales a low-resolution scan and pretends it contains detail.

    blender --background --python tools/blender_asset_pipeline/ingest_scanned_tree.py -- --source reference/scots_pine_scan.glb --output build/generated/scanned-pine --target-height 18

This writes a provenance manifest, an editable `*_scanned_source.blend`, and
four self-contained GLB LOD exports. LOD0 preserves the imported scan geometry;
LOD1–LOD3 use separate collapse-decimation ratios for woody and foliage meshes.
Woody defaults are 0.60, 0.25, 0.10; foliage defaults are deliberately more
conservative at 0.85, 0.65, 0.40. Foliage meshes are identified by material names
containing `needle`, `foliage`, `leaf` or `leaves`. Keep scanned needle/leaf
surfaces in distinct mesh objects with descriptive material names: a combined
trunk-and-needle mesh cannot be selectively decimated and will use the
conservative foliage ratios as a whole. The pipeline records source image
resolution, channel count, packing status, source SHA-256 and both LOD policies.
It refuses to export when a referenced source image cannot be packed, rather
than producing a superficially valid GLB with missing textures.

Verify the exports with:

    blender --background --python tools/blender_asset_pipeline/verify_scanned_tree.py -- build/generated/scanned-pine/scots_pine_scan.scanned-tree.json

This ingestion route requires a genuine external scan. A procedural model,
an untextured mesh, or a generated OBJ does not become photorealistic merely by
passing the importer. The procedural Scots-pine generator remains useful for
controlled structural variants and lower-cost background trees; use the scan
path for close-up hero assets when the captured source quality supports it.

## Geometry and performance policy

- Each tree LOD is consolidated into no more than three material-grouped mesh
  objects. This avoids per-twig draw calls while retaining separate material
  surfaces.
- LOD0/1/2/3 progressively reduce scaffold density, secondary/tertiary
  branching and needle sprays. Individual needles are tapered blade strips,
  not capped cylinders; this reduces foliage triangles while retaining their
  paired silhouette. The LOD0 primary scaffold uses five-sided tubes rather
  than six-sided tubes to keep its branch silhouette while meeting the close-up
  geometry budget. The verifier enforces budgets of 650k / 300k / 150k / 50k
  triangles for LOD0 through LOD3. LOD0 is the close-up hero asset; use lower
  LODs for progressively smaller screen coverage.
- The forest must instance individual tree assets at the scene/runtime layer;
  do not merge the entire forest into one world-sized mesh, which destroys
  useful frustum/occlusion-culling granularity.
- Runtime LOD selection, GPU instancing, visibility culling and transition
  hysteresis are Talvisota runtime responsibilities. The pinned VantaEngine
  importer loads static glTF/GLB, while glTF core has no universal runtime LOD
  selector. The exports are separate files intentionally.
- Keep collision meshes separate and simple. Render foliage must not become a
  high-cost collision surface.
- Profile actual GPU time, draw calls, visible instances, vertex throughput and
  texture memory in the game. Triangle counts alone are not a performance test.

## Materials and texture conventions

The generator uses registered bark, pine-needle and snow albedo, roughness and
tangent-space normal maps when present. Colour images are marked sRGB; roughness
and normal data are marked Non-Color. Normal maps are connected through a
Blender Normal Map node before GLB export. Missing optional maps fall back to
named Principled BSDF material values; the manifest records source-map status.

UV coordinates are authored deterministically per tapered tube face in the
mesh builder; no interactive UV-editor operator or viewport context is needed.
Geometry uses Blender's native Z-up authoring space; the glTF exporter performs
the standard conversion to glTF's +Y-up, metre-based coordinate convention
used by Talvisota. The exported manifest records Talvisota's +Y up, +X east,
+Z north convention.

## Verify

    blender --background --python tools/blender_asset_pipeline/verify_tree_outputs.py -- build/generated/trees

The verifier re-imports each GLB in Blender and checks its GLB container,
non-empty mesh geometry, UVs, material assignments, required source maps and
strictly decreasing triangle budgets. This proves export integrity, not visual
photorealism. Talvisota's engine-backed asset validator and content packager
remain the integration gates once generated files are registered in the
content manifest. This generator does not silently replace shipped OBJ assets
or alter the content catalog.
