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
MATERIAL_MAP_STATUS = {}
TREE_HEIGHT = 18.0
TREE_RADIUS = 6.0
# Mature, open-grown Scots pine proportions. The lower bole remains visible;
# crown width peaks below mid-height and contracts into an irregular upper crown.
LOD_PRESETS = {
    "LOD0": {"whorls": 11, "primary": 5, "secondary": 5, "tertiary": 5, "needle_clusters": 3, "sides": 6},
    "LOD1": {"whorls": 9, "primary": 4, "secondary": 4, "tertiary": 4, "needle_clusters": 2, "sides": 5},
    "LOD2": {"whorls": 6, "primary": 4, "secondary": 3, "tertiary": 3, "needle_clusters": 2, "sides": 4},
    "LOD3": {"whorls": 4, "primary": 3, "secondary": 2, "tertiary": 2, "needle_clusters": 1, "sides": 4},
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
        self.face_uvs = []
        self.face_material_indices = []

    def tube(self, start, end, radius_start, radius_end, sides=5, bend=None, material_index=0):
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
        self.face_uvs.append([(0.5, 0.5)] * sides)
        self.face_material_indices.append(material_index)
        v_scale = max(0.1, (b - a).length / max(radius_start + radius_end, 0.01))
        for side in range(sides):
            nxt = (side + 1) % sides
            self.faces.append((base + side, base + nxt, base + sides + nxt, base + sides + side))
            self.face_uvs.append([
                (side / sides, 0.0),
                ((side + 1) / sides, 0.0),
                ((side + 1) / sides, v_scale),
                (side / sides, v_scale),
            ])
            self.face_material_indices.append(material_index)
        self.faces.append(tuple(base + sides + i for i in range(sides)))
        self.face_uvs.append([(0.5, 0.5)] * sides)
        self.face_material_indices.append(material_index)

    def path_tube(self, points, radii, sides=5, material_indices=None):
        """Create one continuous tapered tube with parallel-transported frames."""
        centers = [Vector(point) for point in points]
        if len(centers) < 2 or len(centers) != len(radii):
            raise ValueError("path_tube requires matching point/radius arrays")
        if material_indices is None:
            material_indices = [0] * (len(centers) - 1)
        if len(material_indices) != len(centers) - 1:
            raise ValueError("path_tube material indices must match its segments")
        tangents = []
        for index in range(len(centers)):
            if index == 0:
                tangent = centers[1] - centers[0]
            elif index == len(centers) - 1:
                tangent = centers[-1] - centers[-2]
            else:
                tangent = centers[index + 1] - centers[index - 1]
            if tangent.length < 1e-8:
                raise ValueError("path_tube contains coincident control points")
            tangents.append(tangent.normalized())

        reference = Vector((0.0, 0.0, 1.0))
        if abs(tangents[0].dot(reference)) > 0.92:
            reference = Vector((1.0, 0.0, 0.0))
        normal = tangents[0].cross(reference).normalized()
        rings = []
        cumulative = [0.0]
        for index in range(1, len(centers)):
            cumulative.append(cumulative[-1] + (centers[index] - centers[index - 1]).length)
        uv_scale = max(radii[0] + radii[-1], 0.01)
        for index, (center, radius, tangent) in enumerate(zip(centers, radii, tangents)):
            if index:
                normal = normal - tangent * normal.dot(tangent)
                if normal.length < 1e-8:
                    normal = tangent.cross(Vector((0.0, 1.0, 0.0)))
                normal.normalize()
            binormal = tangent.cross(normal).normalized()
            ring = []
            for side in range(sides):
                angle = 2.0 * math.pi * side / sides
                point = center + radius * (math.cos(angle) * normal +
                                           math.sin(angle) * binormal)
                ring.append(len(self.vertices))
                self.vertices.append(tuple(point))
            rings.append(ring)

        first = rings[0]
        self.faces.append(tuple(first[i] for i in reversed(range(sides))))
        self.face_uvs.append([(0.5, 0.5)] * sides)
        self.face_material_indices.append(material_indices[0])
        for ring_index in range(len(rings) - 1):
            v0 = cumulative[ring_index] / uv_scale
            v1 = cumulative[ring_index + 1] / uv_scale
            for side in range(sides):
                nxt = (side + 1) % sides
                self.faces.append((rings[ring_index][side], rings[ring_index][nxt],
                                   rings[ring_index + 1][nxt], rings[ring_index + 1][side]))
                self.face_uvs.append([
                    (side / sides, v0), ((side + 1) / sides, v0),
                    ((side + 1) / sides, v1), (side / sides, v1),
                ])
                self.face_material_indices.append(material_indices[ring_index])
        last = rings[-1]
        self.faces.append(tuple(last[i] for i in range(sides)))
        self.face_uvs.append([(0.5, 0.5)] * sides)
        self.face_material_indices.append(material_indices[-1])

    def cone(self, start, end, radius, sides=4):
        self.tube(start, end, radius, 0.008, sides=sides)

    def make_object(self, name, material):
        mesh = bpy.data.meshes.new(name + "Mesh")
        mesh.from_pydata(self.vertices, [], self.faces)
        material_list = list(material) if isinstance(material, (list, tuple)) else [material]
        for slot in material_list:
            mesh.materials.append(slot)
        uv_layer = mesh.uv_layers.new(name="UVMap")
        for polygon, coordinates, material_index in zip(
                mesh.polygons, self.face_uvs, self.face_material_indices):
            polygon.material_index = material_index
            for loop_index, uv in zip(polygon.loop_indices, coordinates):
                uv_layer.data[loop_index].uv = uv
            polygon.use_smooth = True
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        return obj


def load_map(texture_root, stem, color_space):
    path = texture_root / stem
    MATERIAL_MAP_STATUS[stem] = path.is_file()
    if not path.is_file():
        return None
    image = bpy.data.images.load(str(path.resolve()), check_existing=True)
    image.colorspace_settings.name = color_space
    image.pack()
    return image


def make_material(name, texture_root, albedo_name, normal_name, roughness_name,
                  fallback_color, roughness_value, tint_color=None):
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
        if tint_color:
            tint = nodes.new("ShaderNodeRGB")
            tint.outputs[0].default_value = (*tint_color, 1.0)
            multiply = nodes.new("ShaderNodeMixRGB")
            multiply.blend_type = "MULTIPLY"
            multiply.inputs[0].default_value = 1.0
            links.new(node.outputs["Color"], multiply.inputs[1])
            links.new(tint.outputs[0], multiply.inputs[2])
            links.new(multiply.outputs["Color"], principled.inputs["Base Color"])
        else:
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
        "bark_lower": make_material(
            "Scots Pine | Lower grey-brown plated bark", texture_root, "bark-albedo.bmp",
            "bark-normal.bmp", "bark-roughness.bmp", (0.24, 0.18, 0.12), 0.92,
            tint_color=(0.76, 0.70, 0.62)),
        "bark_upper": make_material(
            "Scots Pine | Upper orange-yellow smooth bark", texture_root, "bark-albedo.bmp",
            "bark-normal.bmp", "bark-roughness.bmp", (0.48, 0.27, 0.12), 0.76,
            tint_color=(1.0, 0.72, 0.42)),
        "needles": make_material(
            "Scots Pine | Needles", texture_root, "pine_needles-albedo.bmp",
            "pine_needles-normal.bmp", "pine_needles-roughness.bmp",
            (0.075, 0.19, 0.075), 0.82),
        "snow": make_material(
            "Scots Pine | Snow", texture_root, "snow-albedo.bmp",
            "snow-normal.bmp", "snow-roughness.bmp", (0.78, 0.84, 0.9), 0.94),
    }


def add_path(builder, points, radii, sides, material_indices=None):
    """Build one continuous, smoothly tapered branch or trunk section."""
    builder.path_tube(points, radii, sides=sides, material_indices=material_indices)


def add_needle_pair(builder, origin, direction, rng):
    """Create one visible, individually modelled Scots-pine needle fascicle."""
    axis = Vector(direction).normalized()
    reference = Vector((0.0, 0.0, 1.0))
    if abs(axis.dot(reference)) > 0.94:
        reference = Vector((0.0, 1.0, 0.0))
    side = axis.cross(reference).normalized()
    length = rng.uniform(0.052, 0.082)
    for sign in (-1.0, 1.0):
        spread = side * sign * rng.uniform(0.16, 0.34)
        lift = Vector((0.0, 0.0, rng.uniform(0.12, 0.32)))
        needle_axis = (axis * 0.72 + spread + lift).normalized()
        tip = Vector(origin) + needle_axis * length
        # Three-sided needles retain a tapered profile at a modest triangle cost.
        builder.tube(origin, tip, 0.0022, 0.00055, sides=3)


def add_needle_spray(builder, origin, direction, rng, pairs=3):
    """Build a compact radial spray of paired needles around a terminal shoot."""
    axis = Vector(direction).normalized()
    reference = Vector((0.0, 0.0, 1.0))
    if abs(axis.dot(reference)) > 0.94:
        reference = Vector((0.0, 1.0, 0.0))
    side = axis.cross(reference).normalized()
    up = axis.cross(side).normalized()
    for index in range(pairs):
        angle = (2.399963229728653 * index) + rng.uniform(-0.55, 0.55)
        radial = side * math.cos(angle) + up * math.sin(angle)
        spray_axis = (axis * rng.uniform(0.30, 0.62) +
                      radial * rng.uniform(0.55, 1.0) +
                      Vector((0.0, 0.0, rng.uniform(0.08, 0.38)))).normalized()
        add_needle_pair(builder, origin, spray_axis, rng)


def build_tree(lod_name, preset, materials, height, crown_radius, seed):
    rng = random.Random(seed)
    trunk = MeshBuilder()
    foliage = MeshBuilder()
    snow = MeshBuilder()
    whorls = preset["whorls"]
    primary_count = preset["primary"]
    secondary_count = preset["secondary"]
    tertiary_count = preset["tertiary"]
    needle_clusters = preset["needle_clusters"]
    sides = preset["sides"]

    # Mature pine: a substantial clear lower bole, with a broad crown above it.
    trunk_points = []
    trunk_radii = []
    trunk_segments = max(12, whorls + 5)
    lean_x, lean_y = rng.uniform(-0.22, 0.22), rng.uniform(-0.18, 0.18)
    for index in range(trunk_segments + 1):
        t = index / trunk_segments
        trunk_points.append(Vector((lean_x * t * t + 0.035 * math.sin(t * 5.2),
                                    lean_y * t * t + 0.025 * math.sin(t * 3.7),
                                    height * t)))
        # Flared root collar, strong lower bole, then progressive taper.
        radius = height * (0.018 * (1.0 - t) ** 1.28 + 0.0024)
        if t < 0.09:
            radius *= 1.0 + 0.34 * (1.0 - t / 0.09)
        trunk_radii.append(radius)
    trunk_zones = [0 if (index + 0.5) / trunk_segments < 0.58 else 1
                   for index in range(trunk_segments)]
    add_path(trunk, trunk_points, trunk_radii, max(8, sides + 2), trunk_zones)

    # Irregular whorl spacing and azimuth prevent repeated geometric rings.
    crown_start, crown_end = 0.43, 0.94
    whorl_heights = []
    for whorl in range(whorls):
        t = whorl / max(1, whorls - 1)
        base = crown_start + (crown_end - crown_start) * t
        whorl_heights.append(base + rng.uniform(-0.018, 0.018))
    whorl_heights.sort()

    for whorl_index, fraction in enumerate(whorl_heights):
        z = height * fraction
        crown_profile = math.sin(math.pi * ((fraction - crown_start) /
                                             (crown_end - crown_start)) * 0.78)
        length_base = crown_radius * (0.48 + 0.52 * max(0.0, crown_profile))
        count = max(3, primary_count + rng.choice((-1, 0, 0, 1)))
        phase = whorl_index * 2.399963229728653 + rng.uniform(-0.34, 0.34)
        for branch_index in range(count):
            angle = phase + 2.0 * math.pi * branch_index / count + rng.uniform(-0.22, 0.22)
            radial = Vector((math.cos(angle), math.sin(angle), 0.0))
            length = length_base * rng.uniform(0.72, 1.18)
            start = Vector((lean_x * fraction * fraction,
                            lean_y * fraction * fraction, z))
            # Scaffold limbs droop under their own weight, then sweep outward.
            primary_points = []
            for step in range(5):
                t = step / 4.0
                outward = length * t
                vertical = height * (0.018 * t - 0.075 * t * (1.0 - t))
                vertical -= height * max(0.0, fraction - 0.78) * 0.06 * t
                point = start + radial * outward + Vector((0.0, 0.0, vertical))
                point += Vector((math.sin(angle * 2.1) * 0.10 * t * t,
                                 math.cos(angle * 1.7) * 0.08 * t * t, 0.0))
                primary_points.append(point)
            base_radius = height * (0.0105 * (1.0 - fraction * 0.40))
            add_path(trunk, primary_points,
                     [base_radius * (1.0 - 0.91 * (i / 4.0)) for i in range(5)],
                     sides, material_indices=[1] * 4)

            # Secondary limbs split from the scaffold at uneven intervals and angles.
            for secondary_index in range(secondary_count):
                t = rng.uniform(0.18, 0.96)
                segment = min(3, int(t * 4))
                local_t = t * 4 - segment
                origin = primary_points[segment].lerp(primary_points[segment + 1], local_t)
                sign = rng.choice((-1.0, 1.0))
                tangent = (primary_points[min(4, segment + 1)] -
                           primary_points[max(0, segment)]).normalized()
                lateral = Vector((-radial.y, radial.x, 0.0))
                direction = (radial * rng.uniform(0.16, 0.52) +
                             lateral * sign * rng.uniform(0.42, 0.96) +
                             Vector((0.0, 0.0, rng.uniform(0.12, 0.62)))).normalized()
                secondary_length = length * rng.uniform(0.22, 0.48) * (1.0 - 0.20 * t)
                secondary_points = [
                    origin,
                    origin + direction * (secondary_length * 0.48) + tangent * (secondary_length * 0.10),
                    origin + direction * secondary_length +
                    Vector((0.0, 0.0, secondary_length * rng.uniform(-0.02, 0.18))),
                ]
                secondary_radius = base_radius * rng.uniform(0.19, 0.32)
                add_path(trunk, secondary_points,
                         [secondary_radius, secondary_radius * 0.48, 0.0022],
                         max(4, sides - 1), material_indices=[1, 1])

                # Terminal shoots are distributed along the full secondary limb,
                # not bunched near its attachment point. Each shoot carries
                # several dense fascicle sprays so the crown reads as foliage.
                for twig_index in range(tertiary_count):
                    twig_t = 0.24 + 0.70 * ((twig_index + rng.uniform(0.12, 0.88)) /
                                             max(1, tertiary_count))
                    if twig_t <= 0.48:
                        twig_origin = secondary_points[0].lerp(secondary_points[1], twig_t / 0.48)
                    else:
                        twig_origin = secondary_points[1].lerp(
                            secondary_points[2], (twig_t - 0.48) / 0.52)
                    twig_direction = (direction * rng.uniform(0.18, 0.48) +
                                      lateral * rng.uniform(-0.92, 0.92) +
                                      Vector((0.0, 0.0, rng.uniform(0.24, 0.92)))).normalized()
                    twig_length = secondary_length * rng.uniform(0.22, 0.42)
                    twig_mid = twig_origin + twig_direction * (twig_length * 0.56) + tangent * (twig_length * 0.08)
                    twig_tip = twig_origin + twig_direction * twig_length
                    trunk.path_tube([twig_origin, twig_mid, twig_tip],
                                    [0.0028, 0.0017, 0.00065],
                                    sides=3, material_indices=[1, 1])
                    for cluster in range(needle_clusters):
                        cluster_t = 0.18 + 0.76 * ((cluster + rng.uniform(0.12, 0.88)) /
                                                   max(1, needle_clusters))
                        cluster_origin = twig_origin.lerp(twig_tip, cluster_t)
                        add_needle_spray(foliage, cluster_origin, twig_direction, rng, pairs=3)

            # Sparse snow accents sit on upper scaffold sections only.
            if fraction > 0.56 and rng.random() < 0.38:
                snow_start = primary_points[2]
                snow_end = primary_points[3].lerp(primary_points[4], 0.35)
                snow.tube(snow_start, snow_end, height * 0.0022, 0.004,
                          sides=max(3, sides - 1))

    trunk_obj = trunk.make_object("Trunk_Scaffold_And_Twigs_" + lod_name,
                                   [materials["bark_lower"], materials["bark_upper"]])
    objects = [trunk_obj]
    if foliage.faces:
        objects.append(foliage.make_object("Paired_Pine_Needles_" + lod_name, materials["needles"]))
    if snow.faces:
        objects.append(snow.make_object("Snow_Load_" + lod_name, materials["snow"]))

    for obj in objects:
        # UV coordinates are authored by MeshBuilder per tube; no interactive
        # UV-editor context is required in headless Blender.
        obj.data.validate(verbose=False)
        obj.data.update()
    return objects


def export_lod(objects, filepath):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    if not hasattr(bpy.ops.export_scene, "gltf"):
        try:
            bpy.ops.preferences.addon_enable(module="io_scene_gltf2")
        except RuntimeError:
            pass
    if not hasattr(bpy.ops.export_scene, "gltf"):
        raise RuntimeError("Blender glTF 2.0 exporter is not installed or enabled")
    bpy.ops.export_scene.gltf(
        filepath=str(filepath), export_format="GLB", use_selection=True,
        export_apply=True, export_texcoords=True, export_normals=True,
        export_materials="EXPORT", export_image_format="AUTO",
    )
    if not filepath.is_file() or filepath.stat().st_size < 128:
        raise RuntimeError("glTF exporter did not produce a valid GLB: " + str(filepath))
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
        "asset": "scots_pine_mature_open_crown",
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
        "growth_form": "mature_open_grown_broad_crowned_scots_pine",
        "material_zones": {
            "lower_bole": {"material": "Scots Pine | Lower grey-brown plated bark", "height_fraction": [0.0, 0.58]},
            "upper_trunk_and_scaffold": {"material": "Scots Pine | Upper orange-yellow smooth bark", "height_fraction": [0.58, 1.0]},
        },
        "geometry_note": "Scaffold limbs, secondary branches, terminal shoots and dense radial sprays of individually modelled paired 5-8 cm needles are generated as a deterministic hierarchy; this is a structural asset, not a photogrammetry scan.",
        "runtime_note": "Select LOD by projected screen size/distance in Talvisota; files are separate because glTF core has no universal LOD selector.",
    }
    manifest["material_map_status"] = MATERIAL_MAP_STATUS
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
