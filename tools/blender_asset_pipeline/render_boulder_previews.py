#!/usr/bin/env python3
"""Render oblique, side and top views of the Blender granite boulder."""
import sys
from pathlib import Path

import bpy
from mathutils import Vector


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(args) != 2:
        raise SystemExit("usage: render_boulder_previews.py -- <blend> <output-dir>")
    blend, output = Path(args[0]), Path(args[1])
    if not blend.is_file():
        raise SystemExit("missing boulder source blend: " + str(blend))
    output.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(blend))
    for collection in bpy.data.collections:
        collection.hide_render = collection.name != "LOD0"
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 48
    scene.cycles.use_denoising = False
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Medium High Contrast"
    scene.render.resolution_x = 768
    scene.render.resolution_y = 768
    scene.render.image_settings.file_format = "PNG"
    ground_mat = bpy.data.materials.new("Boulder preview ground")
    ground_mat.diffuse_color = (0.13, 0.15, 0.17, 1.0)
    ground_mat.use_nodes = True
    ground_mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.13, 0.15, 0.17, 1.0)
    bpy.ops.mesh.primitive_plane_add(size=12.0, location=(0.0, 0.0, -0.015))
    bpy.context.object.data.materials.append(ground_mat)
    scene.world.use_nodes = True
    scene.world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.10, 0.12, 0.15, 1.0)
    scene.world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.25
    for name, location, energy, size in (
        ("Key", (4.0, -5.0, 6.0), 520.0, 4.0),
        ("Fill", (-4.0, -2.0, 3.5), 260.0, 3.0),
        ("Rim", (1.0, 4.0, 5.0), 360.0, 3.0),
    ):
        data = bpy.data.lights.new(name, "AREA")
        data.energy = energy
        data.shape = "DISK"
        data.size = size
        light = bpy.data.objects.new(name, data)
        scene.collection.objects.link(light)
        light.location = location
        light.rotation_euler = (Vector((0.0, 0.0, 0.8)) - light.location).to_track_quat("-Z", "Y").to_euler()
    camera_data = bpy.data.cameras.new("BoulderReviewCamera")
    camera = bpy.data.objects.new("BoulderReviewCamera", camera_data)
    scene.collection.objects.link(camera)
    camera_data.type = "ORTHO"
    scene.camera = camera
    views = {
        "oblique": (4.0, -5.0, 3.4),
        "side": (-5.0, -1.0, 2.4),
        "top": (0.5, -1.0, 7.0),
        "macro": (2.0, -2.0, 2.0),
    }
    for name, location in views.items():
        camera.location = location
        camera.rotation_euler = (Vector((0.0, 0.0, 0.8)) - camera.location).to_track_quat("-Z", "Y").to_euler()
        camera_data.ortho_scale = 3.8 if name == "macro" else 4.8
        scene.render.resolution_x = 1024 if name == "macro" else 768
        scene.render.resolution_y = scene.render.resolution_x
        scene.render.filepath = str(output / ("granite_boulder_" + name + ".png"))
        bpy.ops.render.render(write_still=True)
        print("BOULDER_PREVIEW_RENDERED", name)
    from PIL import Image, ImageDraw
    names = ("oblique", "side", "top", "macro")
    thumbs = []
    for name in names:
        image = Image.open(output / ("granite_boulder_" + name + ".png")).convert("RGB")
        image.thumbnail((480, 480), Image.Resampling.LANCZOS)
        panel = Image.new("RGB", (480, 520), (24, 27, 31))
        panel.paste(image, ((480 - image.width) // 2, 36))
        ImageDraw.Draw(panel).text((12, 12), name.upper(), fill=(240, 244, 248))
        thumbs.append(panel)
    sheet = Image.new("RGB", (960, 1040), (18, 20, 24))
    for index, panel in enumerate(thumbs):
        sheet.paste(panel, ((index % 2) * 480, (index // 2) * 520))
    sheet.save(output / "granite_boulder_review.png", optimize=True)
    print("BOULDER_PREVIEW_CONTACT_SHEET", output / "granite_boulder_review.png")


if __name__ == "__main__":
    main()
