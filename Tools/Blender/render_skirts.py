"""render_skirts.py — evidence render: the rock skirts against a wall box, as they stand in the arena.
Run: blender.exe -b --factory-startup --python-exit-code 1 -P render_skirts.py -- <out_dir> <png>
"""
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
out_dir, png = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
names = ["SM_RockSkirt_B", "SM_RockSkirt_A", "SM_RockSkirt_C"]
for n in names:
    with bpy.data.libraries.load(os.path.join(out_dir, n + ".blend")) as (src, dst):
        dst.objects = [n]
    for ob in dst.objects:
        sc.collection.objects.link(ob)
# a wall block filling y > 0; the ground z = 0
sk = {o.name: o for o in sc.objects}
sk["SM_RockSkirt_B"].location = (-4.0, 0.0, 0.0)
sk["SM_RockSkirt_A"].location = (1.2, 0.0, 0.0)
sk["SM_RockSkirt_C"].location = (5.6, 0.0, 0.0)
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(-1.5, 3.0, 1.5))
wall = bpy.context.active_object
wall.scale = (18.0, 6.0, 3.0)
bpy.ops.mesh.primitive_plane_add(size=40.0, location=(0.0, 0.0, 0.0))
cam_data = bpy.data.cameras.new("cam")
cam = bpy.data.objects.new("cam", cam_data)
sc.collection.objects.link(cam)
cam.location = (2.0, -11.0, 3.2)
cam.rotation_euler = (Vector((0.5, 0.0, 0.6)) - cam.location).to_track_quat("-Z", "Y").to_euler()
cam_data.lens = 26
sc.camera = cam
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.rotation_euler = (math.radians(50), 0.0, math.radians(30))
sc.collection.objects.link(sun)
sc.render.engine = "BLENDER_WORKBENCH"
sc.display.shading.light = "STUDIO"
sc.display.shading.show_cavity = True
sc.display.shading.show_shadows = True
sc.render.resolution_x, sc.render.resolution_y = 1400, 700
sc.render.filepath = png
bpy.ops.render.render(write_still=True)
print(f"RENDER OK {png}")
