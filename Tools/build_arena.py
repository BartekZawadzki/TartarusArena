"""build_arena.py — builds /Game/Maps/Arena: a jungle-ruins arena with real elevation, dressed only with the
operator's library packs (Paragon Agora/Monolith props, KiteDemo nature) and engine content.

Run headless (idempotent, the map is cleared and rebuilt):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path to this file>" -unattended -nosplash -nullrhi
Prints ARENA_MAP lines; the last one is ARENA_MAP OK path=... actors=N or ARENA_MAP FAIL <why>.

Layout (cm, mirrored in x and y; team A = Dawn at x < 0, team B = Dusk at x > 0):
  base plateaus  |x| 4400..7000, |y| < 2000, top +300, ramps to the middle and to both side lanes
  central pit    |x| < 2000, |y| < 1500, floor -150, ramps east/west, orb altar in the centre
  ridges         |x| < 2400, 1500 < |y| < 2400, top +250 (a cliff over the pit), ramps at the ends and from the lane
  side lanes     |y| 3000..4000 through jungle, border cliffs at |y| 4600
Gameplay collision is clean hidden geometry (reliable navmesh); trees, rocks, statues and pillars keep their own
collision; grass, ferns, flowers and floors are visual only.
"""
import math
import random

import unreal

MAP = "/Game/Maps/Arena"
MAT_DIR = "/Game/Arena/Materials"
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
CYL = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
BASIC = unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")
PLANE = "/Game/ParagonProps/Ground/Meshes/SM_Plane_5x5m.SM_Plane_5x5m"

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
rng = random.Random(7)
count = {"n": 0}
_mesh_cache = {}


def asset(path):
    a = unreal.load_asset(path)
    if not a:
        print(f"ARENA_MAP WARN missing {path}")
    return a


def R(yaw=0.0, pitch=0.0, roll=0.0):
    return unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)


def V(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def color_mat(name, rgb):
    path = f"{MAT_DIR}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, BASIC)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(*rgb, 1.0))
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    return mi


def spawn(cls, loc, rot=None, label=None):
    a = actors.spawn_actor_from_class(cls, V(*loc), rot or R())
    if label:
        a.set_actor_label(label)
    count["n"] += 1
    return a


# ---- gameplay collision: clean boxes for a reliable navmesh ------------------------------------------------
# The terrain boxes are rendered with a world-aligned rock (M_ArenaCliff): every face that no dressing covers is a
# rock face, never a see-through hole into the void. Only the tall border walls and the orb altar (its own deco
# mesh has the same shape) stay invisible.
BLOCK_MAT = None
CLIFF_MAT = None


def solid(a, visible):
    c = a.static_mesh_component
    if visible and CLIFF_MAT:
        c.set_material(0, CLIFF_MAT)
    else:
        c.set_material(0, BLOCK_MAT)
        a.set_actor_hidden_in_game(True)


def block(label, x0, x1, y0, y1, z0, z1, visible=True):
    a = spawn(unreal.StaticMeshActor, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), label=label)
    c = a.static_mesh_component
    c.set_static_mesh(CUBE)
    c.set_world_scale3d(V(abs(x1 - x0) / 100.0, abs(y1 - y0) / 100.0, abs(z1 - z0) / 100.0))
    solid(a, visible)
    return a


def cyl_block(label, x, y, radius, z0, z1, visible=True):
    a = spawn(unreal.StaticMeshActor, (x, y, (z0 + z1) / 2), label=label)
    c = a.static_mesh_component
    c.set_static_mesh(CYL)
    c.set_world_scale3d(V(radius / 50.0, radius / 50.0, (z1 - z0) / 100.0))
    solid(a, visible)
    return a


def ramp_geom(p0, p1):
    dx, dy, dz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
    horiz = math.hypot(dx, dy)
    return math.degrees(math.atan2(dy, dx)), math.degrees(math.atan2(dz, horiz)), math.hypot(horiz, dz)


RAMPS = []   # footprints (x0, x1, y0, y1, along_x) of every ramp with its parapets: no ground cover is planted on a slope


def on_ramp(x, y, pad=0.0):
    return any(a - pad <= x <= b + pad and c - pad <= y <= d + pad for a, b, c, d, _ in RAMPS)


def ramp(label, p0, p1, width, thick=450.0):
    """Walkable slope from p0 (x, y, z top) to p1; a rotated box, extended 30 cm at both ends against lips. It is
    thick: its underside sinks into the ground, so from the side a ramp is a solid earthwork, not a floating plank."""
    yaw, pitch, length = ramp_geom(p0, p1)
    p, yw = math.radians(pitch), math.radians(yaw)
    n = (-math.sin(p) * math.cos(yw), -math.sin(p) * math.sin(yw), math.cos(p))
    mid = [(p0[i] + p1[i]) / 2 for i in range(3)]
    center = (mid[0] - n[0] * thick / 2, mid[1] - n[1] * thick / 2, mid[2] - n[2] * thick / 2)
    half = width / 2 + 90.0
    if abs(p1[0] - p0[0]) >= abs(p1[1] - p0[1]):
        RAMPS.append((min(p0[0], p1[0]) - 40, max(p0[0], p1[0]) + 40, p0[1] - half, p0[1] + half, True))
    else:
        RAMPS.append((p0[0] - half, p0[0] + half, min(p0[1], p1[1]) - 40, max(p0[1], p1[1]) + 40, False))
    a = spawn(unreal.StaticMeshActor, center, R(yaw=yaw, pitch=pitch), label=label)
    c = a.static_mesh_component
    c.set_static_mesh(CUBE)
    c.set_world_scale3d(V((length + 60.0) / 100.0, width / 100.0, thick / 100.0))
    solid(a, True)
    return yaw, pitch, length


PARAPETS = []   # (p0, p1, width, thick, above) of every ramp_walls pair: build_skirts dresses their outer feet


def ramp_walls(label, p0, p1, width, thick=60.0, above=110.0, depth=450.0):
    """Stone parapets along a ramp, parallel to its slope (110 cm above the walking surface, footed deep in the
    ground): nobody steps onto the ramp sideways or is knocked off its side mid-climb, and the ramp reads as a
    built causeway."""
    PARAPETS.append((p0, p1, width, thick, above))
    yaw, pitch, length = ramp_geom(p0, p1)
    p, yw = math.radians(pitch), math.radians(yaw)
    n = (-math.sin(p) * math.cos(yw), -math.sin(p) * math.sin(yw), math.cos(p))   # slope normal
    sx, sy = -math.sin(yw), math.cos(yw)                                               # sideways
    mid = [(p0[i] + p1[i]) / 2 for i in range(3)]
    for side in (-1, 1):
        off = side * (width / 2 + thick / 2)
        lift = (above - depth) / 2
        cx, cy, cz = mid[0] + sx * off + n[0] * lift, mid[1] + sy * off + n[1] * lift, mid[2] + n[2] * lift
        a = spawn(unreal.StaticMeshActor, (cx, cy, cz), R(yaw=yaw, pitch=pitch), label=f"{label}_Wall{side}")
        c = a.static_mesh_component
        c.set_static_mesh(CUBE)
        c.set_world_scale3d(V((length + 60.0) / 100.0, thick / 100.0, (above + depth) / 100.0))
        solid(a, True)
        # a carved stone cap along the top (library trim, pitched with the slope): a built balustrade, not a raw box
        seg = 250.0
        n_seg = max(1, int(math.ceil((length + 40.0) / seg)))
        for k in range(n_seg):
            t = (k + 0.5) / n_seg
            px = p0[0] + (p1[0] - p0[0]) * t + sx * off
            py = p0[1] + (p1[1] - p0[1]) * t + sy * off
            pz = p0[2] + (p1[2] - p0[2]) * t + above - 34.0
            inst("/Game/ParagonProps/Monolith/Ruins/Meshes/JungleTrim01_250.JungleTrim01_250", px, py, pz, yaw=yaw, pitch=pitch,
                 scale=None, height=0.62, shadows=True, cull=0.0, size_xy=((length + 40.0) / n_seg / 100.0 + 0.05, thick / 100.0 + 0.16))


# ---- dressing ------------------------------------------------------------------------------------------------
def mesh_info(path):
    if path not in _mesh_cache:
        m = asset(path)
        b = m.get_bounds() if m else None
        _mesh_cache[path] = (m, b.origin if b else V(0, 0, 0), b.box_extent if b else V(50, 50, 50))
    return _mesh_cache[path]


def fit_scale(e, size=None, height=None, width=None, scale=None):
    if size:
        return V(size[0] * 50.0 / max(e.x, 1.0), size[1] * 50.0 / max(e.y, 1.0), size[2] * 50.0 / max(e.z, 1.0))
    if height:
        u = height * 50.0 / max(e.z, 1.0)
    elif width:
        u = width * 50.0 / max(e.x, e.y, 1.0)
    else:
        u = scale or 1.0
    return V(u, u, u)


def aligned_location(o, e, s, x, y, z_bottom, yaw):
    """Pivot location that puts the scaled bounding box's bottom-centre at (x, y, z_bottom)."""
    ox, oy, oz = o.x * s.x, o.y * s.y, (o.z - e.z) * s.z
    c, sn = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return (x - (ox * c - oy * sn), y - (ox * sn + oy * c), z_bottom - oz)


def place(label, path, x, y, z, yaw=0.0, size=None, height=None, width=None, scale=None, collide=False, mat=None, pitch=0.0, roll=0.0):
    m, o, e = mesh_info(path)
    if not m:
        return None
    s = fit_scale(e, size, height, width, scale)
    a = spawn(unreal.StaticMeshActor, aligned_location(o, e, s, x, y, z, yaw), R(yaw=yaw, pitch=pitch, roll=roll), label=label)
    c = a.static_mesh_component
    c.set_static_mesh(m)
    c.set_world_scale3d(s)
    if mat:
        c.set_material(0, mat)
    if collide:
        c.set_collision_profile_name("BlockAll")
        # footprint of a solid piece: trees, rocks and ground cover are never scattered into it
        cy, sy_ = abs(math.cos(math.radians(yaw))), abs(math.sin(math.radians(yaw)))
        hx, hy = e.x * s.x * cy + e.y * s.y * sy_, e.x * s.x * sy_ + e.y * s.y * cy
        SOLID.append((x - hx, x + hx, y - hy, y + hy))
    else:
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property("can_ever_affect_navigation", False)
    return a


_scatter = {}


def inst(path, x, y, z, yaw=0.0, scale=1.0, height=None, mat=None, collide=False, shadows=True, cull=0.0, pitch=0.0, roll=0.0, size_xy=None):
    m, o, e = mesh_info(path)
    if not m:
        return
    s = fit_scale(e, height=height, scale=scale)
    if size_xy:
        s = V(size_xy[0] * 50.0 / max(e.x, 1.0), size_xy[1] * 50.0 / max(e.y, 1.0), s.z)
    loc = aligned_location(o, e, s, x, y, z, yaw)
    key = (path, mat.get_path_name() if mat else "", collide, shadows, cull)
    _scatter.setdefault(key, []).append(unreal.Transform(location=V(*loc), rotation=R(yaw=yaw, pitch=pitch, roll=roll), scale=s))


_usage_done = set()


def ensure_ism_usage(mat):
    """A material without the instanced-mesh usage flag renders as the grey default checker on instances in the
    game (the plateau floors, the ramps, the pines and a rock were grey). Set it on the base material once."""
    if not mat:
        return
    base = mat.get_base_material() if hasattr(mat, "get_base_material") else mat
    if not base or base.get_path_name() in _usage_done:
        return
    _usage_done.add(base.get_path_name())
    try:
        if not base.get_editor_property("used_with_instanced_static_meshes"):
            base.set_editor_property("used_with_instanced_static_meshes", True)
            unreal.MaterialEditingLibrary.recompile_material(base)
            unreal.EditorAssetLibrary.save_loaded_asset(base)
            print(f"ARENA_MAP usage ism set {base.get_path_name()}")
    except Exception as ex:  # noqa: BLE001
        print(f"ARENA_MAP WARN usage {base.get_path_name()}: {ex}")


def flush_scatter():
    total = 0
    for (path, mat_path, collide, shadows, cull), xs in _scatter.items():
        m = mesh_info(path)[0]
        if mat_path:
            ensure_ism_usage(unreal.load_asset(mat_path))
        elif m:
            for sm in m.get_editor_property("static_materials"):
                ensure_ism_usage(sm.get_editor_property("material_interface"))
        a = spawn(unreal.ArenaScatter, (0, 0, 0), label=f"Scatter_{path.split('.')[-1]}_{len(xs)}")
        a.set_editor_property("mesh", mesh_info(path)[0])
        if mat_path:
            a.set_editor_property("material", unreal.load_asset(mat_path))
        a.set_editor_property("collide", collide)
        a.set_editor_property("shadows", shadows)
        a.set_editor_property("cull_distance", cull)
        a.set_editor_property("instances", xs)
        built = a.rebuild()
        if built != len(xs):
            print(f"ARENA_MAP WARN scatter {path} built={built} expected={len(xs)}")
        total += built
    print(f"ARENA_MAP scatter groups={len(_scatter)} instances={total}")


def tiles(x0, x1, y0, y1, z, mat, size=500.0):
    """Visual floor: SM_Plane_5x5m tiles covering the rectangle, 2 cm above the collision top."""
    nx, ny = max(1, math.ceil(abs(x1 - x0) / size)), max(1, math.ceil(abs(y1 - y0) / size))
    sx, sy = abs(x1 - x0) / nx, abs(y1 - y0) / ny
    for i in range(nx):
        for j in range(ny):
            cx, cy = min(x0, x1) + (i + 0.5) * sx, min(y0, y1) + (j + 0.5) * sy
            m, o, e = mesh_info(PLANE)
            s = V(sx / 500.0 * 1.01, sy / 500.0 * 1.01, 1.0)
            key = (PLANE, mat.get_path_name(), False, False, 0.0)
            _scatter.setdefault(key, []).append(unreal.Transform(location=V(cx - o.x * s.x, cy - o.y * s.y, z + 2.0), rotation=R(), scale=s))


def ramp_tiles(p0, p1, width, mat):
    """The ramp's floor as a grid of ~5 m tiles laid on the slope (one plane stretched over a whole ramp blew the
    floor pattern up to a single giant ornament)."""
    yaw, pitch, length = ramp_geom(p0, p1)
    m, o, e = mesh_info(PLANE)
    mid = [(p0[i] + p1[i]) / 2 for i in range(3)]
    p, yw = math.radians(pitch), math.radians(yaw)
    ax = (math.cos(p) * math.cos(yw), math.cos(p) * math.sin(yw), math.sin(p))       # along the slope
    ay = (-math.sin(yw), math.cos(yw), 0.0)                                           # sideways
    az = (-math.sin(p) * math.cos(yw), -math.sin(p) * math.sin(yw), math.cos(p))     # slope normal
    full_l, full_w = length + 40.0, width
    nl, nw = max(1, int(math.ceil(full_l / 500.0))), max(1, int(math.ceil(full_w / 500.0)))
    seg_l, seg_w = full_l / nl, full_w / nw
    s = V(seg_l / 500.0 * 1.01, seg_w / 500.0 * 1.01, 1.0)
    key = (PLANE, mat.get_path_name(), False, False, 0.0)
    for i in range(nl):
        for j in range(nw):
            u, v = -full_l / 2 + (i + 0.5) * seg_l, -full_w / 2 + (j + 0.5) * seg_w
            # cell centre on the slope, 3 cm above it, minus the plane's own pivot offset turned with the slope
            c = [mid[k] + ax[k] * u + ay[k] * v + az[k] * 3.0 - (ax[k] * o.x * s.x + ay[k] * o.y * s.y) for k in range(3)]
            _scatter.setdefault(key, []).append(unreal.Transform(location=V(*c), rotation=R(yaw=yaw, pitch=pitch), scale=s))


def mirror4(x, y):
    return [(x, y), (-x, y), (x, -y), (-x, -y)]


# ---- keep-out areas for anything that collides -----------------------------------------------------------------
CLEAR = []
SOLID = []   # footprints of hand-placed solid dressing (walls, statues, rocks, arches, braziers)


def in_solid(x, y, pad=0.0):
    return any(a - pad <= x <= b + pad and c - pad <= y <= d + pad for a, b, c, d in SOLID)


def clear_rect(x0, x1, y0, y1, mirror=True):
    pts = [(x0, x1, y0, y1)]
    if mirror:
        pts += [(-x1, -x0, y0, y1), (x0, x1, -y1, -y0), (-x1, -x0, -y1, -y0)]
    CLEAR.extend(pts)


def is_clear(x, y, pad=0.0):
    return not any(a - pad <= x <= b + pad and c - pad <= y <= d + pad for a, b, c, d in CLEAR)


def dress_rock(path, x, y, height, yaw, taken, collide=True):
    """A rock or ruin with its whole footprint (never wider than 2.2 x its height) clear of the keep-outs; the
    footprint keeps ground cover out of it (v18)."""
    m, o, e = mesh_info(path)
    if not m:
        return False
    for _ in range(2):
        s = min(height * 50.0 / max(e.z, 1.0), height * 2.2 * 50.0 / max(e.x, e.y, 1.0))
        r = max(e.x, e.y) * s * 0.85
        probes = [(x + dx * r, y + dy * r) for dx in (-1, 0, 1) for dy in (-1, 0, 1)]
        if all(is_clear(px, py, 30.0) and not on_ramp(px, py, 30.0) for px, py in probes) and not in_solid(x, y, 60.0):
            inst(path, x, y, -15, yaw=yaw, scale=s, collide=collide)
            SOLID.append((x - r * 0.9, x + r * 0.9, y - r * 0.9, y + r * 0.9))
            taken.append((x, y))
            return True
        height *= 0.7
    return False


def scatter_points(zones, n, min_dist, taken, pad=0.0):
    """Up to n random points in the zones (x0, x1, y0, y1), mirrored to 4 quadrants, spaced min_dist apart."""
    out = []
    tries = 0
    while len(out) < n and tries < n * 60:
        tries += 1
        x0, x1, y0, y1 = rng.choice(zones)
        x, y = rng.uniform(x0, x1), rng.uniform(y0, y1)
        quad = mirror4(x, y)
        if all(is_clear(px, py, pad) and not in_solid(px, py, 150.0) and all(math.hypot(px - tx, py - ty) >= min_dist for tx, ty in taken) for px, py in quad):
            out.extend(quad)
            taken.extend(quad)
    return out


# ---- the map ---------------------------------------------------------------------------------------------------
def build_collision():
    T = 300.0   # ground slab thickness (walls of the pit go down to -300)
    # ground at z = 0 around the pit (the pit and its east/west ramps are left open)
    block("Ground_North", -7000, 7000, 1500, 4600, -T, 0)
    block("Ground_South", -7000, 7000, -4600, -1500, -T, 0)
    for sx in (-1, 1):
        block(f"Ground_Side_{sx}", sx * 2900, sx * 7000, -1500, 1500, -T, 0)
        block(f"Ground_RampN_{sx}", sx * 2000, sx * 2900, 600, 1500, -T, 0)
        block(f"Ground_RampS_{sx}", sx * 2000, sx * 2900, -1500, -600, -T, 0)
    block("Pit_Floor", -2000, 2000, -1500, 1500, -T, -150)
    # base plateaus (+300) and their ramps: to the middle and down to both lanes
    for sx in (-1, 1):
        block(f"Plateau_{sx}", sx * 4400, sx * 7000, -2000, 2000, -T, 300)
        ramp(f"BaseRamp_Mid_{sx}", (sx * 4400, 0, 300), (sx * 3200, 0, 0), 1200)
        ramp_walls(f"BaseRamp_Mid_{sx}", (sx * 4400, 0, 300), (sx * 3200, 0, 0), 1200)
        for sy in (-1, 1):
            ramp(f"BaseRamp_Lane_{sx}_{sy}", (sx * 5700, sy * 2000, 300), (sx * 5700, sy * 3200, 0), 1200)
            ramp_walls(f"BaseRamp_Lane_{sx}_{sy}", (sx * 5700, sy * 2000, 300), (sx * 5700, sy * 3200, 0), 1200)
        # pit ramps east / west; low walls along their sides (the ground there stands above the slope, units
        # used to perch on that edge and push into it)
        ramp(f"PitRamp_{sx}", (sx * 2900, 0, 0), (sx * 2000, 0, -150), 1200)
        for sy in (-1, 1):
            block(f"PitRampWall_{sx}_{sy}", sx * 2000, sx * 2900, sy * 600, sy * 670, -150, 110)
    # ridges (+250) over the pit, ramps at the ends and from the lane in the middle
    for sy in (-1, 1):
        block(f"Ridge_{sy}", -2400, 2400, sy * 1500, sy * 2400, -T, 250)
        for sx in (-1, 1):
            ramp(f"RidgeRamp_End_{sx}_{sy}", (sx * 2400, sy * 1950, 250), (sx * 3500, sy * 1950, 0), 700)
            ramp_walls(f"RidgeRamp_End_{sx}_{sy}", (sx * 2400, sy * 1950, 250), (sx * 3500, sy * 1950, 0), 700)
        ramp(f"RidgeRamp_Lane_{sy}", (0, sy * 2400, 250), (0, sy * 3300, 0), 900)
        ramp_walls(f"RidgeRamp_Lane_{sy}", (0, sy * 2400, 250), (0, sy * 3300, 0), 900)
    # border
    block("Border_N", -7400, 7400, 4600, 4900, -T, 2500, visible=False)
    block("Border_S", -7400, 7400, -4900, -4600, -T, 2500, visible=False)
    block("Border_E", 7000, 7300, -4900, 4900, -T, 2500, visible=False)
    block("Border_W", -7300, -7000, -4900, 4900, -T, 2500, visible=False)
    # orb altar in the pit
    cyl_block("Orb_Altar", 0, 0, 380, -150, -125, visible=False)       # 25 cm: walkable (navmesh step 35 cm)
    # keep-out zones for trees and rocks
    clear_rect(-5900, 5900, 2900, 4100, mirror=False)
    clear_rect(-5900, 5900, -4100, -2900, mirror=False)
    clear_rect(1950, 4600, -1050, 1050, mirror=False)
    clear_rect(-4600, -1950, -1050, 1050, mirror=False)
    clear_rect(-2050, 2050, -1550, 1550, mirror=False)            # pit (hand placed)
    clear_rect(2300, 3650, 1500, 2400)                               # ridge end ramps
    clear_rect(-650, 650, 2350, 3350, mirror=False)
    clear_rect(-650, 650, -3350, -2350, mirror=False)
    clear_rect(4250, 7000, -2100, 2100, mirror=False)                # plateaus
    clear_rect(-7000, -4250, -2100, 2100, mirror=False)
    clear_rect(4950, 6450, 1950, 3350)                               # plateau lane ramps
    clear_rect(-2450, 2450, 1450, 2450, mirror=False)                # ridge tops (hand placed)
    clear_rect(-2450, 2450, -2450, -1450, mirror=False)


def build_floors():
    leafy = asset("/Game/KiteDemo/Environments/GroundTiles/LeafyPath/MI_Tile_LeafPathStones.MI_Tile_LeafPathStones")
    rocky = asset("/Game/KiteDemo/Environments/GroundTiles/RockyPath/MI_PSM_RockyPath_Tile.MI_PSM_RockyPath_Tile")
    pebbly = asset("/Game/KiteDemo/Environments/GroundTiles/PebblyRiverbank/MI_PSM_PebblyRiverbank_Tile.MI_PSM_PebblyRiverbank_Tile")
    stone = asset("/Game/ParagonProps/Monolith/Ruins/Materials/MI_Ruins_DecoFloor.MI_Ruins_DecoFloor")
    stone2 = asset("/Game/ParagonProps/Monolith/Ruins/Materials/MI_Ruins_BuffFloorB.MI_Ruins_BuffFloorB")
    ruins = asset("/Game/ParagonProps/Monolith/Ruins/Materials/MI_Ruins_BuffFloorA.MI_Ruins_BuffFloorA")
    # jungle floor everywhere at ground level, paths on top of it
    tiles(-7000, 7000, 1500, 4600, 0, leafy)
    tiles(-7000, 7000, -4600, -1500, 0, leafy)
    for sx in (-1, 1):
        tiles(sx * 2900, sx * 4400, -1500, 1500, 0, leafy)
        tiles(sx * 2000, sx * 2900, 600, 1500, 0, leafy)
        tiles(sx * 2000, sx * 2900, -1500, -600, 0, leafy)
        tiles(sx * 2950, sx * 4400, -550, 550, 4, rocky)                 # mid path to the pit ramp
        tiles(sx * 4400, sx * 7000, -2000, 2000, 300, stone)             # plateau
        ramp_tiles((sx * 4400, 0, 300), (sx * 3200, 0, 0), 1200, stone2)
        ramp_tiles((sx * 2900, 0, 0), (sx * 2000, 0, -150), 1200, ruins)
        for sy in (-1, 1):
            ramp_tiles((sx * 5700, sy * 2000, 300), (sx * 5700, sy * 3200, 0), 1200, stone2)
            ramp_tiles((sx * 2400, sy * 1950, 250), (sx * 3500, sy * 1950, 0), 700, rocky)
    for sy in (-1, 1):
        tiles(-5700, 5700, sy * 3150, sy * 3850, 4, rocky)               # side lanes
        tiles(-2400, 2400, sy * 1500, sy * 2400, 250, leafy)             # ridge tops
        ramp_tiles((0, sy * 2400, 250), (0, sy * 3300, 0), 900, rocky)
    tiles(-2000, 2000, -1500, 1500, -150, pebbly)                        # pit
    # the world beyond the border: one huge jungle floor so nothing floats in the void
    for x0, x1, y0, y1 in ((-40000, 40000, 4600, 40000), (-40000, 40000, -40000, -4600), (-40000, -7000, -4600, 4600), (7000, 40000, -4600, 4600)):
        tiles(x0, x1, y0, y1, -40, leafy, size=8000.0)
    return rocky


def build_bases():
    for team, sx in ((0, -1), (1, 1)):
        dawn = team == 0
        # floor medallion and the gate the team defends
        place(f"BaseFloor_{team}", "/Game/ParagonProps/Monolith/Dawn/Meshes/Dawn_Inhibitor_Ring_B.Dawn_Inhibitor_Ring_B" if dawn else
              "/Game/ParagonProps/Monolith/Dusk/Meshes/SternInhibitorRing1.SternInhibitorRing1", sx * 5700, 0, 300.5, size=(24.0, 24.0, 0.05))
        place(f"BaseGate_{team}", "/Game/ParagonProps/Monolith/Dawn/Meshes/Dawn_Gate.Dawn_Gate" if dawn else
              "/Game/ParagonProps/Monolith/Dusk/Meshes/Evil_Gate_A.Evil_Gate_A", sx * 6750, 0, 300, yaw=90.0, height=12.0, collide=True)
        # plateau cliff faces: walls in the team's style
        wall = "/Game/ParagonProps/Monolith/Dawn/Meshes/Dawn_Wall_Simple_Straight_A.Dawn_Wall_Simple_Straight_A" if dawn else \
            "/Game/ParagonProps/Monolith/Dusk/Meshes/Evil_Gate_Wall_Str8_B.Evil_Gate_Wall_Str8_B"
        for y0, y1 in ((-2000, -600), (600, 2000)):
            place(f"BaseFace_Front_{team}_{y0}", wall, sx * 4348,   # just outside the plateau face (no coplanar flicker)
                  (y0 + y1) / 2, -20, yaw=90.0, size=(abs(y1 - y0) / 100.0, 1.0, 3.4), collide=True)
        for sy in (-1, 1):
            for x0, x1 in ((4400, 5100), (6300, 7000)):
                place(f"BaseFace_Side_{team}_{sy}_{x0}", wall, sx * (x0 + x1) / 2, sy * 2050, -20, size=((x1 - x0) / 100.0, 1.0, 3.4), collide=True)
            # sconces with light at the back corners, turrets at the front corners
            place(f"BaseSconce_{team}_{sy}", "/Game/ParagonProps/Monolith/Dawn/Meshes/DawnSide_Sconce.DawnSide_Sconce" if dawn else
                  "/Game/ParagonProps/Monolith/Dusk/Meshes/SM_Dusk_WallA_Endcap_Firepit.SM_Dusk_WallA_Endcap_Firepit", sx * 6500, sy * 1650, 300, height=5.0, collide=True)
            point_light((sx * 6500, sy * 1650, 700), (0.35, 0.6, 1.0) if dawn else (1.0, 0.35, 0.12), 8000.0, 2000.0)
            place(f"BasePillar_{team}_{sy}", "/Game/ParagonProps/Monolith/Dawn/Meshes/Pillar_Large.Pillar_Large" if dawn else
                  "/Game/ParagonProps/Monolith/Dusk/Meshes/Evil_Inhibitor_Gate_Sconce_A.Evil_Inhibitor_Gate_Sconce_A", sx * 4750, sy * 1550, 300, height=5.5, collide=True)
        # towers and walls behind the base (backdrop)
        for sy in (-1, 1):
            place(f"BaseTower_{team}_{sy}", "/Game/ParagonProps/Monolith/Dawn/Meshes/Dawn_Tower.Dawn_Tower" if dawn else
                  "/Game/ParagonProps/Monolith/Dusk/Meshes/Grim_Barbican_A.Grim_Barbican_A", sx * 7900, sy * 3000, -40, height=20.0)
        back = "/Game/ParagonProps/Monolith/Dawn/Meshes/Dawn_Wall_Elaborate_Straight_A.Dawn_Wall_Elaborate_Straight_A" if dawn else \
            "/Game/ParagonProps/Monolith/Dusk/Meshes/Dusk_WallA_Strait01.Dusk_WallA_Strait01"
        for i, y in enumerate(range(-4500, 4501, 1000)):
            if abs(y) < 900:
                continue
            place(f"BaseBackWall_{team}_{i}", back, sx * 7250, y, -40, yaw=90.0, size=(10.2, 3.0, 9.0))
        point_light((sx * 5700, 0, 900), (0.4, 0.65, 1.0) if dawn else (1.0, 0.3, 0.1), 9000.0, 2600.0)
        ps = spawn(unreal.PlayerStart, (sx * 5700, 0, 420), R(yaw=0.0 if dawn else 180.0))
        ps.set_editor_property("player_start_tag", "BaseA" if dawn else "BaseB")


def build_pit():
    ruins = "/Game/ParagonProps/Monolith/Ruins/Meshes/"
    place("PitMedallion", ruins + "Ruins_DecoFloor.Ruins_DecoFloor", 0, 0, -149, size=(13.0, 13.0, 0.06))
    place("OrbAltar", ruins + "Jungle_DiamondBase_Deco.Jungle_DiamondBase_Deco", 0, 0, -150, size=(7.6, 7.6, 0.25))
    place("OrbRing", "/Game/ParagonProps/Monolith/Dawn/Meshes/Dawn_Core_Ring_A.Dawn_Core_Ring_A", 0, 0, -123, size=(9.0, 9.0, 0.02))
    # north/south pit walls = the ridge cliffs (4 m, -150..250)
    for sy in (-1, 1):
        for i, x in enumerate(range(-1600, 1601, 800)):
            place(f"PitWall_{sy}_{i}", ruins + ("JungleWall_02A.JungleWall_02A" if i % 2 else "JungleWall_02B.JungleWall_02B"),
                  x, sy * 1545, -150, yaw=0.0 if sy > 0 else 180.0, size=(8.1, 1.0, 4.05))
        # railing on the ridge edge
        for i, x in enumerate(range(-2250, 2251, 250)):
            if i % 4 == 3:
                continue
            place(f"RidgeRail_{sy}_{i}", ruins + "JungleTrim01_250.JungleTrim01_250", x, sy * 1530, 250, size=(2.5, 0.5, 0.8), collide=True)
    # east/west pit edges (1.5 m) beside the ramps
    for sx in (-1, 1):
        for y0, y1 in ((-1500, -600), (600, 1500)):
            place(f"PitEdge_{sx}_{y0}", ruins + "JungleWall_02B.JungleWall_02B", sx * 2045, (y0 + y1) / 2, -150, yaw=90.0, size=(9.0, 1.0, 1.65))
        # braziers at the top of the ramps (the ramp sides are the rock walls PitRampWall; a rail there sat inside them)
        for sy in (-1, 1):
            place(f"Brazier_{sx}_{sy}", "/Game/ParagonProps/Monolith/Dusk/Meshes/SM_Dusk_WallA_Endcap_Firepit.SM_Dusk_WallA_Endcap_Firepit",
                  sx * 3100, sy * 1150, 0, height=2.6, collide=True)   # clear of the ramp corridor (it narrowed the pass at 760)
            point_light((sx * 3100, sy * 1150, 330), (1.0, 0.5, 0.18), 6000.0, 1500.0)
    # statues in the corners, pillar blocks as cover (their own collision)
    for (x, y) in mirror4(1650, 1150):
        place(f"PitStatue_{x}_{y}", ruins + "MonoStatue.MonoStatue", x, y, -150, yaw=45.0 if x * y > 0 else -45.0, height=5.0, collide=True)
    for i, (x, y) in enumerate(mirror4(900, 620)):
        place(f"PitPillar_{i}", ruins + ("JunglePillarBlock_01A.JunglePillarBlock_01A" if i % 2 else "JunglePillarBlockCrumble01_A.JunglePillarBlockCrumble01_A"),
              x, y, -150, yaw=rng.uniform(0, 90), size=(2.2, 2.2, 2.3), collide=True)
    for sx in (-1, 1):
        place(f"PitRubble_{sx}", ruins + "JungleRubblePile_A.JungleRubblePile_A", sx * 1350, 0, -150, yaw=90.0, size=(3.0, 3.5, 0.6), collide=True)
        point_light((sx * 1200, 0, 250), (0.9, 0.7, 0.4), 2500.0, 1800.0)


def build_ridges_and_lanes():
    ruins = "/Game/ParagonProps/Monolith/Ruins/Meshes/"
    for sy in (-1, 1):
        # ridge face towards the lane: ruined windows and walls
        for i, x in enumerate(range(-2150, 2151, 500)):
            if abs(x) < 500:
                continue
            place(f"RidgeFace_{sy}_{i}", ruins + ("JungleWindow_01A.JungleWindow_01A" if i % 3 == 0 else "JungleWall_01A.JungleWall_01A"),
                  x, sy * 2440, -20, yaw=90.0, size=(1.0, 5.0, 2.75), collide=True)
        # lane cover: boulders and a rock arch over each lane
        for sx in (-1, 1):
            place(f"LaneRock_A_{sx}_{sy}", "/Game/KiteDemo/Environments/Rocks/Large_Volcanic_Rock_001/LargeVolcanicRock_001.LargeVolcanicRock_001",
                  sx * 2400, sy * 3300, -30, yaw=rng.uniform(0, 360), size=(2.6, 3.4, 1.7), collide=True)
            place(f"LaneRock_B_{sx}_{sy}", "/Game/KiteDemo/Environments/Rocks/Medium_Boulder_001/Medium_Boulder_001.Medium_Boulder_001",
                  sx * 4100, sy * 3750, -20, yaw=rng.uniform(0, 360), height=1.6, collide=True)
            place(f"LaneRock_C_{sx}_{sy}", "/Game/KiteDemo/Environments/Rocks/Mountain_RockFace_002/SM_MountainRock_Closed.SM_MountainRock_Closed",
                  sx * 1150, sy * 3850, -40, yaw=rng.uniform(0, 360), size=(2.8, 2.6, 2.4), collide=True)
            place(f"LaneArch_{sx}_{sy}", "/Game/ParagonProps/Agora/Props/Meshes/Rock_Formation_Strip_C_Arch_A.Rock_Formation_Strip_C_Arch_A",
                  sx * 3000, sy * 3500, -20, yaw=90.0, size=(11.0, 1.2, 8.0), collide=True)
    # mid paths between the plateaus and the pit: broken walls as cover
    for sx in (-1, 1):
        for sy in (-1, 1):
            place(f"MidCover_{sx}_{sy}", ruins + "Aclove_Wall_Broken.Aclove_Wall_Broken", sx * 3700, sy * 780, 0, yaw=90.0 * (sy + 1), size=(3.0, 3.0, 1.6), collide=True)


def build_jungle():
    kite = "/Game/KiteDemo/Environments/"
    taken = []
    border = [(-6900, 6900, 4150, 4550)]
    quad = [(3700, 4250, 1550, 2900), (2100, 3600, 2450, 2900), (2100, 4250, 1100, 1450), (700, 2300, 2500, 2900), (4350, 4900, 2150, 2900), (6500, 6950, 2150, 2900)]
    trees = [(kite + "Trees/HillTree_Tall_02/HillTree_Tall_02.HillTree_Tall_02", 17.0, 23.0),
             (kite + "Trees/ScotsPineTall_01/ScotsPineTall_01.ScotsPineTall_01", 16.0, 22.0)]
    for x, y in scatter_points(border, 14, 900, taken) + scatter_points(quad, 16, 700, taken):
        path, h0, h1 = rng.choice(trees)
        inst(path, x, y, -10, yaw=rng.uniform(0, 360), height=rng.uniform(h0, h1), collide=True, cull=0.0)
    # beyond the border: a dense treeline and cliffs
    for i, x in enumerate(range(-6600, 6601, 1900)):
        for sy in (-1, 1):
            place(f"BorderCliff_{i}_{sy}", kite + "Cliffs/Cliff01/SM_Cliff01.SM_Cliff01", x, sy * 5150, -300, yaw=180.0 if sy > 0 else 0.0,
                  size=(21.0, 7.0, 13.0))
    for i, y in enumerate(range(-3800, 3801, 1900)):
        for sx in (-1, 1):
            place(f"BackCliff_{i}_{sx}", kite + "Cliffs/Cliff01/SM_Cliff01.SM_Cliff01", sx * 8600, y, -300, yaw=90.0 if sx > 0 else -90.0,
                  size=(21.0, 7.0, 16.0))
    # forest in a band all around the arena (visual only)
    for x in range(-13000, 13001, 750):
        for y in range(5300, 12001, 700):
            for sy in (-1, 1):
                if rng.random() < 0.8:
                    path, h0, h1 = rng.choice(trees)
                    inst(path, x + rng.uniform(-300, 300), sy * (y + rng.uniform(-250, 250)), -40, yaw=rng.uniform(0, 360), height=rng.uniform(h0 + 2, h1 + 6), shadows=False)
    for x in range(7700, 13001, 700):
        for y in range(-5000, 5001, 750):
            for sx in (-1, 1):
                if rng.random() < 0.8:
                    path, h0, h1 = rng.choice(trees)
                    inst(path, sx * (x + rng.uniform(-250, 250)), y + rng.uniform(-300, 300), -40, yaw=rng.uniform(0, 360), height=rng.uniform(h0 + 2, h1 + 6), shadows=False)
    for x, y in mirror4(7350, 4950):
        place(f"CornerMonolith_{x}_{y}", "/Game/ParagonProps/Agora/Props/Meshes/Tri_Rock_1.Tri_Rock_1", x, y, -300, yaw=rng.uniform(0, 360), height=26.0)
    # rocks and fallen logs with collision
    rocks = [(kite + "Rocks/Medium_Boulder_001/Medium_Boulder_001.Medium_Boulder_001", 1.2, 1.8),
             (kite + "Rocks/Medium_Boulder_002/Medium_Boulder_LowPoly.Medium_Boulder_LowPoly", 1.2, 1.8),
             ("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_Small_02.SM_RockNordic_Small_02", 1.2, 1.6),
             (kite + "Trees/Tree_Stump_01/Tree_Stump_01.Tree_Stump_01", 1.6, 2.4),
             (kite + "Trees/Vegetation_Debris_002/SM_Vegetation_Debris_002.SM_Vegetation_Debris_002", 0.7, 0.9)]
    for x, y in scatter_points(border + quad, 14, 500, taken):
        path, h0, h1 = rng.choice(rocks)
        inst(path, x, y, -15, yaw=rng.uniform(0, 360), height=rng.uniform(h0, h1), collide=True)
    # (v18, operator: "more natural, less empty") rock outcrops of the Paragon Monolith rocks in the jungle pockets and
    # flat rocks and fallen branches over the open ground (no collision)
    drng = random.Random(1818)
    mono = "/Game/ParagonProps/Monolith/Rocks/Meshes/"
    big = [mono + f"SM_RockNordic_0{i}.SM_RockNordic_0{i}" for i in range(1, 9)]
    small = [mono + f"SM_RockNordic_Small_0{i}.SM_RockNordic_Small_0{i}" for i in range(1, 5)]
    outcrops = 0
    for cx, cy in scatter_points(quad + border, 12, 480, taken, pad=100.0)[::4]:
        b, bh, byaw = drng.choice(big), drng.uniform(1.6, 2.6), drng.uniform(0, 360)
        smalls = [(drng.choice(small), drng.uniform(-300, 300), drng.uniform(-300, 300), drng.uniform(0.8, 1.4), drng.uniform(0, 360)) for _ in range(drng.randint(2, 3))]
        for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
            x, y = sx * abs(cx), sy * abs(cy)
            outcrops += 1 if dress_rock(b, x, y, bh, byaw, taken) else 0
            for p, dx, dy, h, yw in smalls:
                dress_rock(p, x + sx * dx, y + sy * dy, h, yw, taken)
    floor_bits = [(kite + "Rocks/GroundRevealRock001/SM_GroundRevealRock001.SM_GroundRevealRock001", 2.0, 3.2),
                  (kite + "Trees/Vegetation_Debris_002/SM_Vegetation_Debris_002.SM_Vegetation_Debris_002", 2.5, 3.8)]
    open_ground = [(2000, 6800, 1600, 4100), (3000, 6800, 200, 1400)]
    bits = 0
    for x, y in scatter_points(open_ground, 40, 500, taken, pad=60.0):
        p, w0, w1 = drng.choice(floor_bits)
        m, o, e = mesh_info(p)
        if m and not on_ramp(x, y, 80.0):
            inst(p, x, y, -6, yaw=drng.uniform(0, 360), scale=drng.uniform(w0, w1) * 50.0 / max(e.x, e.y, 1.0), shadows=False, cull=6000.0)
            bits += 1
    print(f"ARENA_MAP dressing outcrops={outcrops} floor_bits={bits}")
    # ground cover: no collision, kept off the walking paths
    def ground_ok(x, y):
        if on_ramp(x, y, 60.0):
            return False                                   # slopes: a plant at ground height would float or stick out
        if abs(x) < 2125 and abs(y) < 1560:
            return False                                   # pit and the thickness of its edge walls
        if abs(x) > 4350 and abs(y) < 2050:
            return False                                   # plateaus
        if 3200 < abs(y) < 3800 and abs(x) < 5700:
            return False                                   # lane path
        if 2900 < abs(x) < 4400 and abs(y) < 600:
            return False                                   # mid path
        if abs(x) < 2450 and 1450 < abs(y) < 2450:
            return 1700 < abs(y) < 2300 and rng.random() < 0.25    # a little on the ridges
        return abs(x) < 6950 and abs(y) < 4580
    cover = [(kite + "Foliage/Grass/FieldGrass/SM_FieldGrass_01.SM_FieldGrass_01", 2600, 0.8, 1.4, 5000.0, False),
             (kite + "Foliage/Ferns/SM_Fern_01.SM_Fern_01", 260, 0.7, 1.3, 6000.0, False),
             (kite + "Foliage/Ferns/SM_Fern_02.SM_Fern_02", 220, 0.7, 1.3, 6000.0, False),
             (kite + "Foliage/Ferns/SM_Fern_03.SM_Fern_03", 200, 0.7, 1.2, 6000.0, False),
             (kite + "Foliage/Flowers/Heather/SM_Heather_Mesh_Clumps2.SM_Heather_Mesh_Clumps2", 320, 0.8, 1.3, 6000.0, False),
             (kite + "Foliage/Flowers/FieldScabious/SM_FieldScabious_01.SM_FieldScabious_01", 260, 0.8, 1.2, 5000.0, False),
             (kite + "Foliage/Flowers/Yarrow/SM_Yarrow3.SM_Yarrow3", 200, 0.9, 1.4, 5000.0, False),
             (kite + "Foliage/BogMyrtleBush_01/BogMyrtleBush_01.BogMyrtleBush_01", 160, 0.7, 1.1, 8000.0, True),   # kept under the follow camera
             (kite + "Foliage/BogMyrtleBush_02/BogMyrtleBush_02.BogMyrtleBush_02", 120, 0.7, 1.0, 8000.0, True),
             (kite + "Foliage/Leaves/SM_DeadLeaves_Flat.SM_DeadLeaves_Flat", 700, 0.8, 1.6, 4000.0, False),
             (kite + "Rocks/River_Rock_01/SM_River_Rock_01.SM_River_Rock_01", 160, 0.6, 1.4, 4500.0, False),
             (kite + "Rocks/Scree002/SM_Scree002b.SM_Scree002b", 40, 0.5, 0.8, 8000.0, False)]
    for path, n, s0, s1, cull, shadows in cover:
        placed = 0
        while placed < n:
            x, y = rng.uniform(-6950, 6950), rng.uniform(-4580, 4580)
            if not ground_ok(x, y) or in_solid(x, y, 40.0):
                continue
            z = 250 if (abs(x) < 2450 and 1450 < abs(y) < 2450) else 0
            inst(path, x, y, z - 2, yaw=rng.uniform(0, 360), scale=rng.uniform(s0, s1), shadows=shadows, cull=cull)
            placed += 1
    # grass grows in clumps (a uniform sprinkle reads as noise from the follow camera): clumps of 5-9 blades, a flower
    # patch in some, and a third kind of bush
    grass = kite + "Foliage/Grass/FieldGrass/SM_FieldGrass_01.SM_FieldGrass_01"
    buttercup = kite + "Foliage/Flowers/Buttercup/SM_Buttercup_Patch_01.SM_Buttercup_Patch_01"
    if not mesh_info(buttercup)[0]:
        buttercup = kite + "Foliage/Flowers/FieldScabious/SM_FieldScabious_01.SM_FieldScabious_01"
    clumps = 0
    while clumps < 560:
        cx, cy = rng.uniform(-6950, 6950), rng.uniform(-4580, 4580)
        if not ground_ok(cx, cy) or in_solid(cx, cy, 60.0):
            continue
        z = 250 if (abs(cx) < 2450 and 1450 < abs(cy) < 2450) else 0
        for _ in range(rng.randint(5, 9)):
            x, y = cx + rng.gauss(0, 75), cy + rng.gauss(0, 75)
            if ground_ok(x, y) and not in_solid(x, y, 40.0):
                inst(grass, x, y, z - 2, yaw=rng.uniform(0, 360), scale=rng.uniform(1.0, 1.7), shadows=False, cull=5500.0)
        if rng.random() < 0.3:
            inst(buttercup, cx + rng.uniform(-60, 60), cy + rng.uniform(-60, 60), z - 2, yaw=rng.uniform(0, 360), scale=rng.uniform(0.8, 1.2), shadows=False, cull=5000.0)
        clumps += 1
    placed = 0
    while placed < 110:
        x, y = rng.uniform(-6950, 6950), rng.uniform(-4580, 4580)
        if not ground_ok(x, y) or in_solid(x, y, 80.0):
            continue
        inst(kite + "Foliage/BogMyrtle_01/BogMyrtle_01.BogMyrtle_01", x, y, (250 if (abs(x) < 2450 and 1450 < abs(y) < 2450) else 0) - 2,
             yaw=rng.uniform(0, 360), scale=rng.uniform(0.7, 1.1), shadows=True, cull=8000.0)
        placed += 1
    # ferns and leaves at the foot of the pit walls
    for i in range(80):
        x, y = rng.uniform(-1900, 1900), rng.choice((-1, 1)) * rng.uniform(1250, 1450)
        if abs(x) < 500 or in_solid(x, y, 40.0):
            continue
        inst(kite + "Foliage/Ferns/SM_Fern_01.SM_Fern_01", x, y, -152, yaw=rng.uniform(0, 360), scale=rng.uniform(0.6, 1.0), cull=6000.0)


def build_edges():
    """MOBA maps never show a ruler-straight seam: the edges of the lane paths, the foot of the plateau, ridge and pit
    walls and the outer sides of the ramps are grown over with grass, ferns, leaves and stones (visual only)."""
    kite = "/Game/KiteDemo/Environments/"
    grass = kite + "Foliage/Grass/FieldGrass/SM_FieldGrass_01.SM_FieldGrass_01"
    ferns = [kite + "Foliage/Ferns/SM_Fern_0%d.SM_Fern_0%d" % (n, n) for n in (1, 2, 3)]
    leaves = kite + "Foliage/Leaves/SM_DeadLeaves_Flat.SM_DeadLeaves_Flat"
    pebble = kite + "Rocks/River_Rock_01/SM_River_Rock_01.SM_River_Rock_01"
    scree = kite + "Rocks/Scree002/SM_Scree002b.SM_Scree002b"
    heather = kite + "Foliage/Flowers/Heather/SM_Heather_Mesh_Clumps2.SM_Heather_Mesh_Clumps2"
    n = {"k": 0}

    def tuft(x, y, z, rich=True):
        if on_ramp(x, y, 20.0) or in_solid(x, y, 30.0):
            return
        if z > -100 and abs(x) < 2125 and abs(y) < 1560:
            return                                         # ground-level plant over the pit or in its edge walls
        r = rng.random()
        if r < 0.45:
            inst(grass, x, y, z - 2, yaw=rng.uniform(0, 360), scale=rng.uniform(0.9, 1.5), shadows=False, cull=5000.0)
        elif r < 0.65:
            inst(rng.choice(ferns), x, y, z - 2, yaw=rng.uniform(0, 360), scale=rng.uniform(0.6, 1.1), shadows=False, cull=6000.0)
        elif r < 0.8:
            inst(leaves, x, y, z - 1, yaw=rng.uniform(0, 360), scale=rng.uniform(0.9, 1.5), shadows=False, cull=4000.0)
        elif r < 0.92 or not rich:
            inst(pebble, x, y, z - 6, yaw=rng.uniform(0, 360), scale=rng.uniform(0.6, 1.3), shadows=False, cull=4500.0)
        else:
            inst(heather, x, y, z - 2, yaw=rng.uniform(0, 360), scale=rng.uniform(0.7, 1.0), shadows=False, cull=6000.0)
        n["k"] += 1

    def band(x0, x1, y0, y1, z, count, rich=True):
        for _ in range(count):
            tuft(rng.uniform(x0, x1), rng.uniform(y0, y1), z, rich)

    for sy in (-1, 1):
        # lane path edges (rocky path z 4 over the jungle floor z 2): a band straddling each seam
        for edge in (3150, 3850):
            inner = sy * (edge + (25 if edge == 3150 else -25))
            outer = sy * (edge + (140 if edge == 3850 else -140))
            band(-5700, 5700, min(inner, outer), max(inner, outer), 0, 420)
        # foot of the ridge face on the lane side, foot of the plateau side walls, foot of the border
        band(-2400, 2400, min(sy * 2465, sy * 2650), max(sy * 2465, sy * 2650), 0, 260)
        band(-1950, 1950, min(sy * 1390, sy * 1478), max(sy * 1390, sy * 1478), -150, 160)      # inside the pit
        band(-6950, 6950, min(sy * 4420, sy * 4585), max(sy * 4420, sy * 4585), 0, 380)
        for sx in (-1, 1):
            band(min(sx * 4400, sx * 7000), max(sx * 4400, sx * 7000), min(sy * 2105, sy * 2250), max(sy * 2105, sy * 2250), 0, 150)
            # screes where rock meets ground: at the corners of the plateau and under the ridge ends
            for _ in range(3):
                inst(scree, sx * rng.uniform(4170, 4280), sy * rng.uniform(1100, 1950), -8, yaw=rng.uniform(0, 360), scale=rng.uniform(0.5, 0.8), shadows=False, cull=8000.0)
    for sx in (-1, 1):
        # foot of the plateau front walls, both sides of the mid ramp
        for y0, y1 in ((-2000, -700), (700, 2000)):
            band(min(sx * 4150, sx * 4292), max(sx * 4150, sx * 4292), y0, y1, 0, 110)
        # pit edges beside the pit ramps
        for y0, y1 in ((-1500, -760), (760, 1500)):
            band(min(sx * 2115, sx * 2250), max(sx * 2115, sx * 2250), y0, y1, 0, 60)   # outside the pit-edge wall (1995..2095)
    # outer sides of every ramp at ground level (the parapet foot)
    for x0, x1, y0, y1, along_x in RAMPS:
        # only the two long sides (the parapet feet); the ends are the ramp's top and foot, at other heights
        for side in (0, 1):
            if along_x:
                yy = (y0 - 60, y0 + 10) if side == 0 else (y1 - 10, y1 + 60)
                band(x0 + 80, x1 - 80, yy[0], yy[1], 0, int((x1 - x0) / 60), rich=False)   # clear of both ends (the foot may be in the pit)
            else:
                xx = (x0 - 60, x0 + 10) if side == 0 else (x1 - 10, x1 + 60)
                band(xx[0], xx[1], y0 + 80, y1 - 80, 0, int((y1 - y0) / 60), rich=False)
    print(f"ARENA_MAP edges={n['k']}")


SKIRT_DIR = "/Game/Arena/Generated/Env/"
SKIRTS = []   # footprints (x0, x1, y0, y1) of the rock skirts: ground cover inside them is dropped


def skirt(label, name, x, y, yaw, flip=1.0, depth=1.0, height=1.0):
    """one procedural rock skirt (Tools/Blender/rock_skirts.py) with its pivot on the wall line at ground level;
    in UE the rock protrudes towards the mesh's +Y (see import_env_meshes.py), so yaw turns +Y onto the wall's
    outward normal. Visual only: no collision, no navmesh change; M_ArenaCliff continues the wall's rock."""
    m = unreal.load_asset(f"{SKIRT_DIR}{name}.{name}")
    if not m:
        return None
    a = spawn(unreal.StaticMeshActor, (x, y, 0.0), R(yaw=yaw), label=label)
    c = a.static_mesh_component
    c.set_static_mesh(m)
    c.set_world_scale3d(V(flip, depth, height))
    if CLIFF_MAT:
        c.set_material(0, CLIFF_MAT)
    c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    c.set_editor_property("can_ever_affect_navigation", False)
    b = m.get_bounds()
    ex, ey = b.box_extent.x, b.box_extent.y * 2.0 * depth
    cs, sn = abs(math.cos(math.radians(yaw))), abs(math.sin(math.radians(yaw)))
    hx, hy = ex * cs + ey * sn, ex * sn + ey * cs
    SKIRTS.append((x - hx, x + hx, y - hy, y + hy))
    return a


def build_skirts():
    """Rock skirts at the outer foot of the ramp parapets: where a parapet stands tall over the ground (the upper part
    of a ramp to a plateau or a ridge) its box met the ground in a ruler-straight seam. (The plateau faces carry the
    bases' walls, the ridge faces ruins: no skirts there.) Their own random generator: the jungle keeps its layout."""
    if not unreal.EditorAssetLibrary.does_asset_exist(SKIRT_DIR + "SM_RockSkirt_A"):
        print("ARENA_MAP WARN no rock skirts (run Tools/Blender/rock_skirts.py and Tools/import_env_meshes.py)")
        return
    srng = random.Random(1234)
    pieces = [("SM_RockSkirt_B", 600.0), ("SM_RockSkirt_A", 400.0), ("SM_RockSkirt_C", 300.0)]
    for idx, (p0, p1, width, thick, above) in enumerate(PARAPETS):
        if p1[2] > 1.0 or p0[2] <= 0.0:
            continue                                        # only ramps that climb from the ground
        yaw, pitch, length = ramp_geom(p0, p1)
        run_len = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
        ux, uy = (p1[0] - p0[0]) / run_len, (p1[1] - p0[1]) / run_len      # down the ramp
        nx, ny = -uy, ux                                                   # sideways
        # the tall part: the parapet top (ramp surface + above) at least 1.8 m over the ground; not at the very top,
        # where the plateau or ridge wall stands
        t_end = min(0.85, 1.0 - (180.0 - above) / p0[2]) if p0[2] > 0 else 0.0
        for side in (-1, 1):
            off = side * (width / 2.0 + thick)
            normal_yaw = math.degrees(math.atan2(ny * side, nx * side))
            d = run_len * 0.1
            k = 0
            while True:
                name, plen = srng.choice(pieces)
                if d + plen > run_len * t_end:
                    name, plen = pieces[2]
                    if d + plen > run_len * t_end:
                        break
                mid = d + plen / 2.0
                x = p0[0] + ux * mid + nx * off
                y = p0[1] + uy * mid + ny * off
                # smaller towards the foot of the ramp, where the parapet is lower
                h = 1.0 - 0.35 * (mid / max(run_len * t_end, 1.0))
                skirt(f"Skirt_{idx}_{side}_{k}", name, x, y, normal_yaw - 90.0, flip=srng.choice((1.0, -1.0)),
                      depth=srng.uniform(0.75, 0.95), height=h * srng.uniform(0.9, 1.1))
                d += plen - srng.uniform(20.0, 60.0)
                k += 1
    # ground cover inside a rock would poke through it
    dropped = 0
    for key, xs in _scatter.items():
        keep = [t for t in xs if not any(a - 10 <= t.translation.x <= b + 10 and c - 10 <= t.translation.y <= d + 10 and t.translation.z < 120
                                         for a, b, c, d in SKIRTS)]
        dropped += len(xs) - len(keep)
        xs[:] = keep
    print(f"ARENA_MAP skirts={len(SKIRTS)} cover_dropped={dropped}")


def build_horizon(rx, ry, n, h0, h1, seed=4242):
    """A ring of KiteDemo cliffs (one scale, hazy in the fog) closing the valley: the flat ground beyond the forest
    showed to the horizon from the menu, hero-select and overview cameras (v17). Visual only, no shadows."""
    hrng = random.Random(seed)
    cliff = "/Game/KiteDemo/Environments/Cliffs/Cliff01/SM_Cliff01.SM_Cliff01"
    for k in range(n):
        a = 2.0 * math.pi * (k + hrng.uniform(-0.2, 0.2)) / n
        x, y = rx * math.cos(a), ry * math.sin(a)
        # the cliff's face (+Y in the mesh: yaw 0 on the south edge faces north) turned towards the valley
        tangent = math.degrees(math.atan2(ry * math.cos(a), -rx * math.sin(a)))
        c = place(f"Horizon_{k}", cliff, x, y, -1500, yaw=tangent + hrng.uniform(-10, 10), height=hrng.uniform(h0, h1))
        if c:
            m = c.static_mesh_component
            m.set_editor_property("cast_shadow", False)
            m.set_collision_profile_name("NoCollision")


def build_skyline():
    # (v17: the Dusk spire read as a flat black slab over the lanes; a Dusk barbican stands behind the ring instead)
    far = [("/Game/ParagonProps/Monolith/Dusk/Meshes/Grim_Barbican_A.Grim_Barbican_A", 42000, 0, 95.0, 90.0),
           ("/Game/ParagonProps/Monolith/Dawn/Meshes/Dawn_TermTower1.Dawn_TermTower1", -30000, 0, 60.0, 90.0),
           ("/Game/ParagonProps/Agora/Props/Meshes/TerminusPillarBigYins.TerminusPillarBigYins", -16000, 9000, 40.0, 0.0),
           ("/Game/ParagonProps/Agora/Props/Meshes/TerminusPillarBigYins.TerminusPillarBigYins", 16000, -9000, 40.0, 0.0),
           ("/Game/ParagonProps/Agora/Props/Meshes/Tri_Rock_1.Tri_Rock_1", -9000, -15000, 60.0, 30.0),
           ("/Game/ParagonProps/Agora/Props/Meshes/Tri_Rock_1.Tri_Rock_1", 11000, 15000, 70.0, 200.0)]
    for i, (path, x, y, h, yaw) in enumerate(far):
        place(f"Skyline_{i}", path, x, y, -2000, yaw=yaw, height=h)


LIGHT_SCALE = 0.035   # the old values were tuned against the grey default floor; candelas now: 8000 -> 280 cd


def point_light(loc, rgb, intensity=8000.0, radius=1800.0):
    a = spawn(unreal.PointLight, loc)
    c = a.point_light_component
    c.set_light_color(unreal.LinearColor(*rgb, 1.0))
    try:
        c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    except Exception as ex:  # noqa: BLE001
        print(f"ARENA_MAP WARN light units: {ex}")
    c.set_intensity(intensity * LIGHT_SCALE)
    c.set_attenuation_radius(radius)
    c.set_editor_property("cast_shadows", False)
    return a


def setp(obj, prop, val, what):
    try:
        obj.set_editor_property(prop, val)
    except Exception as ex:  # noqa: BLE001
        print(f"ARENA_MAP WARN {what} {prop}: {ex}")


def build_lighting():
    # a higher sun (-46 deg): shorter shadows keep the lanes readable (at -32 the walls and trees striped them)
    sun = spawn(unreal.DirectionalLight, (0, 0, 3000), R(yaw=35.0, pitch=-46.0), label="Sun")
    lc = sun.light_component
    lc.set_intensity(10.0)
    lc.set_mobility(unreal.ComponentMobility.MOVABLE)
    lc.set_light_color(unreal.LinearColor(1.0, 0.9, 0.78, 1.0))   # warm, not orange (orange sun + bias turned the grass yellow)
    setp(lc, "atmosphere_sun_light", True, "sun")
    # contact shadows: feet, grass and props sit on the ground; distance-field shadows past the cascades: the far
    # trees and cliffs still cast (cheap, from the mesh distance fields)
    setp(lc, "contact_shadow_length", 0.03, "sun")
    setp(lc, "use_ray_traced_distance_field_shadows", True, "sun")
    setp(lc, "distance_field_shadow_distance", 40000.0, "sun")
    setp(lc, "dynamic_shadow_distance_movable_light", 9000.0, "sun")
    setp(lc, "dynamic_shadow_cascades", 3, "sun")
    setp(lc, "cascade_distribution_exponent", 2.6, "sun")
    setp(lc, "light_source_angle", 0.8, "sun")
    spawn(unreal.SkyAtmosphere, (0, 0, 0), label="Sky")
    sky = spawn(unreal.SkyLight, (0, 0, 1000), label="SkyLight")
    sc = sky.light_component
    try:
        sc.set_editor_property("real_time_capture", True)
    except Exception as ex:  # noqa: BLE001
        print(f"ARENA_MAP WARN skylight: {ex}")
    # Lumen bounces the sun: the sky light only fills what it cannot reach (2.0 flattened every shadow blue-grey)
    sc.set_intensity(1.2)
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, -300), label="Fog")
    fc = fog.component
    fc.set_editor_property("fog_density", 0.03)
    fc.set_editor_property("fog_height_falloff", 0.2)
    fc.set_editor_property("start_distance", 3500.0)
    # a light volumetric haze on High / Epic (the scalability turns it off below): sun shafts between the trees
    setp(fc, "enable_volumetric_fog", True, "fog")
    setp(fc, "volumetric_fog_scattering_distribution", 0.6, "fog")
    setp(fc, "volumetric_fog_extinction_scale", 0.35, "fog")
    setp(fc, "volumetric_fog_distance", 6000.0, "fog")
    ppv = spawn(unreal.PostProcessVolume, (0, 0, 0), label="Post")
    ppv.set_editor_property("unbound", True)
    s = ppv.settings
    for prop, val in (
            # exposure (EV100; the sun is 10 lux, mid-grey grass meters about EV 1): adapts within a narrow range only,
            # never brighter than EV 1 (a bright plaza filling the view burned white),
            # a shaded ramp went murky); local exposure keeps the sunlit stone and the shade both readable
            ("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM),
            ("override_auto_exposure_bias", True), ("auto_exposure_bias", 0.0),
            ("override_auto_exposure_min_brightness", True), ("auto_exposure_min_brightness", 1.0),
            ("override_auto_exposure_max_brightness", True), ("auto_exposure_max_brightness", 3.5),
            ("override_auto_exposure_speed_up", True), ("auto_exposure_speed_up", 2.0),
            ("override_auto_exposure_speed_down", True), ("auto_exposure_speed_down", 1.2),
            ("override_local_exposure_highlight_contrast_scale", True), ("local_exposure_highlight_contrast_scale", 0.72),
            ("override_local_exposure_shadow_contrast_scale", True), ("local_exposure_shadow_contrast_scale", 0.85),
            ("override_local_exposure_detail_strength", True), ("local_exposure_detail_strength", 1.1),
            ("override_bloom_intensity", True), ("bloom_intensity", 0.4),
            ("override_vignette_intensity", True), ("vignette_intensity", 0.35),
            # a touch more colour and contrast than the neutral default (Paragon's art is saturated)
            ("override_color_saturation", True), ("color_saturation", unreal.Vector4(1.0, 1.0, 1.0, 1.08)),
            ("override_color_contrast", True), ("color_contrast", unreal.Vector4(1.0, 1.0, 1.0, 1.06)),
            ("override_ambient_occlusion_intensity", True), ("ambient_occlusion_intensity", 0.6),
            # Lumen: its defaults, a longer reach for the reflections on the plazas' stone
            ("override_lumen_scene_lighting_quality", True), ("lumen_scene_lighting_quality", 1.0),
            ("override_lumen_final_gather_quality", True), ("lumen_final_gather_quality", 1.0),
            ("override_lumen_max_trace_distance", True), ("lumen_max_trace_distance", 12000.0)):
        setp(s, prop, val, "ppv")
    ppv.set_editor_property("settings", s)


def look(frm, to):
    dx, dy, dz = to[0] - frm[0], to[1] - frm[1], to[2] - frm[2]
    return R(yaw=math.degrees(math.atan2(dy, dx)), pitch=math.degrees(math.atan2(dz, math.hypot(dx, dy))))


def camera(label, frm, to, tags, fov=80.0):
    c = spawn(unreal.CameraActor, frm, look(frm, to), label=label)
    c.set_editor_property("tags", tags)
    c.camera_component.set_editor_property("field_of_view", fov)
    return c


BLOCKS = ["/Game/ParagonProps/Monolith/Ruins/Meshes/JunglePillarBlockPiece_04A.JunglePillarBlockPiece_04A",
          "/Game/ParagonProps/Monolith/Ruins/Meshes/JunglePillarBlockPiece_04B.JunglePillarBlockPiece_04B",
          "/Game/ParagonProps/Monolith/Ruins/Meshes/JunglePillarBlockPiece_04C.JunglePillarBlockPiece_04C",
          "/Game/ParagonProps/Monolith/Ruins/Meshes/JunglePillarBlockPiece_05A.JunglePillarBlockPiece_05A"]


def build_gameplay_actors(rock_mat):
    # physics props: crates and barrels of Agora rock that blasts and dashes scatter
    props = 0
    spots = [(3300, 350), (3500, -420), (3900, 250), (1000, 1150), (1150, -1150), (2400, 3700), (4600, 3700), (3350, 3300)]
    for i, (x, y) in enumerate(spots):
        for sx in (-1, 1):
            for k in range(2 if i < 3 else 1):
                sy = 1 if (i + k) % 2 else -1
                px, py = sx * x + k * 90, (y if i < 5 else sy * y)
                z = -150 if abs(px) < 2000 and abs(py) < 1500 else 0
                a = spawn(unreal.StaticMeshActor, (px, py, z + 60 + k * 110), R(yaw=rng.uniform(0, 90)), label=f"Prop_{i}_{sx}_{k}")
                c = a.static_mesh_component
                crate = (i + k) % 2 == 0
                block_mesh = BLOCKS[(i * 2 + k + (sx > 0)) % len(BLOCKS)]
                bm, bo, be = mesh_info(block_mesh)
                c.set_static_mesh(bm or CUBE)
                c.set_world_scale3d(V(0.8 * 50.0 / max(be.x, 1.0), 0.8 * 50.0 / max(be.y, 1.0), 0.8 * 50.0 / max(be.z, 1.0)) if bm else V(0.8, 0.8, 0.8))
                c.set_mobility(unreal.ComponentMobility.MOVABLE)
                c.set_collision_profile_name("PhysicsActor")
                c.set_simulate_physics(True)
                c.set_editor_property("can_ever_affect_navigation", False)
                c.set_editor_property("can_character_step_up_on", unreal.CanBeCharacterBase.ECB_NO)
                c.set_mass_override_in_kg("None", 40.0 if crate else 30.0, True)
                props += 1
    print(f"ARENA_MAP props={props}")
    # jump pads: pit -> ridges, lanes -> over the ridge into the pit
    pad_mesh = "/Game/ParagonProps/Monolith/Dawn/Meshes/SM_JumpPad_LP2.SM_JumpPad_LP2"
    for sy in (-1, 1):
        for label, loc, yaw, vel in ((f"PadPit_{sy}", (0, sy * 1100, -150), 90.0 * sy, (650.0, 0.0, 1150.0)),
                                     (f"PadLaneW_{sy}", (-1500, sy * 3150, 0), -90.0 * sy, (1100.0, 0.0, 1200.0)),
                                     (f"PadLaneE_{sy}", (1500, sy * 3150, 0), -90.0 * sy, (1100.0, 0.0, 1200.0))):
            pad = spawn(unreal.ArenaJumpPad, loc, R(yaw=yaw), label=label)
            pad.set_editor_property("launch_velocity", V(*vel))
            for c in pad.get_components_by_class(unreal.StaticMeshComponent):
                c.set_hidden_in_game(True)
                c.set_visibility(False)
            place(f"{label}_Mesh", pad_mesh, loc[0], loc[1], loc[2] + 2, yaw=yaw, width=2.8)
    # orb spot, minion lanes (ordered from base A to base B), cameras
    orb = spawn(unreal.TargetPoint, (0, 0, -30), label="OrbSpot")
    orb.set_editor_property("tags", ["OrbSpot"])
    lanes = {"Mid": [(-4000, 0, 20), (-2600, 0, 20), (0, -600, -130), (2600, 0, 20), (4000, 0, 20)],
             "North": [(-5700, 3500, 20), (-3000, 3600, 20), (0, 3700, 20), (3000, 3600, 20), (5700, 3500, 20)],
             "South": [(-5700, -3500, 20), (-3000, -3600, 20), (0, -3700, 20), (3000, -3600, 20), (5700, -3500, 20)]}
    for name, pts in lanes.items():
        for i, p in enumerate(pts):
            t = spawn(unreal.TargetPoint, p, label=f"Lane_{name}_{i:02d}")
            t.set_editor_property("tags", [f"Lane_{name}_{i:02d}"])
    camera("SelectCam", (0, -9800, 5200), (0, 0, -200), ["SelectCam"], fov=75.0)
    tour = [((-9500, -9000, 6500), (0, 0, 0), 70.0),
            ((-6300, 0, 900), (0, 0, 0), 85.0),
            ((0, -2700, 750), (0, 700, -150), 90.0),
            ((-5000, 3500, 380), (2000, 3500, 60), 85.0),
            ((2600, -1600, 700), (6000, 0, 400), 85.0),
            ((2500, 2300, 450), (4300, 4000, 0), 85.0),
            ((0, 0, 16000), (1, 0, 0), 85.0)]
    for i, (frm, to, fov) in enumerate(tour):
        c = camera(f"Tour_{i:02d}", frm, to, ["Tour", f"Tour_{i:02d}"], fov)
        if i == len(tour) - 1:
            c.set_actor_rotation(R(yaw=0.0, pitch=-90.0), False)
    nav = spawn(unreal.NavMeshBoundsVolume, (0, 0, 250), label="NavBounds")
    nav.set_actor_scale3d(V(72.0, 48.0, 8.0))
    # heroes use a 40 cm capsule: paths keep 42 cm off walls so nobody scrapes along or wedges into a corner
    for a in actors.get_all_level_actors():
        if isinstance(a, unreal.RecastNavMesh):
            try:
                a.set_editor_property("agent_radius", 42.0)
                print("ARENA_MAP navmesh agent_radius=42")
            except Exception as ex:  # noqa: BLE001
                print(f"ARENA_MAP WARN navmesh radius: {ex}")


def main():
    global BLOCK_MAT
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        if not levels.load_level(MAP):
            print("ARENA_MAP FAIL load_level")
            return
        for a in actors.get_all_level_actors():
            # everything but the world settings and the builder brush: volumes are brushes too, and a kept
            # post-process or nav volume piled up on every rebuild (an old manual exposure turned a map white)
            if not isinstance(a, unreal.WorldSettings) and (type(a) is not unreal.Brush or isinstance(a, unreal.Volume)):
                actors.destroy_actor(a)
    else:
        try:
            ok = levels.new_level(MAP, False)
        except TypeError:
            ok = levels.new_level(MAP)
        if not ok:
            print("ARENA_MAP FAIL new_level")
            return
    global CLIFF_MAT
    BLOCK_MAT = color_mat("MI_Stone", (0.16, 0.13, 0.15))
    CLIFF_MAT = asset(f"{MAT_DIR}/M_ArenaCliff.M_ArenaCliff") or BLOCK_MAT
    rock_mat = asset("/Game/ParagonProps/Agora/Rocks/Materials/MI_Burnhope_Rock_Photo.MI_Burnhope_Rock_Photo") or BLOCK_MAT
    build_collision()
    build_floors()
    build_bases()
    build_pit()
    build_ridges_and_lanes()
    build_jungle()
    build_edges()
    build_skirts()
    build_skyline()
    build_horizon(22000.0, 18000.0, 34, 40.0, 60.0)
    build_lighting()
    build_gameplay_actors(rock_mat)
    flush_scatter()
    if not levels.save_current_level():
        print("ARENA_MAP FAIL save")
        return
    print(f"ARENA_MAP solid_footprints={len(SOLID)}")
    print(f"ARENA_MAP OK path={MAP} actors={count['n']}")


import builtins  # noqa: E402
if not getattr(builtins, "ARENA_NO_MAIN", False):   # build_conquest.py imports the helpers without building this map
    main()
