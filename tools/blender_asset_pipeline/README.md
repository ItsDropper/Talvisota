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
- Irregularly spaced and rotated whorls establish the major scaffold limbs.
- Scaffold limbs are long, thick, tapered and slightly drooping; their lengths
  peak in the lower-middle crown and shorten toward the top.
- Secondary branches split from the scaffold at non-uniform positions and
  angles.
- Fine terminal twigs carry explicit paired needle geometry. Individual
  needles are generated at 4–7 cm scale, rather than representing the whole
  crown with green blobs.
- Snow accents are separate geometry and are currently a sparse structural
  approximation, not a validated snow-load simulation.

This is an evolving structural model. Its branching distribution, needle
clustering, bark zoning, natural damage and snow deformation still require
reference-driven art iteration. The generated PBR maps are deterministic
procedural source maps, not captured photogrammetry textures. For a genuinely
photorealistic final asset, replace or augment them with licensed high-resolution
photographic/photogrammetric material captures and validate renders against
reference photographs from multiple angles.

## Geometry and performance policy

- Each tree LOD is consolidated into no more than three material-grouped mesh
  objects. This avoids per-twig draw calls while retaining separate material
  surfaces.
- LOD0/1/2/3 progressively reduce whorls, scaffold limbs, secondary/tertiary
  branches, needle clusters and radial sides. The manifest records vertices
  and triangles.
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

UVs are generated per material-grouped mesh with Smart UV Project. Geometry
uses Blender's native Z-up authoring space; the glTF exporter performs the
standard conversion to glTF's +Y-up, metre-based coordinate convention used by
Talvisota. The exported manifest records Talvisota's +Y up, +X east, +Z north
convention.

## Verify

    blender --background --python tools/blender_asset_pipeline/verify_tree_outputs.py -- build/generated/trees

The verifier re-imports each GLB in Blender and checks its GLB container,
non-empty mesh geometry, UVs, material assignments, required source maps and
strictly decreasing triangle budgets. This proves export integrity, not visual
photorealism. Talvisota's engine-backed asset validator and content packager
remain the integration gates once generated files are registered in the
content manifest. This generator does not silently replace shipped OBJ assets
or alter the content catalog.
