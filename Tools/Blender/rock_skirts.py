"""rock_skirts.py — procedural rock piles for the outer foot of the arena's ramp parapets (Blender 5.2, headless).

Why: the ramp parapets are boxes drawn with the world-aligned rock material M_ArenaCliff; where a tall parapet meets
the ground the seam is a ruler-straight line. A skirt is a pile of broken rocks leaning on the wall and sunk into the
ground: it breaks that seam. The pieces take M_ArenaCliff in the engine (a world-aligned material: the texture of
the wall continues onto the rocks, the UVs do not matter).

Each rock is the convex hull of random points on a squashed ellipsoid (a broken, faceted stone - the usual
procedural rock), subdivided once and roughened a little so the facets are not razor flat. A smooth displaced
surface was tried first and read as a row of eggs in the engine (QA doc 13).

No downloads, no generators: bmesh + mathutils, seeded (same seed -> same mesh).
Space: 1 unit = 1 m, Z up. The wall is the plane y = 0 (rocks may reach into it; the part inside is never seen),
the ground z = 0 (every rock sinks into it); the pile protrudes towards -Y (Blender's front). Pivot = origin =
wall line at ground level. In UE the import mirrors Y: the pile protrudes towards +Y (import_env_meshes.py checks).

Run:  blender.exe -b --factory-startup --python-exit-code 1 -P rock_skirts.py -- <out_dir>
Writes <out_dir>/<Name>.blend and <Name>.fbx per piece; prints OK <name> tris=... dims=... and DONE assets=n fail=0.
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector, noise

PIECES = [
    # name, length m, height m, depth m, seed
    ("SM_RockSkirt_A", 4.0, 1.3, 0.8, 11),
    ("SM_RockSkirt_B", 6.0, 1.7, 1.0, 23),
    ("SM_RockSkirt_C", 3.0, 0.9, 0.6, 37),
]
MAX_TRIS = 3000


def rock(bm, rng, center, size, yaw, tilt):
    """one broken stone: hull of random points on an ellipsoid of half-sizes `size`, turned by yaw / tilt"""
    pts = []
    for _ in range(26):
        v = Vector((rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 1))).normalized()
        v *= rng.uniform(0.8, 1.0)                                     # some points inside: uneven facets
        pts.append(Vector((v.x * size.x, v.y * size.y, v.z * size.z)))
    rot = Matrix.Rotation(yaw, 3, "Z") @ Matrix.Rotation(tilt, 3, "X")
    tmp = bmesh.new()
    verts = [tmp.verts.new(rot @ p + center) for p in pts]
    hull = bmesh.ops.convex_hull(tmp, input=verts)
    # drop points the hull left inside
    inside = {v for v in hull["geom_interior"] + hull["geom_unused"] if isinstance(v, bmesh.types.BMVert) and v.is_valid}
    if inside:
        bmesh.ops.delete(tmp, geom=list(inside), context="VERTS")
    bmesh.ops.subdivide_edges(tmp, edges=tmp.edges[:], cuts=1, use_grid_fill=True)
    seed = rng.uniform(0, 100)
    for v in tmp.verts:
        n = noise.noise(v.co * 3.0 + Vector((seed, seed * 0.7, seed * 1.3)))
        v.co += (v.co - center).normalized() * 0.04 * n * min(size)     # roughened, not rounded
    tmp_me = bpy.data.meshes.new("tmp")
    tmp.to_mesh(tmp_me)
    tmp.free()
    bm.from_mesh(tmp_me)
    bpy.data.meshes.remove(tmp_me)


def build_piece(name, length, height, depth, seed):
    rng = random.Random(seed)
    bm = bmesh.new()
    x = -length / 2.0 + rng.uniform(0.1, 0.3)
    while x < length / 2.0 - 0.2:
        big = rng.random() < 0.5
        r = rng.uniform(0.45, 0.8) if big else rng.uniform(0.25, 0.45)            # half-length along the wall
        xc = x + r * 0.8
        r = min(r, xc + length / 2.0 - 0.02, length / 2.0 - 0.02 - xc)          # inside the strip
        if r > 0.15:
            h = height * (rng.uniform(0.55, 0.8) if big else rng.uniform(0.3, 0.5))   # half-height
            d = depth * (rng.uniform(0.55, 0.75) if big else rng.uniform(0.35, 0.55))  # half-depth
            h = min(h, r * 1.1)                                                   # no spires: a stone is about as tall as wide
            # leaning on the wall (its back in it), half sunk into the ground
            c = Vector((xc, -d * rng.uniform(0.35, 0.6), h * rng.uniform(0.15, 0.4)))
            rock(bm, rng, c, Vector((r, d, h)), rng.uniform(-0.25, 0.25), rng.uniform(-0.25, 0.1))
        # a second, smaller stone in front of a big one now and then
        if big and rng.random() < 0.5:
            s = rng.uniform(0.2, 0.35)
            c = Vector((xc + rng.uniform(-0.3, 0.3), -depth * rng.uniform(0.8, 1.1), s * 0.2))
            rock(bm, rng, c, Vector((s, s * 0.8, s * 0.7)), rng.uniform(0, math.pi), rng.uniform(-0.3, 0.3))
        x += max(r, 0.2) * rng.uniform(1.1, 1.6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def finish(ob):
    me = ob.data
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(34.0))                    # crisp breaks between facets
    mat = bpy.data.materials.new("M_RockSkirt")
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = (0.36, 0.33, 0.30, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.92
    me.materials.append(mat)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")


def tris(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = os.path.abspath(argv[0] if argv else os.path.join(os.path.dirname(__file__), "out"))
    os.makedirs(out, exist_ok=True)
    fails = 0
    for name, length, height, depth, seed in PIECES:
        try:
            bpy.ops.wm.read_factory_settings(use_empty=True)
            bpy.context.scene.unit_settings.scale_length = 1.0
            bpy.context.preferences.filepaths.save_version = 0   # no .blend1 backups next to the output
            ob = build_piece(name, length, height, depth, seed)
            finish(ob)
            t = tris(ob)
            if t > MAX_TRIS:
                raise RuntimeError(f"tris {t} > {MAX_TRIS}")
            bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, name + ".blend"))
            bpy.ops.export_scene.fbx(filepath=os.path.join(out, name + ".fbx"), use_selection=True, object_types={"MESH"},
                                     apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", axis_forward="-Y", axis_up="Z",
                                     mesh_smooth_type="FACE", use_tspace=True, add_leaf_bones=False)
            d = ob.dimensions
            print(f"OK {name} tris={t} dims=({d.x:.2f},{d.y:.2f},{d.z:.2f})")
        except Exception as ex:  # noqa: BLE001
            fails += 1
            print(f"FAIL {name} {ex}")
    print(f"DONE assets={len(PIECES)} fail={fails}")
    sys.exit(1 if fails else 0)


main()
