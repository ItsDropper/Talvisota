"""Re-import and validate Blender-generated glacial boulder LODs."""
import json
import sys
from pathlib import Path

import bpy


def fail(message):
    raise SystemExit("BOULDER_ASSET_VERIFY_FAILED: " + message)


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(args) != 1:
        fail("usage: blender --background --python verify_boulder_outputs.py -- <output-dir>")
    root = Path(args[0])
    manifest = json.loads((root / "granite_boulder.asset.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != "talvisota.boulder-asset.v1":
        fail("unexpected manifest schema")
    previous = None
    budgets = {"LOD0": 6_000, "LOD1": 2_000, "LOD2": 500, "LOD3": 150}
    for name in ("LOD0", "LOD1", "LOD2", "LOD3"):
        lod = manifest.get("lods", {}).get(name)
        if not lod:
            fail(name + " missing from manifest")
        path = root / lod["file"]
        if not path.is_file() or path.stat().st_size < 128:
            fail(name + " GLB missing or too small")
        if path.read_bytes()[:4] != b"glTF":
            fail(name + " invalid GLB header")
        if lod["triangles"] > budgets[name]:
            fail(name + " exceeds triangle budget")
        if previous is not None and lod["triangles"] >= previous:
            fail("LOD triangle counts do not decrease")
        previous = lod["triangles"]
        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.object.delete(use_global=False)
        if not hasattr(bpy.ops.import_scene, "gltf"):
            try:
                bpy.ops.preferences.addon_enable(module="io_scene_gltf2")
            except RuntimeError:
                pass
        bpy.ops.import_scene.gltf(filepath=str(path))
        meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
        if len(meshes) != 1:
            fail(name + " must import as exactly one mesh object")
        mesh = meshes[0].data
        if len(mesh.polygons) == 0 or len(mesh.uv_layers) == 0:
            fail(name + " has empty geometry or missing UVs")
        names = {material.name for material in mesh.materials if material}
        if not any("Granite" in name for name in names) or not any("Snow" in name for name in names):
            fail(name + " lost granite or snow material")
        if not any(poly.material_index == 1 for poly in mesh.polygons):
            fail(name + " has no snow-cap faces")
        if not any(poly.material_index == 0 for poly in mesh.polygons):
            fail(name + " has no exposed granite faces")
        print("VERIFIED", name, "triangles=", len(mesh.polygons), "materials=", sorted(names))
    print("TALVISOTA_BOULDER_ASSET_VERIFY_OK")


if __name__ == "__main__":
    main()
