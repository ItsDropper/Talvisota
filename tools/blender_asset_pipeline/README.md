# Blender environment asset pipeline

This is the first production-oriented vertical slice of Talvisota's reusable
Blender pipeline. The initial asset is an original procedural Scots pine
(Pinus sylvestris) suitable as a starting point for Central Finland forest
art direction. It is a deterministic, editable Blender scene—not a hand-written
OBJ vertex dump.

## Requirements

- Blender 3.6+ with its bundled glTF 2.0 exporter enabled.
- The repository's generated environment PBR maps in
  assets/textures/environment.
- Python is bundled with Blender; no third-party Python packages are required.

## Generate

From the repository root:

    blender --background --python tools/blender_asset_pipeline/generate_tree.py -- --output build/generated/trees --texture-root assets/textures/environment --seed 1939

The generator writes:

- scots_pine.blend: editable source scene containing all requested LODs.
- scots_pine_lod0.glb through scots_pine_lod3.glb: separate runtime-ready
  exports, with embedded source texture images.
- scots_pine.asset.json: deterministic species, coordinate, material, LOD
  and geometry-budget metadata.

The default seed is 1939. Reusing the same Blender version, input maps, seed
and arguments produces the same procedural topology and geometry counts.
Blender may still change binary GLB byte output between Blender/exporter
versions, so CI validates semantic output and geometry budgets rather than
assuming cross-version byte identity.

## Geometry and performance policy

- The tree is built into a small number of material-grouped meshes per LOD,
  not thousands of independently submitted branch/needle objects.
- The forest must instance these source assets at the scene/runtime layer;
  do not merge an entire forest into one world-sized mesh, because that destroys
  useful frustum/occlusion culling granularity.
- LOD0/1/2/3 progressively reduce branch tiers, branch counts, branch segments,
  needle sprays and radial sides. The manifest records vertices and triangles.
- LOD selection is a Talvisota runtime responsibility. The pinned VantaEngine
  importer loads static glTF/GLB but the glTF core specification does not define
  a universal runtime LOD selector. The exports are separate files intentionally;
  do not claim that importing one GLB automatically switches LOD.
- Keep collision meshes separate and simple when tree collision is introduced.
  Render foliage should not become a high-cost collision surface.

## Materials and texture conventions

The generator uses registered bark, pine-needle and snow albedo, roughness and
tangent-space normal maps when present. Colour images are marked sRGB; roughness
and normal data are marked Non-Color. Normal maps are connected through a
Blender Normal Map node before GLB export. Missing optional maps fall back to
named Principled BSDF material values; the manifest still records expected
map names so missing source maps are visible during review.

UVs are generated per material-grouped mesh with Smart UV Project. Geometry
uses Blender's native Z-up authoring space; the glTF exporter performs the
standard conversion to glTF's +Y-up, metre-based coordinate convention used by
Talvisota. The exported manifest records Talvisota's +Y up, +X east, +Z north
convention.

## Verify

    blender --background --python tools/blender_asset_pipeline/verify_tree_outputs.py -- build/generated/trees

The verifier re-imports each GLB in Blender and checks its GLB container,
non-empty mesh geometry, UVs, material assignments and strictly decreasing
triangle budgets. Talvisota's existing engine-backed asset validator and
content packager remain the final integration gates once the generated files
are registered in the content manifest. This generator does not silently
replace the shipped spruce/pine models or alter the content catalog.
