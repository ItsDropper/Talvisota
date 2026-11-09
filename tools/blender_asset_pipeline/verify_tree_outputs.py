"""Import and validate every GLB emitted by generate_tree.py inside Blender."""
import json
import sys
from pathlib import Path

import bpy


def fail(message):
    raise SystemExit("TREE_ASSET_VERIFY_FAILED: " + message)


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(args) != 1:
        fail("usage: blender --background --python verify_tree_outputs.py -- <output-dir>")
    root = Path(args[0])
    manifest = json.loads((root / "scots_pine.asset.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != "talvisota.tree-asset.v1":
        fail("unexpected manifest schema")
    if not hasattr(bpy.ops.import_scene, "gltf"):
        try:
            bpy.ops.preferences.addon_enable(module="io_scene_gltf2")
        except RuntimeError:
            pass
    if not hasattr(bpy.ops.import_scene, "gltf"):
        fail("Blender glTF 2.0 importer is not installed or enabled")
    previous_triangles = None
    missing_maps = [name for name, found in manifest.get("material_map_status", {}).items() if not found]
    if missing_maps:
        fail("missing PBR source maps: " + ", ".join(missing_maps))
    for lod_name in ("LOD0", "LOD1", "LOD2", "LOD3"):
        lod = manifest["lods"].get(lod_name)
        if not lod:
            continue
        path = root / lod["file"]
        if not path.is_file() or path.stat().st_size < 128:
            fail(lod_name + " GLB is missing or unexpectedly small")
        with path.open("rb") as stream:
            header = stream.read(12)
        if header[:4] != b"glTF":
            fail(lod_name + " does not have a GLB header")
        triangle_limits = {"LOD0": 700_000, "LOD1": 300_000,
                           "LOD2": 150_000, "LOD3": 50_000}
        if lod["triangles"] > triangle_limits[lod_name]:
            fail(lod_name + " exceeds its triangle budget: " + str(lod["triangles"]) +
                 " > " + str(triangle_limits[lod_name]))
        if previous_triangles is not None and lod["triangles"] >= previous_triangles:
            fail("triangle budget did not decrease at " + lod_name)
        previous_triangles = lod["triangles"]

        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.object.delete(use_global=False)
        bpy.ops.import_scene.gltf(filepath=str(path))
        meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
        if not meshes:
            fail(lod_name + " imports without mesh objects")
        if sum(len(obj.data.polygons) for obj in meshes) == 0:
            fail(lod_name + " contains no polygons")
        if any(len(obj.data.uv_layers) == 0 for obj in meshes):
            fail(lod_name + " has a mesh without UV coordinates")
        if any(len(obj.data.materials) == 0 for obj in meshes):
            fail(lod_name + " has a mesh without material assignments")
        imported_material_names = {
            material.name for obj in meshes for material in obj.data.materials if material
        }
        if not any("Lower grey-brown plated bark" in name for name in imported_material_names):
            fail(lod_name + " lost the lower grey-brown bark material zone")
        if not any("Upper orange-yellow smooth bark" in name for name in imported_material_names):
            fail(lod_name + " lost the upper orange-yellow bark material zone")
        print("VERIFIED", lod_name, "meshes=", len(meshes),
              "triangles=", sum(len(p.vertices) - 2 for obj in meshes for p in obj.data.polygons))
    print("TALVISOTA_TREE_ASSET_VERIFY_OK")


if __name__ == "__main__":
    main()
