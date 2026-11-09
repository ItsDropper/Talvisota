#!/usr/bin/env python3
"""Deterministic, headless Blender generator for Talvisota environment trees.

Run:
  blender --background --python tools/blender_asset_pipeline/generate_tree.py -- \
    --output build/generated/trees --texture-root assets/textures/environment

Outputs one .blend source, one GLB per LOD, and a machine-readable manifest.
The GLBs are deliberately split by LOD: the pinned VantaEngine glTF importer
loads static glTF meshes but does not define a universal glTF-core LOD selector.
Talvisota's runtime can therefore select the correct file without relying on
vendor-specific MSFT_lod behavior.
"""
from __future__ import annotations

import argparse
import json
import math
import random
import sys
from pathlib import Path

import bpy
from mathutils import Vector


PIPELINE_VERSION = 1
TREE_HEIGHT = 16.0
TREE_RADIUS = 4.2
# Tier/branch/spray counts are deliberately explicit so geometry budgets are
# deterministic and can be compared in CI.
LOD_PRESETS = {
    "LOD0": {"tiers": 10, "branches": 8, "segments": 5, "sprays": 7, "sides": 6},
    "LOD1": {"tiers": 7, "branches": 6, "segments": 3, "sprays": 4, "sides": 5},
    "LOD2": {"tiers": 4, "branches": 5, "segments": 2, "sprays": 2, "sides": 4},
    "LOD3": {"tiers": 2, "branches": 4, "segments": 1, "sprays": 1, "sides": 4},
}


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--texture-root", type=Path, default=Path("assets/textures/environment"))
    parser.add_argument("--seed", type=int, default=1939)
    parser.add_argument("--height", type=float, default=TREE_HEIGHT)
    parser.add_argument("--radius", type=float, default=TREE_RADIUS)
    parser.add_argument("--only-lods", nargs="+", choices=LOD_PRESETS.keys(),
                        default=list(LOD_PRESETS.keys()))
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])


class MeshBuilder:
    def __init__(self):
        self.vertices = []
        self.faces = []

    def tube(self, start, end, radius_start, radius_end, sides=5, bend=None):
        """Append a tapered, capped tube between two points."""
        a, b = Vector(start), Vector(end)
        axis = (b - a).normalized()
        reference = Vector((0.0, 0.0, 1.0))
        if abs(axis.dot(reference)) > 0.92:
            reference = Vector((1.0, 0.0, 0.0))
        u = axis.cross(reference).normalized()
        v = axis.cross(u).normalized()
        base = len(self.vertices)
        for center, radius in ((a, radius_start), (b, radius_end)):
            for side in range(sides):
                angle = 2.0 * math.pi * side / sides
                point = center + radius * (math.cos(angle) * u + math.sin(angle) * v)
                self.vertices.append(tuple(point))
        self.faces.append(tuple(base + i for i in reversed(range(sides))))
        for side in range(sides):
            nxt = (side + 1) % sides
            self.faces.append((base + side, base + nxt, base + sides + nxt, base + sides + side))
        self.faces.append(tuple(base + sides + i for i in range(sides)))

    def cone(self, start, end, radius, sides=4):
        self.tube(start, end, radius, 0.008, sides=sides)

    def make_object(self, name, material):
        mesh = bpy.data.meshes.new(name + "Mesh")
        mesh.from_pydata(self.vertices, [], self.faces)
        mesh.materials.append(material)
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        return obj


def load_map(texture_root, stem, color_space):
    path = texture_root / stem
    if not path.is_file():
        return None
    image = bpy.data.images.load(str(path.resolve()), check_existing=True)
    image.colorspace_settings.name = color_space
    image.pack()
    return image


def make_material(name, texture_root, albedo_name, normal_name, roughness_name,
                  fallback_color, roughness_value):
    material = bpy.data.materials.new(name)
    material.diffuse_color = (*fallback_color, 1.0)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    principled = nodes.get("Principled BSDF")
    principled.inputs["Base Color"].default_value = (*fallback_color, 1.0)
    principled.inputs["Roughness"].default_value = roughness_value

    albedo = load_map(texture_root, albedo_name, "sRGB")
    if albedo:
        node = nodes.new("ShaderNodeTexImage")
        node.image = albedo
        node.label = "Talvisota base colour (sRGB)"
        links.new(node.outputs["Color"], principled.inputs["Base Color"])

    roughness = load_map(texture_root, roughness_name, "Non-Color")
    if roughness:
        node = nodes.new("ShaderNodeTexImage")
        node.image = roughness
        node.label = "Talvisota roughness (linear)"
        links.new(node.outputs["Color"], principled.inputs["Roughness"])

    normal = load_map(texture_root, normal_name, "Non-Color")
    if normal:
        texture = nodes.new("ShaderNodeTexImage")
        texture.image = normal
        texture.label = "Talvisota tangent-space normal (Non-Color)"
        normal_node = nodes.new("ShaderNodeNormalMap")
        normal_node.inputs["Strength"].default_value = 0.8
        links.new(texture.outputs["Color"], normal_node.inputs["Color"])
        links.new(normal_node.outputs["Normal"], principled.inputs["Normal"])
    return material


def create_materials(texture_root):
    return {
        "bark": make_material(
            "Scots Pine | Bark", texture_root, "bark-albedo.bmp",
            "bark-normal.bmp", "bark-roughness.bmp", (0.19, 0.105, 0.055), 0.88),
        "needles": make_material(
            "Scots Pine | Needles", texture_root, "pine_needles-albedo.bmp",
            "pine_needles-normal.bmp", "pine_needles-roughness.bmp",
            (0.075, 0.19, 0.075), 0.82),
        "snow": make_material(
            "Scots Pine | Snow", texture_root, "snow-albedo.bmp",
            "snow-normal.bmp", "snow-roughness.bmp", (0.78, 0.84, 0.9), 0.94),
    }


def add_spray(builder, origin, direction, length, rng, sides):
    direction = Vector(direction).normalized()
    start = Vector(origin)
    # A spray is a small tapered bough; four asymmetric side shoots keep the
    # silhouette irregular instead of producing repeated geometric stars.
    tip = start + direction * length
    builder.tube(start, tip, length * 0.095, 0.006, sides=sides)
    axis = direction.cross(Vector((0, 0, 1)))
    if axis.length < 0.1:
        axis = direction.cross(Vector((0, 1, 0)))
    axis.normalize()
    other = direction.cross(axis).normalized()
    for index in range(4):
        t = 0.24 + index * 0.16
        center = start.lerp(tip, t)
        side = axis if index % 2 == 0 else other
        sign = -1.0 if index % 3 == 0 else 1.0
        shoot = (direction * 0.48 + side * sign * 0.78 +
                 Vector((0, 0, 0.12 + rng.random() * 0.16))).normalized()
        shoot_length = length * (0.34 - index * 0.035)
        builder.tube(center, center + shoot * shoot_length,
                     shoot_length * 0.12, 0.003, sides=sides)


def build_tree(lod_name, preset, materials, height, crown_radius, seed):
    rng = random.Random(seed)
    trunk = MeshBuilder()
    foliage = MeshBuilder()
    snow = MeshBuilder()
    tiers = preset["tiers"]
    branches = preset["branches"]
    segments = preset["segments"]
    sprays = preset["sprays"]
    sides = preset["sides"]

    # Tapered, slightly wandering trunk. Small deterministic lean prevents a
    # mathematically perfect vertical silhouette.
    trunk_base = (0.0, 0.0, 0.0)
    trunk_top = (0.12, -0.04, height)
    trunk.tube(trunk_base, trunk_top, height * 0.022, height * 0.005,
               sides=max(6, sides + 2))

    for tier in range(tiers):
        fraction = 0.19 + 0.77 * (tier / max(1, tiers - 1))
        z = height * fraction
        normalized = (1.0 - fraction)
        # Scots pine has a broad, irregular lower crown and a narrower top.
        tier_radius = crown_radius * (0.20 + 0.80 * math.sqrt(normalized))
        phase = (tier % 2) * (math.pi / branches) + rng.uniform(-0.10, 0.10)
        for branch_index in range(branches):
            angle = phase + 2.0 * math.pi * branch_index / branches
            jitter = rng.uniform(0.82, 1.16)
            length = tier_radius * jitter
            direction = Vector((math.cos(angle), math.sin(angle),
                                0.20 + 0.30 * fraction)).normalized()
            start = Vector((0.0, 0.0, z))
            previous = start
            for segment in range(segments):
                t = (segment + 1) / segments
                point = start + direction * (length * t)
                point.z += (t * t) * (0.38 + 0.55 * fraction)
                radius = max(0.012, height * 0.0065 * (1.0 - t) * (1.0 - fraction * 0.35))
                trunk.tube(previous, point, radius, max(0.006, radius * 0.62),
                           sides=sides)
                previous = point
            if sprays:
                for spray_index in range(sprays):
                    t = 0.22 + 0.72 * (spray_index / max(1, sprays - 1))
                    origin = start + direction * (length * t)
                    origin.z += (t * t) * (0.38 + 0.55 * fraction)
                    lateral = Vector((-math.sin(angle), math.cos(angle), 0.0))
                    spray_direction = (direction * 0.42 +
                                       Vector((0.0, 0.0, 0.52)) +
                                       lateral * rng.uniform(-0.38, 0.38))
                    spray_length = max(0.12, length * (0.18 if fraction < 0.45 else 0.13))
                    add_spray(foliage, origin, spray_direction, spray_length, rng, sides)
            # Snow accumulation is sparse and concentrated on upper surfaces.
            if fraction > 0.42 and tier % 2 == 0 and rng.random() < 0.34:
                snow_start = start + direction * (length * 0.58)
                snow_start.z += 0.28
                snow_end = snow_start + direction * (length * 0.12) + Vector((0, 0, 0.08))
                snow.tube(snow_start, snow_end, max(0.018, height * 0.0018),
                          0.006, sides=max(3, sides - 1))

    trunk_obj = trunk.make_object("Trunk_Branches_" + lod_name, materials["bark"])
    foliage_obj = foliage.make_object("Needle_Sprays_" + lod_name, materials["needles"])
    objects = [trunk_obj, foliage_obj]
    if snow.faces:
        objects.append(snow.make_object("Snow_Accents_" + lod_name, materials["snow"]))

    # Join geometry by material to minimize object/submesh count. This is
    # intentionally per-LOD, not one world-sized combined forest mesh.
    for obj in objects:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(island_margin=0.012)
        bpy.ops.object.mode_set(mode="OBJECT")
        obj.select_set(False)
        obj.data.validate(verbose=False)
        obj.data.update()
    return objects


def export_lod(objects, filepath):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.gltf(
        filepath=str(filepath), export_format="GLB", use_selection=True,
        export_apply=True, export_texcoords=True, export_normals=True,
        export_materials="EXPORT", export_image_format="AUTO",
    )
    bpy.ops.object.select_all(action="DESELECT")


def main():
    args = parse_args()
    if args.height <= 0 or args.radius <= 0:
        raise SystemExit("--height and --radius must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    materials = create_materials(args.texture_root)
    all_objects = []
    manifest = {
        "schema": "talvisota.tree-asset.v1",
        "pipeline_version": PIPELINE_VERSION,
        "asset": "scots_pine",
        "species": "Pinus sylvestris",
        "seed": args.seed,
        "units": "metres",
        "axis": {"up": "+Y", "east": "+X", "north": "+Z"},
        "source": "scots_pine.blend",
        "lods": {},
        "material_maps": {
            "bark": ["bark-albedo.bmp", "bark-normal.bmp", "bark-roughness.bmp"],
            "needles": ["pine_needles-albedo.bmp", "pine_needles-normal.bmp",
                        "pine_needles-roughness.bmp"],
            "snow": ["snow-albedo.bmp", "snow-normal.bmp", "snow-roughness.bmp"],
        },
        "runtime_note": "Select LOD by projected screen size/distance in Talvisota; files are separate because glTF core has no universal LOD selector.",
    }
    for lod_name in args.only_lods:
        preset = LOD_PRESETS[lod_name]
        collection = bpy.data.collections.new(lod_name)
        bpy.context.scene.collection.children.link(collection)
        bpy.context.view_layer.active_layer_collection = bpy.context.view_layer.layer_collection.children[collection.name]
        objects = build_tree(lod_name, preset, materials, args.height, args.radius,
                             args.seed + list(LOD_PRESETS).index(lod_name) * 1009)
        for obj in objects:
            for owner in list(obj.users_collection):
                owner.objects.unlink(obj)
            collection.objects.link(obj)
            all_objects.append(obj)
        filepath = args.output / ("scots_pine_" + lod_name.lower() + ".glb")
        export_lod(objects, filepath)
        vertex_count = sum(len(obj.data.vertices) for obj in objects)
        triangle_count = sum(len(poly.vertices) - 2 for obj in objects for poly in obj.data.polygons)
        manifest["lods"][lod_name] = {
            "file": filepath.name,
            "vertices": vertex_count,
            "triangles": triangle_count,
            "objects": len(objects),
            "screen_coverage_hint": {"LOD0": 0.35, "LOD1": 0.12, "LOD2": 0.035, "LOD3": 0.0}[lod_name],
        }
    bpy.context.view_layer.active_layer_collection = bpy.context.view_layer.layer_collection
    blend_path = args.output / "scots_pine.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))
    manifest_path = args.output / "scots_pine.asset.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")
    print("TALVISOTA_TREE_PIPELINE_OK")
    print(json.dumps(manifest["lods"], sort_keys=True))


if __name__ == "__main__":
    main()
