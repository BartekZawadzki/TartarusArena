"""inspect_map.py — survey a level (default /Game/SampleMap/ParagonSample): actor classes, bounds, landscapes,
most used meshes, lights; with rendering (-AllowCommandletRendering) it also writes top/perspective captures to
Saved/MapCaptures/<name>_*.png. Prints INSPECT lines.
Run: UnrealEditor-Cmd <uproject> -run=pythonscript -script="<abs>/inspect_map.py" -AllowCommandletRendering
Map override: set env ARENA_INSPECT_MAP=/Game/Path/Map
"""
import collections
import os
import unreal

MAP = os.environ.get("ARENA_INSPECT_MAP", "/Game/SampleMap/ParagonSample")
OUT = os.path.join(unreal.Paths.project_saved_dir(), "MapCaptures")
os.makedirs(OUT, exist_ok=True)

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.load_level(MAP):
    print(f"INSPECT FAIL load {MAP}")
    raise SystemExit
all_actors = actors.get_all_level_actors()
classes = collections.Counter(a.get_class().get_name() for a in all_actors)
print(f"INSPECT map={MAP} actors={len(all_actors)}")
for cls, n in classes.most_common(25):
    print(f"INSPECT class {cls} x{n}")

meshes = collections.Counter()
lo = unreal.Vector(1e9, 1e9, 1e9)
hi = unreal.Vector(-1e9, -1e9, -1e9)
for a in all_actors:
    if isinstance(a, unreal.StaticMeshActor):
        m = a.static_mesh_component.static_mesh
        if m:
            meshes[m.get_path_name()] += 1
        o, e = a.get_actor_bounds(False)
        if e.x < 20000 and e.y < 20000:
            lo = unreal.Vector(min(lo.x, o.x - e.x), min(lo.y, o.y - e.y), min(lo.z, o.z - e.z))
            hi = unreal.Vector(max(hi.x, o.x + e.x), max(hi.y, o.y + e.y), max(hi.z, o.z + e.z))
    for comp in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
        if comp.static_mesh:
            meshes[comp.static_mesh.get_path_name() + " (instanced)"] += comp.get_instance_count()
print(f"INSPECT bounds min={lo} max={hi}")
for m, n in meshes.most_common(40):
    print(f"INSPECT mesh x{n} {m}")
for a in all_actors:
    if isinstance(a, (unreal.Landscape, unreal.LandscapeProxy)):
        o, e = a.get_actor_bounds(False)
        print(f"INSPECT landscape {a.get_name()} center={o} extent={e}")
    if isinstance(a, (unreal.PlayerStart, unreal.DirectionalLight, unreal.SkyLight, unreal.ExponentialHeightFog, unreal.SkyAtmosphere, unreal.PostProcessVolume)):
        print(f"INSPECT key {a.get_class().get_name()} {a.get_name()} at {a.get_actor_location()}")

# captures (need -AllowCommandletRendering)
try:
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    center = unreal.Vector((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2)
    size = max(hi.x - lo.x, hi.y - lo.y)
    rt = unreal.RenderingLibrary.create_render_target2d(world, 1600, 1000, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    shots = {
        "top": (unreal.Vector(center.x, center.y, hi.z + size * 0.9), unreal.Rotator(0.0, -90.0, 0.0)),
        "persp_a": (unreal.Vector(center.x - size * 0.55, center.y - size * 0.35, hi.z + size * 0.25), unreal.Rotator(0.0, -28.0, 32.0)),
        "persp_b": (unreal.Vector(center.x + size * 0.55, center.y + size * 0.35, hi.z + size * 0.25), unreal.Rotator(0.0, -28.0, 212.0)),
    }
    for name, (loc, rot) in shots.items():
        cap = actors.spawn_actor_from_class(unreal.SceneCapture2D, loc, rot)
        cc = cap.capture_component2d
        cc.set_editor_property("texture_target", rt)
        cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        cc.set_editor_property("fov_angle", 70.0)
        cc.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, rt, OUT, f"{MAP.split('/')[-1]}_{name}.png")
        actors.destroy_actor(cap)
        print(f"INSPECT capture {name} -> {OUT}")
except Exception as ex:  # noqa: BLE001
    print(f"INSPECT capture skipped: {ex}")
