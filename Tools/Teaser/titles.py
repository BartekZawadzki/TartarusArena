"""titles.py — the teaser's title cards: 3D Cinzel lettering in gold, each letter flipping up into place in turn, a
slow push-in and a light sweeping across the metal; rendered on a transparent film (the edit lays them over the
footage). Run: blender -b --factory-startup --python-exit-code 1 -P titles.py -- <out_dir> [card ...] [--still N]
Prints TITLE OK <card> frames=N per card, TITLES DONE at the end.
"""
import math
import os
import sys

import bpy

FONT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Content", "Data", "Fonts"))
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = ARGS[0] if ARGS else os.path.abspath("titles")
STILL = None
if "--still" in ARGS:
    STILL = int(ARGS[ARGS.index("--still") + 1])
WANT = [a for i, a in enumerate(ARGS[1:], 1) if not a.startswith("--") and not a.isdigit() and ARGS[i - 1] not in ("--still", "--cardframes")]
CARD_FRAMES = int(ARGS[ARGS.index("--cardframes") + 1]) if "--cardframes" in ARGS else None

CARDS = {
    # name: (lines [(text, size, y, tracking, font)], frames, sweep (start, end))
    "heroes": ([("11 HEROES", 1.0, 0.0, 0.16, "Cinzel-Black.ttf")], 60, (14, 52)),
    "5v5": ([("5 VS 5", 1.15, 0.0, 0.2, "Cinzel-Black.ttf")], 60, (14, 52)),
    "conquest": ([("CONQUEST", 1.0, 0.0, 0.14, "Cinzel-Black.ttf")], 60, (14, 52)),
    "logo": ([("TARTARUS", 1.25, 0.32, 0.12, "Cinzel-Black.ttf"), ("ARENA", 0.62, -0.78, 0.55, "Cinzel-Bold.ttf")], 240, (40, 110)),
}


def hexlin(h):
    h = h.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c] + [1.0]


def gold():
    m = bpy.data.materials.new("Gold")
    b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    b.inputs["Base Color"].default_value = hexlin("#d9a441")
    b.inputs["Metallic"].default_value = 1.0
    b.inputs["Roughness"].default_value = 0.24
    b.inputs["Emission Color"].default_value = hexlin("#ffb347")
    b.inputs["Emission Strength"].default_value = 0.14
    return m


def edge():
    m = bpy.data.materials.new("Edge")
    b = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    b.inputs["Base Color"].default_value = hexlin("#5a3a16")
    b.inputs["Metallic"].default_value = 1.0
    b.inputs["Roughness"].default_value = 0.35
    return m


def letter(ch, font, size, mat, mat_edge):
    cu = bpy.data.curves.new(f"L_{ch}", "FONT")
    cu.body = ch
    cu.font = font
    cu.size = size
    cu.extrude = 0.06 * size
    cu.bevel_depth = 0.018 * size
    cu.bevel_resolution = 3
    cu.align_x = "CENTER"
    cu.align_y = "CENTER"
    ob = bpy.data.objects.new(f"L_{ch}", cu)
    ob.data.materials.append(mat)
    ob.data.materials.append(mat_edge)
    cu.materials[0] = mat
    bpy.context.scene.collection.objects.link(ob)
    ob.rotation_euler = (math.radians(90), 0, 0)   # the text stands up, facing -Y (the camera)
    return ob


def key(ob, path, frame, value, interp="BEZIER", easing="AUTO"):
    setattr(ob, path, value) if not isinstance(value, tuple) else None
    if isinstance(value, tuple):
        getattr(ob, path)[:] = value
    ob.keyframe_insert(path, frame=frame)
    act = ob.animation_data.action
    # 5.x layered actions: walk the channelbag of the object's slot
    from bpy_extras import anim_utils
    cb = anim_utils.action_get_channelbag_for_slot(act, ob.animation_data.action_slot)
    for fc in cb.fcurves:
        if fc.data_path == path:
            for kp in fc.keyframe_points:
                if abs(kp.co[0] - frame) < 0.01:
                    kp.interpolation = interp
                    kp.easing = easing


def build(name):
    lines, frames, sweep = CARDS[name]
    if CARD_FRAMES and name != "logo":   # longer cards (teaser v3: 3 s), the sweep stretched with them
        frames, sweep = CARD_FRAMES, (int(CARD_FRAMES * 0.2), int(CARD_FRAMES * 0.88))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x, sc.render.resolution_y = 1920, 1080
    sc.render.fps = 30
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.frame_start, sc.frame_end = 1, frames
    try:
        sc.eevee.taa_render_samples = 48
    except Exception:
        pass
    try:
        sc.view_settings.view_transform = "AgX"
        sc.view_settings.look = "AgX - Punchy"
    except Exception:
        pass
    world = bpy.data.worlds.new("W")
    sc.world = world
    bg = next(n for n in world.node_tree.nodes if n.type == "BACKGROUND")
    bg.inputs["Color"].default_value = (0.02, 0.018, 0.016, 1)
    bg.inputs["Strength"].default_value = 1.0

    mat, mat_edge = gold(), edge()
    letters = []
    for text, size, y, track, fname in lines:
        # the whole line as one text (the font's own kerning, the tracking as letter spacing), then one mesh per
        # letter (loose parts), each with its pivot in its middle
        font = bpy.data.fonts.load(os.path.join(FONT_DIR, fname), check_existing=True)
        cu = bpy.data.curves.new(f"T_{text}", "FONT")
        cu.body = text
        cu.font = font
        cu.size = size
        cu.space_character = 1.0 + track * 2.2
        cu.space_word = 1.0 + track
        cu.extrude = 0.06 * size
        cu.bevel_depth = 0.018 * size
        cu.bevel_resolution = 3
        cu.align_x = "CENTER"
        cu.align_y = "CENTER"
        ob = bpy.data.objects.new(f"T_{text}", cu)
        sc.collection.objects.link(ob)
        ob.data.materials.append(mat)
        ob.location = (0.0, 0.0, y)
        ob.rotation_euler = (math.radians(90), 0, 0)
        for o in list(bpy.context.view_layer.objects.selected):
            o.select_set(False)
        bpy.context.view_layer.objects.active = ob
        ob.select_set(True)
        bpy.ops.object.convert(target="MESH")
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.remove_doubles(threshold=0.0005 * size)   # the caps and the bevel come apart from a text
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.separate(type="LOOSE")
        bpy.ops.object.mode_set(mode="OBJECT")
        parts = [o for o in bpy.context.view_layer.objects.selected]
        for o in parts:
            for p in parts:
                p.select_set(p == o)
            bpy.context.view_layer.objects.active = o
            bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY", center="BOUNDS")
        bpy.context.view_layer.update()
        parts.sort(key=lambda o: o.location.x)
        print(f"TITLE PARTS {text!r} {len(parts)}")
        letters += [(o, y) for o in parts]

    # the letters flip up in turn: from lying back and below, a little overshoot (BACK easing)
    stagger = 2 if name != "logo" else 2
    for i, (o, y) in enumerate(letters):
        f0 = 1 + i * stagger + (6 if (name == "logo" and y < 0) else 0)
        f1 = f0 + 14
        rest_loc = tuple(o.location)
        key(o, "location", f0, (rest_loc[0], rest_loc[1] + 1.2, rest_loc[2] - 0.5))
        key(o, "rotation_euler", f0, (math.radians(90 - 85), 0.0, 0.0))
        key(o, "scale", f0, (0.001, 0.001, 0.001))
        key(o, "scale", f0 + 5, (1.0, 1.0, 1.0), "BACK", "EASE_OUT")
        key(o, "location", f1, rest_loc, "BACK", "EASE_OUT")
        key(o, "rotation_euler", f1, (math.radians(90), 0.0, 0.0), "BACK", "EASE_OUT")
        # re-key the starts with the easing on the segment that leaves them
        key(o, "location", f0, (rest_loc[0], rest_loc[1] + 1.2, rest_loc[2] - 0.5), "BACK", "EASE_OUT")
        key(o, "rotation_euler", f0, (math.radians(90 - 85), 0.0, 0.0), "BACK", "EASE_OUT")
        key(o, "scale", f0, (0.001, 0.001, 0.001), "SINE", "EASE_OUT")

    # the camera: a slow push-in through the whole card
    cam_d = bpy.data.cameras.new("Cam")
    cam_d.lens = 50
    cam = bpy.data.objects.new("Cam", cam_d)
    sc.collection.objects.link(cam)
    sc.camera = cam
    span = max(1.0, max((o.location.x for o, _ in letters), default=1) * 2)
    # a 50 mm lens on a 36 mm film sees 0.72 x the distance across: the title fills ~60 % of the frame
    dist = max(7.5, span / (0.72 * 0.6))
    if name == "logo":
        dist = max(dist, span / (0.72 * 0.68))
    cam.rotation_euler = (math.radians(90), 0, 0)
    key(cam, "location", 1, (0.0, -dist * 1.06, 0.0), "LINEAR")
    key(cam, "location", frames, (0.0, -dist, 0.0), "LINEAR")

    # the light: a warm key from above left, a cool rim from behind, a soft fill; the sweep crosses the letters
    def area(name_, loc, rot, energy, size, color):
        d = bpy.data.lights.new(name_, "AREA")
        d.energy = energy
        d.size = size
        d.color = color
        o = bpy.data.objects.new(name_, d)
        o.location = loc
        o.rotation_euler = [math.radians(a) for a in rot]
        sc.collection.objects.link(o)
        return o

    area("Key", (-4, -6, 4), (55, 0, -35), 1500, 4, (1.0, 0.86, 0.66))
    area("Fill", (5, -7, -1), (95, 0, 35), 250, 6, (0.75, 0.82, 1.0))
    area("Rim", (0, 4, 3), (-60, 0, 0), 700, 8, (0.6, 0.75, 1.0))
    sw = area("Sweep", (-span, -2.2, 0.2), (90, 0, 0), 0, 0.6, (1.0, 0.95, 0.85))
    sw.data.shape = "RECTANGLE"
    sw.data.size, sw.data.size_y = 0.35, 6.0
    s0, s1 = sweep
    sw.data.energy = 2600
    key(sw, "location", s0, (-span * 0.75 - 1.5, -2.2, 0.2), "SINE", "EASE_IN_OUT")
    key(sw, "location", s1, (span * 0.75 + 1.5, -2.2, 0.2), "SINE", "EASE_IN_OUT")
    # a softbox the gold reflects (out of shot, above the camera)
    pl = bpy.data.meshes.new("Box")
    pl.from_pydata([(-6, 0, -1.5), (6, 0, -1.5), (6, 0, 1.5), (-6, 0, 1.5)], [], [(0, 1, 2, 3)])
    box = bpy.data.objects.new("Box", pl)
    box.location = (0, -12, 6)
    box.rotation_euler = (math.radians(-30), 0, 0)
    em = bpy.data.materials.new("Em")
    em.node_tree.nodes.clear()
    e = em.node_tree.nodes.new("ShaderNodeEmission")
    e.inputs["Strength"].default_value = 3.0
    e.inputs["Color"].default_value = (1.0, 0.92, 0.8, 1)
    o_ = em.node_tree.nodes.new("ShaderNodeOutputMaterial")
    em.node_tree.links.new(e.outputs[0], o_.inputs[0])
    pl.materials.append(em)
    sc.collection.objects.link(box)
    box.visible_camera = False

    out = os.path.join(OUT, name)
    os.makedirs(out, exist_ok=True)
    if STILL:
        sc.frame_set(STILL)
        sc.render.filepath = os.path.join(OUT, f"still_{name}_{STILL:03d}.png")
        bpy.ops.render.render(write_still=True)
        print(f"TITLE STILL {name} {sc.render.filepath}")
        return
    sc.render.filepath = os.path.join(out, "f_")
    bpy.ops.render.render(animation=True)
    print(f"TITLE OK {name} frames={frames}")


try:
    for n in (WANT or list(CARDS)):
        build(n)
    print("TITLES DONE")
except Exception as ex:
    import traceback
    traceback.print_exc()
    print(f"TITLES FAIL {ex}")
    sys.exit(1)
