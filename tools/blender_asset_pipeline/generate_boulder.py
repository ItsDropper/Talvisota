#!/usr/bin/env python3
"""Deterministic Blender generator for a snow-capped Finnish granite glacial boulder."""
import argparse
import json
import math
import random
import sys
from pathlib import Path

import bpy
from mathutils import Vector

PRESETS = {
    "LOD0": {"subdivisions": 4, "budget": 6_000},
    "LOD1": {"subdivisions": 3, "budget": 2_000},
    "LOD2": {"subdivisions": 2, "budget": 500},
    "LOD3": {"subdivisions": 1, "budget": 150},
}


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--texture-root", type=Path, default=Path("assets/textures/environment"))
    parser.add_argument("--seed", type=int, default=1939)
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])


def make_material(name, texture_root, albedo, normal, roughness, ao, fallback):
    material = bpy.data.materials.new(name)
    material.diffuse_color = (*fallback, 1.0)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    shader = nodes.get("Principled BSDF")
    shader.inputs["Roughness"].default_value = 0.88
    for filename, color_space, socket in (
        (albedo, "sRGB", "Base Color"),
        (roughness, "Non-Color", "Roughness"),
        (ao, "Non-Color", ""),
        (normal, "Non-Color", "Normal"),
    ):
        path = texture_root / filename
        if not path.is_file():
            raise FileNotFoundError("Required boulder PBR map is missing: " + str(path))
        image = bpy.data.images.load(str(path.resolve()), check_existing=True)
        image.colorspace_settings.name = color_space
        image.pack()
        texture = nodes.new("ShaderNodeTexImage")
        texture.image = image
        texture.label = filename
        if socket == "Normal":
            normal_node = nodes.new("ShaderNodeNormalMap")
            links.new(texture.outputs["Color"], normal_node.inputs["Color"])
            links.new(normal_node.outputs["Normal"], shader.inputs["Normal"])
        elif socket:
            links.new(texture.outputs["Color"], shader.inputs[socket])
        elif filename == ao:
            multiply = nodes.new("ShaderNodeMixRGB")
            multiply.blend_type = "MULTIPLY"
            multiply.inputs[0].default_value = 0.35
            links.new(shader.inputs["Base Color"].links[0].from_socket, multiply.inputs[1]) if shader.inputs["Base Color"].is_linked else None
            links.new(texture.outputs["Color"], multiply.inputs[2])
            if multiply.inputs[1].is_linked:
                for link in list(shader.inputs["Base Color"].links):
                    links.remove(link)
                links.new(multiply.outputs["Color"], shader.inputs["Base Color"])
    return material


def shape_point(direction, seed):
    x, y, z = direction
    broad = math.sin(x * 4.1 + y * 2.7 + seed * 0.013) * math.cos(z * 3.8 - y * 2.1)
    medium = math.sin(x * 12.7 + z * 10.3 + seed * 0.071) * math.sin(y * 13.1 - z * 8.4)
    fine = math.sin(x * 31.0 + y * 23.0 + z * 27.0 + seed * 0.19)
    radial = 1.0 + broad * 0.11 + medium * 0.04 + fine * 0.014
    px = x * 1.22 * radial
    py = y * 0.96 * radial
    pz = 0.82 + z * 0.91 * radial
    # Glacial abrasion leaves a broad, stable resting plane rather than a pointed base.
    pz = max(0.035, pz)
    return (px, py, pz)


def build_lod(name, preset, rock, snow, seed):
    bpy.ops.mesh.primitive_ico_sphere_add(
        subdivisions=preset["subdivisions"], radius=1.0, location=(0.0, 0.0, 0.0))
    source = bpy.context.object
    source_mesh = source.data
    source_vertices = [shape_point(vertex.co.normalized(), seed) for vertex in source_mesh.vertices]
    source_faces = [tuple(poly.vertices) for poly in source_mesh.polygons]
    bpy.data.objects.remove(source, do_unlink=True)
    mesh = bpy.data.meshes.new("GraniteBoulder_" + name + "Mesh")
    mesh.from_pydata(source_vertices, [], source_faces)
    mesh.materials.append(rock)
    mesh.materials.append(snow)
    mesh.update()
    uv = mesh.uv_layers.new(name="UVMap")
    for polygon in mesh.polygons:
        coords = []
        for vertex_index in polygon.vertices:
            point = Vector(mesh.vertices[vertex_index].co)
            direction = Vector((point.x / 1.22, point.y / 0.96, (point.z - 0.82) / 0.91)).normalized()
            u = 0.5 + math.atan2(direction.y, direction.x) / (2.0 * math.pi)
            v = 0.5 - math.asin(max(-1.0, min(1.0, direction.z))) / math.pi
            coords.append([u, v])
        if max(item[0] for item in coords) - min(item[0] for item in coords) > 0.5:
            for item in coords:
                if item[0] < 0.5:
                    item[0] += 1.0
        for loop_index, coord in zip(polygon.loop_indices, coords):
            uv.data[loop_index].uv = coord
        center = polygon.center
        normal = polygon.normal
        edge_noise = (
            math.sin(center.x * 5.7 + seed * 0.13) *
            math.cos(center.y * 6.9 - seed * 0.07) * 0.075 +
            math.sin(center.x * 15.1 - center.y * 11.3 + seed) * 0.028
        )
        # A small, irregular snow cap: exposed granite must dominate the visible
        # surface, and the rock keeps its fractured planar facets.
        snowline = 1.50 + edge_noise * 1.15
        polygon.material_index = 1 if normal.z > 0.90 and center.z > snowline else 0
        polygon.use_smooth = polygon.material_index == 1
    obj = bpy.data.objects.new("Finnish_Glacial_Boulder_" + name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj, len(mesh.polygons)


def main():
    args = arguments()
    args.output.mkdir(parents=True, exist_ok=True)
    random.seed(args.seed)
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    texture_root = args.texture_root
    required = ("granite-albedo.bmp", "granite-normal.bmp", "granite-roughness.bmp", "granite-ao.bmp",
                "snow-albedo.bmp", "snow-normal.bmp", "snow-roughness.bmp", "snow-ao.bmp")
    missing = [name for name in required if not (texture_root / name).is_file()]
    if missing:
        raise SystemExit("Missing PBR maps: " + ", ".join(missing))
    rock = make_material("Granite | weathered glacial rock", texture_root,
                         "granite-albedo.bmp", "granite-normal.bmp",
                         "granite-roughness.bmp", "granite-ao.bmp", (0.31, 0.32, 0.34))
    snow = make_material("Snow | wind-packed cap", texture_root,
                         "snow-albedo.bmp", "snow-normal.bmp",
                         "snow-roughness.bmp", "snow-ao.bmp", (0.78, 0.84, 0.89))
    collections = {}
    manifest = {"schema": "talvisota.boulder-asset.v1", "asset": "model.environment.boulder",
                "seed": args.seed, "dimensions_m": {"width": 2.44, "depth": 1.92, "height": 1.73},
                "materials": [rock.name, snow.name], "lods": {}}
    for name, preset in PRESETS.items():
        collection = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(collection)
        bpy.context.view_layer.active_layer_collection = bpy.context.view_layer.layer_collection.children[name]
        obj, triangles = build_lod(name, preset, rock, snow, args.seed)
        for owner in list(obj.users_collection):
            owner.objects.unlink(obj)
        collection.objects.link(obj)
        collections[name] = collection
        if triangles > preset["budget"]:
            raise SystemExit(f"{name} exceeds triangle budget: {triangles} > {preset['budget']}")
        manifest["lods"][name] = {"file": f"granite_boulder_{name.lower()}.glb",
                                  "objects": 1, "triangles": triangles,
                                  "screen_coverage_hint": {"LOD0": 0.30, "LOD1": 0.10, "LOD2": 0.025, "LOD3": 0.0}[name]}
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.export_scene.gltf(filepath=str(args.output / manifest["lods"][name]["file"]),
                                  export_format="GLB", use_selection=True,
                                  export_apply=True, export_materials="EXPORT")
        collection.hide_viewport = True
        collection.hide_render = True
    collections["LOD0"].hide_viewport = False
    collections["LOD0"].hide_render = False
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output / "granite_boulder.blend"))
    (args.output / "granite_boulder.asset.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("TALVISOTA_BOULDER_PIPELINE_OK", json.dumps(manifest["lods"], sort_keys=True))


if __name__ == "__main__":
    main()
