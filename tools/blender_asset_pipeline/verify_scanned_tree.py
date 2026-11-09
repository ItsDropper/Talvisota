#!/usr/bin/env python3
"""Re-import and verify GLBs produced by ingest_scanned_tree.py."""
import json
import sys
from pathlib import Path

import bpy


def fail(message):
    raise SystemExit("SCANNED_TREE_VERIFY_FAILED: " + message)


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(args) != 1:
        fail("usage: blender --background --python verify_scanned_tree.py -- <manifest.json>")
    manifest_path = Path(args[0])
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != "talvisota.scanned-tree.v1":
        fail("unexpected scanned-tree manifest schema")
    if len(manifest.get("source_sha256", "")) != 64:
        fail("source provenance SHA-256 is missing")
    if not manifest.get("source_image_textures"):
        fail("source has no image-textured materials")
    if not hasattr(bpy.ops.import_scene, "gltf"):
        try:
            bpy.ops.preferences.addon_enable(module="io_scene_gltf2")
        except RuntimeError:
            pass
    if not hasattr(bpy.ops.import_scene, "gltf"):
        fail("Blender glTF importer is unavailable")

    previous_triangles = None
    for lod_name in ("LOD0", "LOD1", "LOD2", "LOD3"):
        lod = manifest.get("lods", {}).get(lod_name)
        if not lod:
            fail("missing " + lod_name + " manifest entry")
        path = manifest_path.parent / lod["file"]
        if not path.is_file() or path.stat().st_size < 128:
            fail(lod_name + " GLB is missing or empty")
        with path.open("rb") as stream:
            if stream.read(4) != b"glTF":
                fail(lod_name + " does not have a GLB header")
        if previous_triangles is not None and lod["triangles"] >= previous_triangles:
            fail("triangle budget did not decrease at " + lod_name)
        previous_triangles = lod["triangles"]

        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.object.delete(use_global=False)
        bpy.ops.import_scene.gltf(filepath=str(path))
        meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
        if not meshes:
            fail(lod_name + " imports without mesh objects")
        if any(not obj.data.uv_layers for obj in meshes):
            fail(lod_name + " contains a mesh without UV coordinates")
        if any(not obj.data.materials for obj in meshes):
            fail(lod_name + " contains a mesh without materials")
        images = {
            node.image.name
            for obj in meshes
            for material in obj.data.materials if material and material.use_nodes
            for node in material.node_tree.nodes
            if node.type == "TEX_IMAGE" and node.image
        }
        if not images:
            fail(lod_name + " lost all image textures during GLB export")
        imported_triangles = sum(max(0, len(poly.vertices) - 2)
                                 for obj in meshes for poly in obj.data.polygons)
        if imported_triangles <= 0:
            fail(lod_name + " imports without triangle geometry")
        print("VERIFIED_SCANNED", lod_name, "meshes=", len(meshes),
              "triangles=", imported_triangles, "images=", len(images))
    print("TALVISOTA_SCANNED_TREE_VERIFY_OK")


if __name__ == "__main__":
    main()
