"""build_proto.py — builds /Game/Maps/Proto: the greybox of the new game, a small town made of the engine's basic
shapes with the project's metre-grid material (no art, as a blockout is made): three lanes like a classic MOBA
between two base plazas, and city blocks between them with buildings of several heights, alleys, roofs to jump
onto, bridges, ramps, stairs, covered passages and walkways over the alleys.

  map 200 x 120 m (x -10000..10000, y -6000..6000), bases at x = +-7600
  mid lane    y -700..700 (the main street), the "river" street x -500..500 crosses it
  top / bottom lanes  y +-3800..5200 (and down to the plazas along x +-7200..8800)
  heights  120 crate / low wall · 250 jump + climb · 400 double jump + climb · 650 / 950 roofs · 1400 landmarks
The layout is point-symmetric: every piece authored for the west half is also placed at (-x, -y), so both teams
get the same town. The west plaza x -9000..-6600, y -2400..2400 stays flat and empty: the ProtoLab measures the
mechanics there (AProtoGameMode, yard at -7800, -700).
World settings name AProtoGameMode (compile the C++ first). Run headless (idempotent, the map is rebuilt):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" -unattended -nosplash -nullrhi
Prints PROTO_MAP OK path=... actors=N or PROTO_MAP FAIL <why>.
"""
import math

import unreal

MAP = "/Game/Maps/Proto"
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
CYL = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
GRID = unreal.load_asset("/Game/Arena/Materials/M_ArenaTraining.M_ArenaTraining")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
count = {"n": 0}
_mats = {}

# heights and their grey (lower = lighter, so the climbable steps read at a glance)
H_CRATE, H_LOW, H_MID, H_HIGH, H_TOP, H_LAND = 120, 250, 400, 650, 950, 1400
TONE = {H_CRATE: 0.42, H_LOW: 0.34, H_MID: 0.29, H_HIGH: 0.24, H_TOP: 0.2, H_LAND: 0.16}


def V(x, y, z):
    return unreal.Vector(x, y, z)


def R(yaw=0.0, pitch=0.0, roll=0.0):
    return unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)


def spawn(cls, loc, rot=None, label=None):
    a = actors.spawn_actor_from_class(cls, V(*loc), rot or R())
    if label:
        a.set_actor_label(label)
    count["n"] += 1
    return a


def tone_mat(tone):
    """the grid material at a grey tone (an instance per tone, shared with the training map)"""
    key = int(round(tone * 100))
    if key in _mats:
        return _mats[key]
    name = f"MI_Training_{key}"
    path = f"/Game/Arena/Materials/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, "/Game/Arena/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, GRID)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "Tone", key / 100.0)
        unreal.MaterialEditingLibrary.update_material_instance(mi)
        unreal.EditorAssetLibrary.save_loaded_asset(mi)
    _mats[key] = mi
    return mi


# ---- the layout as data: pieces authored once, placed twice (as authored and point-mirrored) ----------------------
WEST = []     # authored for the west team's half, mirrored to the east
CENTRE = []   # symmetric by themselves, placed once


def box(label, x0, x1, y0, y1, z0, z1, tone=None, into=None):
    (into if into is not None else WEST).append(("box", label, (x0, x1, y0, y1, z0, z1), tone))


def bld(label, x0, x1, y0, y1, h, into=None):
    """a building: a solid block from the ground to its roof, grey by height"""
    box(label, x0, x1, y0, y1, 0, h, TONE.get(h, 0.26), into)


def slab(label, x0, x1, y0, y1, top, thick=40, into=None):
    """a bridge, a walkway or a roof over a passage: a thin slab with its top at `top`"""
    box(label, x0, x1, y0, y1, top - thick, top, 0.31, into)


def crate(label, x, y, size=130, h=H_CRATE, into=None):
    box(label, x - size / 2, x + size / 2, y - size / 2, y + size / 2, 0, h, TONE[H_CRATE], into)


def cyl(label, x, y, r, h, tone=0.26, z0=0, into=None):
    (into if into is not None else WEST).append(("cyl", label, (x, y, r, h, z0), tone))


def ramp(label, top, foot, width, into=None):
    (into if into is not None else WEST).append(("ramp", label, (top, foot, width), 0.3))


def stairs(label, x0, x1, y0, y1, h, n, axis, into=None):
    """n steps rising to h along +axis ('x' or 'y', or '-x' / '-y') across the given footprint"""
    for i in range(n):
        f0, f1 = i / n, (i + 1) / n
        z = h * (i + 1) / n
        if axis == "y":
            box(f"{label}_{i}", x0, x1, y0 + (y1 - y0) * f0, y0 + (y1 - y0) * f1, 0, z, 0.36, into)
        elif axis == "-y":
            box(f"{label}_{i}", x0, x1, y1 - (y1 - y0) * f1, y1 - (y1 - y0) * f0, 0, z, 0.36, into)
        elif axis == "x":
            box(f"{label}_{i}", x0 + (x1 - x0) * f0, x0 + (x1 - x0) * f1, y0, y1, 0, z, 0.36, into)
        else:
            box(f"{label}_{i}", x1 - (x1 - x0) * f1, x1 - (x1 - x0) * f0, y0, y1, 0, z, 0.36, into)


def point(kind, tag, x, y, z=110, yaw=0.0, into=None):
    (into if into is not None else WEST).append((kind, tag, (x, y, z, yaw), None))


def sign(text, x, y, z, yaw, size, rgb, into=None):
    (into if into is not None else WEST).append(("sign", text, (x, y, z, yaw, size, rgb), None))


def mirror(p):
    kind, label, g, tone = p
    if kind == "box":
        x0, x1, y0, y1, z0, z1 = g
        return (kind, label + "_E", (-x1, -x0, -y1, -y0, z0, z1), tone)
    if kind == "cyl":
        x, y, r, h, z0 = g
        return (kind, label + "_E", (-x, -y, r, h, z0), tone)
    if kind == "ramp":
        (t, f, w) = g
        return (kind, label + "_E", ((-t[0], -t[1], t[2]), (-f[0], -f[1], f[2]), w), tone)
    if kind in ("start", "patrol"):
        x, y, z, yaw = g
        return (kind, label.replace("ProtoA", "ProtoB"), (-x, -y, z, yaw + 180.0), tone)
    if kind == "sign":
        x, y, z, yaw, size, rgb = g
        text = label.replace("BASE A", "BASE B").replace("TOP", "BOTTOM")
        return (kind, text, (-x, -y, z, yaw + 180.0, size, (rgb[2], rgb[1], rgb[0])), tone)
    raise ValueError(kind)


def layout():
    # ---- ground and lanes (the lane strips 1 cm proud of the ground, a lighter grey) ----
    box("Ground", -10000, 10000, -6000, 6000, -40, 0, 0.19, CENTRE)
    box("Lane_Mid", -6000, 6000, -700, 700, 0, 1, 0.25, CENTRE)
    box("River", -500, 500, -3800, 3800, 0, 1, 0.23, CENTRE)
    box("Lane_Top", -8800, 8800, 3800, 5200, 0, 1, 0.25)          # mirrored: the bottom lane
    box("Lane_TopW", -8800, -7200, 2600, 3800, 0, 1, 0.25)        # down to the west plaza (mirrored: bottom-east)
    box("Lane_TopE", 7200, 8800, 2600, 3800, 0, 1, 0.25)          # down to the east plaza (mirrored: bottom-west)
    box("Plaza", -9600, -6000, -2600, 2600, 0, 1, 0.27)

    # ---- the outer ring: tall edge buildings (the map's walls) ----
    box("Edge_W", -10000, -9600, -6000, 6000, 0, H_LAND, TONE[H_LAND])
    heights = [H_LAND, H_TOP, H_LAND, H_HIGH, H_TOP, H_LAND, H_TOP, H_HIGH, H_LAND, H_TOP]
    for i, h in enumerate(heights):
        x0 = -9600 + i * 1920
        bld(f"Edge_N_{i}", x0, x0 + 1920, 5200, 6000, h)
    # porches along the top lane: low roofs in front of the edge buildings, to jump onto and run along
    for i, x0 in enumerate((-7400, -1800, 3000)):
        bld(f"Porch_N_{i}", x0, x0 + 1400, 5000, 5200, H_LOW)
    bld("Edge_NW", -9600, -8800, 2600, 5200, H_HIGH)               # between the plaza and the top lane

    # ---- the base plaza (west; the east one is its mirror): the core, the lane gates, low walls ----
    cyl("Core_A", -9200, 0, 300, 900, 0.22)
    cyl("Core_A_Ring", -9200, 0, 520, 40, 0.3)
    cyl("Gate_A_Mid_N", -6300, 1150, 120, 350, 0.22)
    cyl("Gate_A_Mid_S", -6300, -1150, 120, 350, 0.22)
    cyl("Gate_A_Top", -9050, 2350, 120, 350, 0.22)
    cyl("Gate_A_Bot", -9050, -2350, 120, 350, 0.22)
    box("PlazaWall_NW", -9600, -9200, 2450, 2600, 0, H_CRATE, TONE[H_CRATE])
    box("PlazaWall_SW", -9600, -9200, -2600, -2450, 0, H_CRATE, TONE[H_CRATE])
    point("start", "ProtoA", -7600, 0, 110, 0.0)
    sign("BASE A", -9580, 0, 1150, 0.0, 160.0, (0.45, 0.7, 1.0))

    # ---- the towers along the lanes (the west team's; the mirror gives the east team's) ----
    for name, x, y in (("Mid1", -4600, 450), ("Mid2", -2000, -450), ("Top1", -5600, 5000), ("Top2", -2600, 4000),
                       ("Bot1", -5600, -4000), ("Bot2", -2600, -5000)):
        cyl(f"Tower_A_{name}", x, y, 150, 700, 0.21)
        cyl(f"Tower_A_{name}_Cap", x, y, 210, 60, 0.3, 700)

    # ---- the north-west blocks (x -6000..-500, y 900..3600; alleys at x -4650..-4300, -2800..-2400 and y 2100..2500)
    # col A, row 1: terraces 250 -> 400 -> 650 from the mid lane
    bld("NW_A1_High", -6000, -5300, 900, 2100, H_HIGH)
    bld("NW_A1_Low", -5300, -4650, 900, 1500, H_LOW)
    bld("NW_A1_Mid", -5300, -4650, 1500, 2100, H_MID)
    crate("NW_A1_Crate", -4960, 790)
    # col A, row 2: a tall block, reached from the 650 terrace across the alley with a double jump
    bld("NW_A2_Top", -6000, -4650, 2500, 3600, H_TOP)
    # col B, row 1: the covered passage (a 3 m tunnel from the mid lane to the alley under a 400 roof)
    bld("NW_B1_W", -4300, -3700, 900, 2100, H_MID)
    bld("NW_B1_E", -3400, -2800, 900, 2100, H_MID)
    slab("NW_B1_Roof", -3700, -3400, 900, 2100, H_MID, 90)
    # col B, row 2: a ramp from the alley to a landing at 400, a bridge over the alley to the passage roof
    bld("NW_B2_House", -4300, -2800, 2900, 3600, H_LOW)
    box("NW_B2_Landing", -3000, -2800, 2500, 2900, 0, H_MID, TONE[H_MID])
    ramp("NW_B2_Ramp", (-3000, 2700, H_MID), (-4250, 2700, 0), 400)
    slab("NW_B2_Bridge", -3200, -2800, 2100, 2500, H_MID)
    # col C, row 1: a staircase of roofs toward the river street: crate -> 250 -> 400 -> 650 -> 950
    bld("NW_C1_Top", -2400, -1700, 900, 2100, H_TOP)
    bld("NW_C1_High", -1700, -1100, 900, 2100, H_HIGH)
    bld("NW_C1_Low", -1100, -500, 900, 1500, H_LOW)
    bld("NW_C1_Mid", -1100, -500, 1500, 2100, H_MID)
    crate("NW_C1_Crate", -390, 1060, 120)
    # col C, row 2: a walled courtyard (sneaking: low walls, a pillar, crates for cover)
    box("NW_C2_WallN", -2400, -500, 3540, 3600, 0, H_LOW, TONE[H_LOW])
    box("NW_C2_WallW", -2400, -2340, 2500, 3540, 0, H_LOW, TONE[H_LOW])
    box("NW_C2_WallE", -560, -500, 2500, 3100, 0, H_LOW, TONE[H_LOW])
    box("NW_C2_WallS", -2400, -1500, 2500, 2560, 0, H_LOW, TONE[H_LOW])
    cyl("NW_C2_Pillar", -1450, 3050, 120, H_MID, TONE[H_MID])
    crate("NW_C2_Crate1", -2050, 2850, 150)
    crate("NW_C2_Crate2", -1000, 3300, 150)
    crate("NW_C2_Crate3", -850, 2750, 150)
    # a walkway over the horizontal alley at 250 (jump up to it; walk under it)
    slab("NW_Walkway", -6000, -3300, 2100, 2280, H_LOW, 30)
    # the corner block by the plaza: stairs up to a 250 house
    bld("NW_Corner", -7200, -6000, 2900, 3700, H_LOW)
    stairs("NW_Corner_Stairs", -6800, -6400, 2600, 2900, H_LOW - 10, 6, "y")

    # ---- the north-east blocks (x 500..6000; alleys at x 2200..2600, 4300..4700 and y 2100..2500) ----
    # col D, row 1: a market hall (a 5 m passage under a slab between a 400 and a 650 house)
    bld("NE_D1_W", 500, 1100, 900, 2100, H_MID)
    bld("NE_D1_E", 1600, 2200, 900, 2100, H_HIGH)
    slab("NE_D1_Hall", 1100, 1600, 900, 2100, H_MID, 70)
    crate("NE_D1_Crate", 800, 790)
    # col D, row 2: the clock tower (a landmark, not climbable) with a low annex
    bld("NE_D2_Tower", 900, 1800, 2700, 3400, H_LAND)
    bld("NE_D2_Annex", 500, 900, 2500, 3600, H_LOW)
    # col E, row 1: a 400 block; the overpass over the mid lane leaves from its roof
    bld("NE_E1", 2600, 4300, 900, 2100, H_MID)
    slab("Overpass", 3000, 3400, -900, 900, H_MID, 50)
    # col E, row 2: a ramp from the alley to a landing, a bridge over the alley onto the E1 roof
    bld("NE_E2_House", 2600, 4300, 2900, 3600, H_LOW)
    box("NE_E2_Landing", 3900, 4300, 2500, 2900, 0, H_MID, TONE[H_MID])
    ramp("NE_E2_Ramp", (3900, 2700, H_MID), (2650, 2700, 0), 400)
    slab("NE_E2_Bridge", 3900, 4300, 2100, 2500, H_MID)
    # col F, row 1: a 400 roof joined to E1 by a bridge, and a 950 tower beside it
    bld("NE_F1_Mid", 4700, 5400, 900, 2100, H_MID)
    bld("NE_F1_Top", 5400, 6000, 900, 2100, H_TOP)
    slab("NE_F1_Bridge", 4300, 4700, 1500, 1900, H_MID)
    # col F, row 2: steps up a 650 house (crate 120 -> 250 -> double jump onto 650)
    bld("NE_F2_High", 4900, 6000, 2500, 3600, H_HIGH)
    box("NE_F2_Step1", 4700, 4900, 3000, 3300, 0, H_CRATE, TONE[H_CRATE])
    box("NE_F2_Step2", 4700, 4900, 3300, 3600, 0, H_LOW, TONE[H_LOW])
    # the corner block by the east plaza's top gate
    crate("NE_Corner_Crate", 6380, 2520, 150)
    box("NE_Corner_Step", 6250, 6700, 2600, 2800, 0, H_LOW, TONE[H_LOW])
    bld("NE_Corner", 6000, 7200, 2800, 3700, H_MID)
    bld("Edge_NE", 8800, 9600, 2600, 5200, H_HIGH)

    # ---- street furniture: lamp posts along the mid lane, a fountain in the river street ----
    for i, x in enumerate(range(-5500, -600, 1600)):
        cyl(f"Lamp_N_{i}", x, 820, 18, 420, 0.3)
        cyl(f"Lamp_S_{i}", x + 1000, -820, 18, 420, 0.3)
    cyl("Fountain_N", 0, 2300, 300, 60, 0.3)
    cyl("Fountain_N_Pillar", 0, 2300, 90, 260, 0.24)

    # ---- the centre: a raised round plaza and a pillar to climb for the view ----
    cyl("Centre_Plaza", 0, 0, 650, 40, 0.3, 0, CENTRE)
    cyl("Centre_Pillar", 0, 0, 160, H_LOW, TONE[H_LOW], 0, CENTRE)

    # ---- patrols (the bots' walk when they have not seen the player) ----
    for x, y in ((-7600, 1800), (-5200, 0), (-3000, 300), (-1200, -250), (-6000, 4500), (-2000, 4500), (-8000, 3400),
                 (-4480, 1600), (-2600, 2300), (-1900, 3200), (0, 1500), (1400, 1500), (3000, 2300), (4500, 3000)):
        point("patrol", "ProtoPatrol", x, y, 100)

    # ---- signs (text in the world: which lane, which base) ----
    sign("NEW GAME · PROTOTYPE", 960, 5190, 1100, -90.0, 180.0, (1.0, 0.82, 0.4), CENTRE)
    sign("TOP LANE", -4000, 5190, 700, -90.0, 90.0, (0.85, 0.9, 1.0))
    sign("MID LANE", -6010, -1600, 600, 180.0, 80.0, (0.85, 0.9, 1.0))


def place(p):
    kind, label, g, tone = p
    if kind == "box":
        x0, x1, y0, y1, z0, z1 = g
        a = spawn(unreal.StaticMeshActor, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), label=label)
        c = a.static_mesh_component
        c.set_static_mesh(CUBE)
        c.set_world_scale3d(V(abs(x1 - x0) / 100.0, abs(y1 - y0) / 100.0, abs(z1 - z0) / 100.0))
        c.set_material(0, tone_mat(tone))
        c.set_collision_profile_name("BlockAll")
    elif kind == "cyl":
        x, y, r, h, z0 = g
        a = spawn(unreal.StaticMeshActor, (x, y, z0 + h / 2), label=label)
        c = a.static_mesh_component
        c.set_static_mesh(CYL)
        c.set_world_scale3d(V(r / 50.0, r / 50.0, h / 100.0))
        c.set_material(0, tone_mat(tone))
        c.set_collision_profile_name("BlockAll")
    elif kind == "ramp":
        # a walkable slope from the top to the foot: a rotated cube whose top face runs through both points
        p0, p1, width = g
        thick = 300.0
        dx, dy, dz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
        horiz = math.hypot(dx, dy)
        yaw, pitch, length = math.degrees(math.atan2(dy, dx)), math.degrees(math.atan2(dz, horiz)), math.hypot(horiz, dz)
        pr, yr = math.radians(pitch), math.radians(yaw)
        n = (-math.sin(pr) * math.cos(yr), -math.sin(pr) * math.sin(yr), math.cos(pr))
        mid = [(p0[i] + p1[i]) / 2 for i in range(3)]
        a = spawn(unreal.StaticMeshActor, tuple(mid[i] - n[i] * thick / 2 for i in range(3)), R(yaw=yaw, pitch=pitch), label=label)
        c = a.static_mesh_component
        c.set_static_mesh(CUBE)
        c.set_world_scale3d(V((length + 40.0) / 100.0, width / 100.0, thick / 100.0))
        c.set_material(0, tone_mat(tone))
        c.set_collision_profile_name("BlockAll")
    elif kind == "start":
        x, y, z, yaw = g
        a = spawn(unreal.PlayerStart, (x, y, z), R(yaw=yaw), label=f"Start_{label}")
        a.set_editor_property("player_start_tag", label)
    elif kind == "patrol":
        x, y, z, yaw = g
        a = spawn(unreal.TargetPoint, (x, y, z), R(yaw=yaw), label=f"Patrol_{int(x)}_{int(y)}")
        a.tags = [label]
    elif kind == "sign":
        x, y, z, yaw, size, rgb = g
        a = spawn(unreal.TextRenderActor, (x, y, z), R(yaw=yaw), label=f"Sign_{label[:12]}")
        t = a.text_render
        t.set_editor_property("text", label)
        t.set_editor_property("world_size", size)
        t.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
        t.set_editor_property("text_render_color", unreal.Color(int(rgb[0] * 255), int(rgb[1] * 255), int(rgb[2] * 255), 255))


def main():
    if not GRID:
        print("PROTO_MAP FAIL no M_ArenaTraining (run Tools/make_materials.py)")
        return
    try:
        mode = unreal.ProtoGameMode
    except AttributeError:
        print("PROTO_MAP FAIL no ProtoGameMode class (compile the C++ first)")
        return
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        if not levels.load_level(MAP):
            print("PROTO_MAP FAIL load_level")
            return
        for a in actors.get_all_level_actors():
            if not isinstance(a, unreal.WorldSettings) and (type(a) is not unreal.Brush or isinstance(a, unreal.Volume)):
                actors.destroy_actor(a)
    else:
        try:
            ok = levels.new_level(MAP, False)
        except TypeError:
            ok = levels.new_level(MAP)
        if not ok:
            print("PROTO_MAP FAIL new_level")
            return
    layout()
    for p in CENTRE:
        place(p)
    for p in WEST:
        place(p)
        place(mirror(p))
    # the menu's view: the town from above the west base
    cam = spawn(unreal.CameraActor, (-11500, -8200, 6200), R(yaw=36.0, pitch=-26.0), label="ProtoCam")
    cam.tags = ["ProtoCam"]
    cam.camera_component.set_editor_property("field_of_view", 70.0)
    # light: sun, sky light and a sky, a little fog (as the training map)
    sun = spawn(unreal.DirectionalLight, (0, 0, 2000), R(yaw=35.0, pitch=-48.0), label="Sun")
    sun.light_component.set_editor_property("intensity", 6.0)
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), label="Sky")
    sky = spawn(unreal.SkyLight, (0, 0, 2000), label="SkyLight")
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_editor_property("intensity", 1.2)
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, -200), label="Fog")
    fog.component.set_editor_property("fog_density", 0.006)
    # navigation (built at run time: the project's navmesh is dynamic)
    nav = spawn(unreal.NavMeshBoundsVolume, (0, 0, 700), label="NavBounds")
    nav.set_actor_scale3d(V(205.0, 125.0, 18.0))
    ws = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    ws.set_editor_property("default_game_mode", mode)
    if not levels.save_current_level():
        print("PROTO_MAP FAIL save")
        return
    print(f"PROTO_MAP OK path={MAP} actors={count['n']} west={len(WEST)} centre={len(CENTRE)}")


main()
