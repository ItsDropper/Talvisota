#!/usr/bin/env python3
"""Render multi-angle, material-colour geometry previews for a generated tree."""
import sys
from pathlib import Path

import bpy
from mathutils import Vector


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(args) != 2:
        raise SystemExit("usage: blender --background --python render_tree_previews.py -- <blend> <output-dir>")
    blend_path, output_dir = Path(args[0]), Path(args[1])
    if not blend_path.is_file():
        raise SystemExit("tree source blend is missing: " + str(blend_path))
    output_dir.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(blend_path))

    scene = bpy.context.scene
    for collection in bpy.data.collections:
        collection.hide_render = collection.name != "LOD0"

    # Workbench rendering is intentionally used for deterministic CI shape
    # inspection without depending on a GPU or pretending this is a PBR beauty render.
    scene.render.engine = "BLENDER_WORKBENCH"
    shading = scene.display.shading
    shading.light = "STUDIO"
    shading.color_type = "MATERIAL"
    shading.show_shadows = True
    shading.show_cavity = True
    shading.cavity_type = "BOTH"
    shading.curvature_ridge_factor = 1.35
    shading.curvature_valley_factor = 1.0
    shading.show_floor = True
    shading.show_axis_x = False
    shading.show_axis_y = False
    scene.render.resolution_x = 768
    scene.render.resolution_y = 768
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    scene.render.filepath = str(output_dir / "scots_pine_oblique.png")

    camera_data = bpy.data.cameras.new("TreePreviewCamera")
    camera = bpy.data.objects.new("TreePreviewCamera", camera_data)
    scene.collection.objects.link(camera)
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = 22.5
    scene.camera = camera
    target = Vector((0.0, 0.0, 9.0))
    views = {
        "oblique": Vector((24.0, -30.0, 12.0)),
        "opposite": Vector((-26.0, -28.0, 11.0)),
        "side": Vector((30.0, 22.0, 12.0)),
    }
    for name, position in views.items():
        camera.location = position
        camera.rotation_euler = (target - position).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = str(output_dir / ("scots_pine_" + name + ".png"))
        bpy.ops.render.render(write_still=True)
        print("TREE_PREVIEW_RENDERED", name, scene.render.filepath)


if __name__ == "__main__":
    main()
