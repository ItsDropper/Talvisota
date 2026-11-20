"""Import and validate every GLB emitted by generate_tree.py inside Blender."""
from collections import Counter
import json
import math
import sys
from pathlib import Path

import bpy


def fail(message):
    raise SystemExit("TREE_ASSET_VERIFY_FAILED: " + message)



def validate_closed_snow_mesh(mesh, lod_name):
    """Weld GLB seam duplicates by position and require a closed snow volume."""
    positions = [
        tuple(round(float(component), 5) for component in vertex.co)
        for vertex in mesh.vertices
    ]
    edge_uses = Counter()
    for polygon in mesh.polygons:
        area = float(polygon.area)
        if not math.isfinite(area) or area <= 1e-10:
            fail(lod_name + " contains a degenerate snow polygon after GLB import")
        vertices = tuple(polygon.vertices)
        for index, vertex_index in enumerate(vertices):
            first = positions[vertex_index]
            second = positions[vertices[(index + 1) % len(vertices)]]
            if first == second:
                fail(lod_name + " contains a zero-length snow edge after GLB import")
            edge_uses[tuple(sorted((first, second)))] += 1
    invalid_edges = sum(count != 2 for count in edge_uses.values())
    if not edge_uses or invalid_edges:
        boundary_edges = sum(count == 1 for count in edge_uses.values())
        non_manifold_edges = sum(count > 2 for count in edge_uses.values())
        fail(lod_name + " snow shell is not closed after GLB round-trip: " +
             str(boundary_edges) + " boundary edges, " +
             str(non_manifold_edges) + " non-manifold edges")


UV_DETERMINANT_EPSILON = 1.0e-8


def uv_triangle_determinant(first, second, third):
    return ((second[0] - first[0]) * (third[1] - first[1]) -
            (third[0] - first[0]) * (second[1] - first[1]))


def validate_normal_map_uv_basis(mesh, lod_name, object_name):
    """Check Blender's exported triangle split against the engine's UV threshold."""
    if len(mesh.uv_layers) == 0:
        fail(lod_name + " " + object_name + " has no UV layer for its normal maps")
    uv_layer = mesh.uv_layers.active or mesh.uv_layers[0]
    mesh.calc_loop_triangles()
    for triangle in mesh.loop_triangles:
        coordinates = [
            (float(uv_layer.data[loop_index].uv.x),
             float(uv_layer.data[loop_index].uv.y))
            for loop_index in triangle.loops
        ]
        determinant = uv_triangle_determinant(*coordinates)
        if not math.isfinite(determinant) or abs(determinant) < UV_DETERMINANT_EPSILON:
            fail(lod_name + " " + object_name + " polygon " + str(triangle.polygon_index) +
                 " triangle " + str(triangle.index) +
                 " has a degenerate tangent-space UV basis (determinant " +
                 format(determinant, ".3e") + ")")
    return len(mesh.loop_triangles)


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
        triangle_limits = {"LOD0": 750_000, "LOD1": 300_000,
                           "LOD2": 90_000, "LOD3": 12_000}
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
        uv_triangle_count = sum(
            validate_normal_map_uv_basis(obj.data, lod_name, obj.name)
            for obj in meshes
        )
        if any(len(obj.data.materials) == 0 for obj in meshes):
            fail(lod_name + " has a mesh without material assignments")
        imported_material_names = {
            material.name for obj in meshes for material in obj.data.materials if material
        }
        if not any("Lower grey-brown plated bark" in name for name in imported_material_names):
            fail(lod_name + " lost the lower grey-brown bark material zone")
        if not any("Upper orange-yellow smooth bark" in name for name in imported_material_names):
            fail(lod_name + " lost the upper orange-yellow bark material zone")
        has_needle_material = any(
            material and "Needles" in material.name
            for obj in meshes for material in obj.data.materials
        )
        if not has_needle_material:
            fail(lod_name + " lost the Scots pine needle material")
        has_snow_material = any(
            material and "Snow" in material.name
            for obj in meshes for material in obj.data.materials
        )
        if not has_snow_material:
            fail(lod_name + " lost the separate snow-load material")
        snow_meshes = [
            obj for obj in meshes
            if any(material and "Snow" in material.name for material in obj.data.materials)
        ]
        if len(snow_meshes) != 1:
            fail(lod_name + " must contain exactly one snow-load mesh")
        validate_closed_snow_mesh(snow_meshes[0].data, lod_name)
        needle_triangles = sum(
            len(polygon.vertices) - 2
            for obj in meshes for polygon in obj.data.polygons
            if polygon.material_index < len(obj.data.materials)
            and obj.data.materials[polygon.material_index]
            and "Needles" in obj.data.materials[polygon.material_index].name
        )
        minimum_needle_triangles = {
            "LOD0": 410_112, "LOD1": 139_968, "LOD2": 18_144, "LOD3": 2_160
        }[lod_name]
        if needle_triangles < minimum_needle_triangles:
            fail(lod_name + " needle geometry is under-dense: " +
                 str(needle_triangles) + " < " + str(minimum_needle_triangles))
        maximum_needle_triangles = {"LOD0": 500_000, "LOD1": 180_000,
                                    "LOD2": 25_000, "LOD3": 6_000}
        if needle_triangles > maximum_needle_triangles[lod_name]:
            fail(lod_name + " needle geometry exceeds its density budget: " +
                 str(needle_triangles) + " > " + str(maximum_needle_triangles[lod_name]))
        bark_triangles = sum(
            len(polygon.vertices) - 2
            for obj in meshes for polygon in obj.data.polygons
            if polygon.material_index < len(obj.data.materials)
            and obj.data.materials[polygon.material_index]
            and "bark" in obj.data.materials[polygon.material_index].name.lower()
        )
        minimum_bark_triangles = {
            "LOD0": 28_188, "LOD1": 15_072, "LOD2": 5_748, "LOD3": 1_452
        }[lod_name]
        if bark_triangles < minimum_bark_triangles:
            fail(lod_name + " woody scaffold geometry regressed: " +
                 str(bark_triangles) + " < " + str(minimum_bark_triangles))
        snow_triangles = sum(
            len(polygon.vertices) - 2
            for obj in meshes for polygon in obj.data.polygons
            if polygon.material_index < len(obj.data.materials)
            and obj.data.materials[polygon.material_index]
            and "Snow" in obj.data.materials[polygon.material_index].name
        )
        expected_snow_triangles = lod.get("snow_triangles")
        if expected_snow_triangles != snow_triangles:
            fail(lod_name + " snow-load triangle manifest/import mismatch: " +
                 str(expected_snow_triangles) + " != " + str(snow_triangles))
        expected_snow_caps = {"LOD0": 120, "LOD1": 80, "LOD2": 42, "LOD3": 18}[lod_name]
        if lod.get("snow_triangles_per_cap") != 66:
            fail(lod_name + " has an unexpected snow-cap topology budget")
        if snow_triangles % 66 != 0 or lod.get("snow_caps") != snow_triangles // 66:
            fail(lod_name + " snow-cap count does not match its 66-triangle closed-shell budget")
        if lod.get("snow_caps") != expected_snow_caps:
            fail(lod_name + " deterministic snow-cap count changed: " +
                 str(lod.get("snow_caps")) + " != " + str(expected_snow_caps))
        minimum_snow_triangles = {
            "LOD0": 1000, "LOD1": 800, "LOD2": 500, "LOD3": 200
        }[lod_name]
        maximum_snow_triangles = {
            "LOD0": 8000, "LOD1": 7000, "LOD2": 5000, "LOD3": 2500
        }[lod_name]
        if snow_triangles < minimum_snow_triangles:
            fail(lod_name + " snow-load geometry is under-dense: " +
                 str(snow_triangles) + " < " + str(minimum_snow_triangles))
        if snow_triangles > maximum_snow_triangles:
            fail(lod_name + " snow-load geometry exceeds its budget: " +
                 str(snow_triangles) + " > " + str(maximum_snow_triangles))
        expected_density = {
            "LOD0": (10, 8, 8),
            "LOD1": (9, 7, 6),
            "LOD2": (6, 5, 3),
            "LOD3": (4, 3, 3),
        }[lod_name]
        actual_density = (
            lod.get("branchlets_per_secondary"),
            lod.get("sprays_per_branchlet"),
            lod.get("needle_pairs_per_spray"),
        )
        if actual_density != expected_density:
            fail(lod_name + " has unexpected foliage-density metadata: " +
                 str(actual_density) + " != " + str(expected_density))
        print("VERIFIED", lod_name, "meshes=", len(meshes),
              "triangles=", sum(len(p.vertices) - 2 for obj in meshes for p in obj.data.polygons),
              "uv_triangles=", uv_triangle_count,
              "needle_triangles=", needle_triangles,
              "bark_triangles=", bark_triangles,
              "snow_caps=", lod.get("snow_caps"),
              "snow_triangles=", snow_triangles)
    print("TALVISOTA_TREE_ASSET_VERIFY_OK")


if __name__ == "__main__":
    main()
