#!/usr/bin/env python3
"""Ingest a real photogrammetry tree scan and produce textured runtime LODs.

This path deliberately does not synthesize geometry or materials. The source
must be an externally captured/reconstructed GLB, FBX or OBJ with UVs and
image-textured materials. It records the source SHA-256 for provenance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True,
                        help="Photogrammetry export (.glb/.gltf/.fbx/.obj)")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target-height", type=float, default=0.0,
                        help="Optional target height in metres; 0 preserves source scale")
    parser.add_argument("--lod-ratios", nargs=3, type=float,
                        default=(0.60, 0.25, 0.10),
                        help="Collapse-decimation ratios for bark/woody meshes")
    parser.add_argument("--foliage-lod-ratios", nargs=3, type=float,
                        default=(0.85, 0.65, 0.40),
                        help="More conservative ratios for needle/leaf meshes to preserve silhouette")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])


def import_source(path: Path):
    extension = path.suffix.lower()
    if extension in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=str(path))
    elif extension == ".fbx":
        bpy.ops.import_scene.fbx(filepath=str(path))
    elif extension == ".obj":
        if hasattr(bpy.ops.wm, "obj_import"):
            bpy.ops.wm.obj_import(filepath=str(path))
        else:
            try:
                bpy.ops.preferences.addon_enable(module="io_scene_obj")
            except RuntimeError:
                pass
            if not hasattr(bpy.ops.import_scene, "obj"):
                raise RuntimeError("Blender OBJ importer is unavailable")
            bpy.ops.import_scene.obj(filepath=str(path))
    else:
        raise RuntimeError("Supported scan formats are GLB, glTF, FBX and OBJ")


def triangles(obj):
    return sum(max(0, len(poly.vertices) - 2) for poly in obj.data.polygons)


def material_image_details(objects):
    images = {}
    for obj in objects:
        for material in obj.data.materials:
            if not material or not material.use_nodes:
                continue
            for node in material.node_tree.nodes:
                if node.type == "TEX_IMAGE" and node.image:
                    image = node.image
                    images[image.name] = {
                        "width": int(image.size[0]),
                        "height": int(image.size[1]),
                        "channels": int(image.channels),
                        "source": image.source,
                        "color_space": image.colorspace_settings.name,
                    }
    return dict(sorted(images.items()))


def is_foliage_mesh(obj):
    """Keep more source triangles on needle/leaf surfaces during LOD reduction."""
    foliage_tokens = ("needle", "foliage", "leaf", "leaves")
    material_names = [material.name.lower() for material in obj.data.materials if material]
    return any(token in name for name in material_names for token in foliage_tokens)


def export_glb(objects, filepath):
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
        raise RuntimeError("Blender glTF exporter is unavailable")
    bpy.ops.export_scene.gltf(
        filepath=str(filepath), export_format="GLB", use_selection=True,
        export_apply=True, export_texcoords=True, export_normals=True,
        export_materials="EXPORT", export_image_format="AUTO",
    )
    if not filepath.is_file() or filepath.stat().st_size < 128:
        raise RuntimeError("GLB export failed: " + str(filepath))
    bpy.ops.object.select_all(action="DESELECT")


def main():
    args = parse_args()
    source = args.source.resolve()
    if not source.is_file():
        raise SystemExit("Photogrammetry source does not exist: " + str(source))
    if args.target_height < 0:
        raise SystemExit("--target-height must be zero or positive")
    if any(not 0.0 < ratio < 1.0 for ratio in (*args.lod_ratios, *args.foliage_lod_ratios)):
        raise SystemExit("Every LOD ratio must be strictly between 0 and 1")
    if list(args.lod_ratios) != sorted(args.lod_ratios, reverse=True):
        raise SystemExit("LOD ratios must decrease: LOD1 > LOD2 > LOD3")
    if list(args.foliage_lod_ratios) != sorted(args.foliage_lod_ratios, reverse=True):
        raise SystemExit("Foliage LOD ratios must decrease: LOD1 > LOD2 > LOD3")

    source_hash = sha256_file(source)
    args.output.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    import_source(source)
    source_objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if not source_objects:
        raise SystemExit("Imported scan contains no mesh objects")

    # Preserve world-space placement while making the source independent of
    # importer-created parent/collection transforms.
    world_matrices = {obj.name: obj.matrix_world.copy() for obj in source_objects}
    for obj in source_objects:
        obj.parent = None
        obj.matrix_world = world_matrices[obj.name]

    min_z = min((obj.matrix_world @ Vector(corner)).z
                for obj in source_objects for corner in obj.bound_box)
    max_z = max((obj.matrix_world @ Vector(corner)).z
                for obj in source_objects for corner in obj.bound_box)
    source_height = max_z - min_z
    if source_height <= 0.01:
        raise SystemExit("Imported scan has invalid or near-zero height")
    scale_factor = args.target_height / source_height if args.target_height > 0 else 1.0
    if scale_factor != 1.0:
        scale_matrix = Matrix.Scale(scale_factor, 4)
        for obj in source_objects:
            obj.matrix_world = scale_matrix @ obj.matrix_world

    missing_uv = [obj.name for obj in source_objects if len(obj.data.uv_layers) == 0]
    missing_material = [obj.name for obj in source_objects if len(obj.data.materials) == 0]
    image_details = material_image_details(source_objects)
    image_names = sorted(image_details)
    if missing_uv:
        raise SystemExit("Photogrammetry meshes missing UVs: " + ", ".join(missing_uv))
    if missing_material:
        raise SystemExit("Photogrammetry meshes missing materials: " + ", ".join(missing_material))
    if not image_names:
        raise SystemExit("No image-textured material found; refusing to label an untextured model as a scan")

    # Pack source image files so exported GLBs are self-contained where Blender
    # can resolve the original image paths.
    packed_images = []
    unresolved_images = []
    for image in bpy.data.images:
        if image.source != "FILE":
            continue
        try:
            if not image.packed_file and image.filepath and Path(bpy.path.abspath(image.filepath)).is_file():
                image.pack()
            if image.packed_file:
                packed_images.append(image.name)
            else:
                unresolved_images.append(image.name)
        except RuntimeError:
            unresolved_images.append(image.name)

    if unresolved_images:
        raise SystemExit("Photogrammetry textures could not be packed; refusing incomplete GLB exports: " +
                         ", ".join(sorted(unresolved_images)))
    if any(details["width"] < 1 or details["height"] < 1
           for details in image_details.values()):
        raise SystemExit("Photogrammetry source contains an image with invalid dimensions")

    for name, details in image_details.items():
        details["packed"] = name in packed_images
        details["resolution_class"] = (
            "4k+" if max(details["width"], details["height"]) >= 4096 else
            "2k+" if max(details["width"], details["height"]) >= 2048 else
            "sub-2k"
        )

    source_mesh_details = []
    for obj in source_objects:
        dimensions = obj.dimensions
        uv_layer = obj.data.uv_layers.active
        degenerate_uv_faces = 0
        if uv_layer:
            for polygon in obj.data.polygons:
                loops = list(polygon.loop_indices)
                if len(loops) < 3:
                    degenerate_uv_faces += 1
                    continue
                coords = [uv_layer.data[index].uv for index in loops]
                area_twice = sum(
                    coords[index].x * coords[(index + 1) % len(coords)].y -
                    coords[(index + 1) % len(coords)].x * coords[index].y
                    for index in range(len(coords))
                )
                if abs(area_twice) <= 1.0e-12:
                    degenerate_uv_faces += 1
        source_mesh_details.append({
            "name": obj.name,
            "vertices": len(obj.data.vertices),
            "triangles": triangles(obj),
            "materials": [material.name for material in obj.data.materials if material],
            "uv_layers": len(obj.data.uv_layers),
            "degenerate_uv_faces": degenerate_uv_faces,
            "dimensions_metres": [float(dimensions.x), float(dimensions.y), float(dimensions.z)],
        })

    original_triangles = sum(triangles(obj) for obj in source_objects)
    if original_triangles < 1000:
        raise SystemExit("Source scan has fewer than 1,000 triangles; expected detailed captured geometry")

    manifest = {
        "schema": "talvisota.scanned-tree.v1",
        "asset": source.stem,
        "source_file": source.name,
        "source_sha256": source_hash,
        "source_bytes": source.stat().st_size,
        "source_height_metres": source_height,
        "target_height_metres": args.target_height if args.target_height > 0 else source_height,
        "applied_scale": scale_factor,
        "provenance": "externally supplied photogrammetry/reconstruction source; geometry and materials are not procedurally synthesized",
        "source_meshes": len(source_objects),
        "source_mesh_details": source_mesh_details,
        "source_triangles": original_triangles,
        "source_texture_resolution": {
            "minimum_max_dimension": min(max(item["width"], item["height"]) for item in image_details.values()),
            "maximum_max_dimension": max(max(item["width"], item["height"]) for item in image_details.values()),
            "sub_2k_images": sorted(name for name, item in image_details.items()
                                    if max(item["width"], item["height"]) < 2048),
            "hero_target": "At least 4K source textures recommended for close-up hero use; source resolution is reported, never upscaled or fabricated.",
        },
        "source_image_textures": image_names,
        "source_image_details": image_details,
        "packed_images": sorted(packed_images),
        "unresolved_images": sorted(unresolved_images),
        "lod_policy": {
            "woody_mesh_ratios": {"LOD1": args.lod_ratios[0], "LOD2": args.lod_ratios[1], "LOD3": args.lod_ratios[2]},
            "foliage_mesh_ratios": {"LOD1": args.foliage_lod_ratios[0], "LOD2": args.foliage_lod_ratios[1], "LOD3": args.foliage_lod_ratios[2]},
            "foliage_detection": "material names containing needle, foliage, leaf or leaves",
        },
        "lods": {},
    }

    ratios = {"LOD0": (1.0, 1.0),
              "LOD1": (args.lod_ratios[0], args.foliage_lod_ratios[0]),
              "LOD2": (args.lod_ratios[1], args.foliage_lod_ratios[1]),
              "LOD3": (args.lod_ratios[2], args.foliage_lod_ratios[2])}
    for lod_name, (woody_ratio, foliage_ratio) in ratios.items():
        collection = bpy.data.collections.new(lod_name)
        bpy.context.scene.collection.children.link(collection)
        duplicates = []
        for original in source_objects:
            duplicate = original.copy()
            duplicate.data = original.data.copy()
            duplicate.matrix_world = original.matrix_world.copy()
            collection.objects.link(duplicate)
            duplicates.append(duplicate)
            object_ratio = foliage_ratio if is_foliage_mesh(original) else woody_ratio
            if object_ratio < 1.0 and triangles(duplicate) > 32:
                modifier = duplicate.modifiers.new("Photogrammetry_LOD_Decimate", "DECIMATE")
                modifier.decimate_type = "COLLAPSE"
                modifier.ratio = object_ratio
                modifier.use_collapse_triangulate = False
                bpy.context.view_layer.objects.active = duplicate
                duplicate.select_set(True)
                bpy.ops.object.modifier_apply(modifier=modifier.name)
                duplicate.select_set(False)
        filepath = args.output / (source.stem + "_" + lod_name.lower() + ".glb")
        export_glb(duplicates, filepath)
        lod_triangles = sum(triangles(obj) for obj in duplicates)
        manifest["lods"][lod_name] = {
            "file": filepath.name,
            "triangles": lod_triangles,
            "objects": len(duplicates),
            "woody_decimation_ratio": woody_ratio,
            "foliage_decimation_ratio": foliage_ratio,
        }

    bpy.ops.object.select_all(action="DESELECT")
    for obj in source_objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = source_objects[0]
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output / (source.stem + "_scanned_source.blend")))
    (args.output / (source.stem + ".scanned-tree.json")).write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("TALVISOTA_SCANNED_TREE_PIPELINE_OK")
    print(json.dumps(manifest, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
