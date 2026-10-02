"""build_training.py — builds /Game/Maps/Training: the training centre, a plain grey room made of the engine's basic
shapes (cubes and cylinders) with the project's grid material M_ArenaTraining. Cheap on purpose: no library art.

  room 60 x 40 m, walls 5 m      the player starts at the west end, facing the dummies
  dummies (TargetPoint "Dummy")  three in a row 18 m ahead, one on the podium
  walker (TargetPoint "Mover")   walks side to side: aiming at a moving target
  podium + ramp                  a 3 m step with a 12 m ramp: shots and dashes on a slope
  pillars, a low wall            cover, line of sight, knockbacks into a wall
The game mode recognises the map by name (AArenaGameMode::IsTrainingMap) and runs the training rules.
Run headless (idempotent, the map is cleared and rebuilt):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" -unattended -nosplash -nullrhi
Prints TRAINING_MAP OK path=... actors=N or TRAINING_MAP FAIL <why>.
"""
import math

import unreal

MAP = "/Game/Maps/Training"
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
CYL = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
GRID = unreal.load_asset("/Game/Arena/Materials/M_ArenaTraining.M_ArenaTraining")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
count = {"n": 0}
_mats = {}


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
    """the grid material at a grey tone (an instance per tone, saved next to the map)"""
    if tone in _mats:
        return _mats[tone]
    name = f"MI_Training_{int(tone * 100)}"
    path = f"/Game/Arena/Materials/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, "/Game/Arena/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, GRID)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "Tone", tone)
    unreal.MaterialEditingLibrary.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    _mats[tone] = mi
    return mi


def box(label, x0, x1, y0, y1, z0, z1, tone=0.2):
    a = spawn(unreal.StaticMeshActor, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), label=label)
    c = a.static_mesh_component
    c.set_static_mesh(CUBE)
    c.set_world_scale3d(V(abs(x1 - x0) / 100.0, abs(y1 - y0) / 100.0, abs(z1 - z0) / 100.0))
    c.set_material(0, tone_mat(tone))
    c.set_collision_profile_name("BlockAll")
    return a


def pillar(label, x, y, radius, height, tone=0.26):
    a = spawn(unreal.StaticMeshActor, (x, y, height / 2), label=label)
    c = a.static_mesh_component
    c.set_static_mesh(CYL)
    c.set_world_scale3d(V(radius / 50.0, radius / 50.0, height / 100.0))
    c.set_material(0, tone_mat(tone))
    c.set_collision_profile_name("BlockAll")
    return a


def ramp(label, p0, p1, width, thick=300.0, tone=0.24):
    """a walkable slope from p0 (top) to p1 (foot): a rotated cube whose top face runs through both points"""
    dx, dy, dz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
    horiz = math.hypot(dx, dy)
    yaw, pitch, length = math.degrees(math.atan2(dy, dx)), math.degrees(math.atan2(dz, horiz)), math.hypot(horiz, dz)
    p, yw = math.radians(pitch), math.radians(yaw)
    n = (-math.sin(p) * math.cos(yw), -math.sin(p) * math.sin(yw), math.cos(p))
    mid = [(p0[i] + p1[i]) / 2 for i in range(3)]
    a = spawn(unreal.StaticMeshActor, tuple(mid[i] - n[i] * thick / 2 for i in range(3)), R(yaw=yaw, pitch=pitch), label=label)
    c = a.static_mesh_component
    c.set_static_mesh(CUBE)
    c.set_world_scale3d(V((length + 40.0) / 100.0, width / 100.0, thick / 100.0))
    c.set_material(0, tone_mat(tone))
    c.set_collision_profile_name("BlockAll")
    return a


def sign(text, loc, yaw, size=90.0, rgb=(0.85, 0.9, 1.0)):
    a = spawn(unreal.TextRenderActor, loc, R(yaw=yaw), label=f"Sign_{text[:12]}")
    t = a.text_render
    t.set_editor_property("text", text)
    t.set_editor_property("world_size", size)
    t.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    t.set_editor_property("text_render_color", unreal.Color(int(rgb[0] * 255), int(rgb[1] * 255), int(rgb[2] * 255), 255))
    return a


def point(tag, loc, yaw=180.0):
    a = spawn(unreal.TargetPoint, loc, R(yaw=yaw), label=f"{tag}_{int(loc[0])}_{int(loc[1])}")
    a.tags = [tag]
    return a


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        if not levels.load_level(MAP):
            print("TRAINING_MAP FAIL load_level")
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
            print("TRAINING_MAP FAIL new_level")
            return
    if not GRID:
        print("TRAINING_MAP FAIL no M_ArenaTraining (run Tools/make_materials.py)")
        return
    # the room
    box("Floor", -3000, 3000, -2000, 2000, -40, 0, 0.2)
    box("Wall_N", -3040, 3040, 2000, 2040, -40, 500, 0.28)
    box("Wall_S", -3040, 3040, -2040, -2000, -40, 500, 0.28)
    box("Wall_E", 3000, 3040, -2000, 2000, -40, 500, 0.28)
    box("Wall_W", -3040, -3000, -2000, 2000, -40, 500, 0.28)
    # the podium (3 m) with its ramp, a dummy on top
    box("Podium", 1600, 2960, -1960, -700, -40, 300, 0.24)
    ramp("Ramp", (1600, -1330, 300), (400, -1330, 0), 700)
    # cover and a wall to knock into
    pillar("Pillar_A", -1100, 900, 70, 320)
    pillar("Pillar_B", -1100, -900, 70, 320)
    box("LowWall", 900, 1000, 500, 1700, 0, 180, 0.3)
    # the player's start and the dummies
    ps = spawn(unreal.PlayerStart, (-2300, 0, 110), R(yaw=0.0), label="PlayerStart")
    ps.set_editor_property("player_start_tag", "Training")
    for y in (-600, 0, 600):
        point("Dummy", (-400, y, 110))
    point("Dummy", (2300, -1300, 410))
    point("Dummy", (1200, 1100, 110))          # in front of the low wall: knock it into the wall
    point("Mover", (600, 0, 110))
    # signs
    sign("TRAINING CENTER", (2990, 0, 380), 180.0, 110.0, (1.0, 0.82, 0.4))
    sign("DUMMIES", (-400, -1990, 240), 90.0, 60.0)
    sign("MOVING TARGET", (600, 1990, 240), -90.0, 60.0)
    sign("RAMP AND PLATFORM", (1000, -1990, 360), 90.0, 60.0)
    sign("F1 no cooldowns   F2 level 20   F3 dummies   Esc menu", (-2990, 0, 330), 0.0, 45.0)
    # light: sun, sky light and a sky, a little fog (the defaults: a movable, real-time sky light over the sky
    # atmosphere burned this grey room to white; unbuilt stationary light only prints a note in the editor)
    sun = spawn(unreal.DirectionalLight, (0, 0, 1000), R(yaw=35.0, pitch=-52.0), label="Sun")
    sun.light_component.set_editor_property("intensity", 6.0)
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), label="Sky")
    sky = spawn(unreal.SkyLight, (0, 0, 800), label="SkyLight")
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_editor_property("intensity", 1.2)
    spawn(unreal.ExponentialHeightFog, (0, 0, -200), label="Fog")
    # navigation for dashes and knockbacks (they follow the navmesh surface)
    nav = spawn(unreal.NavMeshBoundsVolume, (0, 0, 200), label="NavBounds")
    nav.set_actor_scale3d(V(64.0, 44.0, 8.0))
    if not levels.save_current_level():
        print("TRAINING_MAP FAIL save")
        return
    print(f"TRAINING_MAP OK path={MAP} actors={count['n']}")


main()
