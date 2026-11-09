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

    # Cycles CPU rendering avoids the OpenGL/EGL dependency that Workbench
    # requires on headless Linux runners. It also exercises the actual PBR maps.
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 12
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 768
    scene.render.resolution_y = 768
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.film_transparent = False
    scene.render.filepath = str(output_dir / "scots_pine_oblique.png")

    ground_material = bpy.data.materials.new("Preview Ground")
    ground_material.diffuse_color = (0.14, 0.16, 0.14, 1.0)
    bpy.ops.mesh.primitive_plane_add(size=60.0, location=(0.0, 0.0, -0.025))
    ground = bpy.context.object
    ground.name = "PreviewGround"
    ground.data.materials.append(ground_material)

    scene.world.use_nodes = True
    background = scene.world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (0.10, 0.12, 0.14, 1.0)
    background.inputs["Strength"].default_value = 0.35

    def add_area_light(name, location, energy, size):
        light_data = bpy.data.lights.new(name, type="AREA")
        light_data.energy = energy
        light_data.shape = "DISK"
        light_data.size = size
        light = bpy.data.objects.new(name, light_data)
        scene.collection.objects.link(light)
        light.location = location
        light.rotation_euler = (Vector((0.0, 0.0, 8.0)) - light.location).to_track_quat("-Z", "Y").to_euler()

    add_area_light("KeyLight", (14.0, -18.0, 24.0), 4200.0, 12.0)
    add_area_light("FillLight", (-14.0, -8.0, 13.0), 1800.0, 10.0)
    add_area_light("RimLight", (2.0, 16.0, 20.0), 3000.0, 9.0)

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
