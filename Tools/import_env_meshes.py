"""import_env_meshes.py — imports the project's own procedural environment meshes (Tools/Blender/out/*.fbx, built by
Tools/Blender/rock_skirts.py) into /Game/Arena/Generated/Env (gitignored, rebuilt per checkout).

Checks the orientation after the import (the UE import gate for a Blender FBX): a skirt's rock must stand on the
ground (sunk below z = 0) and reach further to +Y than into the wall at -Y. UE is left-handed: the import mirrors
Blender's Y, so the pile that protrudes towards Blender -Y protrudes towards UE +Y; build_arena.py places
the pieces with that convention (wall side -Y).
Prints ENV OK n=<meshes> or ENV FAIL <why>. Run headless:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" -unattended -nosplash -nullrhi
"""
import os
import traceback

import unreal

DIR = "/Game/Arena/Generated/Env"
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Blender", "out")


def main():
    names = sorted(f[:-4] for f in os.listdir(SRC) if f.startswith("SM_") and f.endswith(".fbx"))
    if not names:
        raise RuntimeError(f"no FBX in {SRC} (run Tools/Blender/rock_skirts.py)")
    tasks = []
    for n in names:
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", os.path.join(SRC, n + ".fbx"))
        t.set_editor_property("destination_path", DIR)
        t.set_editor_property("destination_name", n)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    bad = []
    for n in names:
        m = unreal.load_asset(f"{DIR}/{n}.{n}")
        if not isinstance(m, unreal.StaticMesh):
            bad.append(f"{n} not imported")
            continue
        b = m.get_bounds()
        lo = b.origin - b.box_extent
        hi = b.origin + b.box_extent
        print(f"ENV mesh {n} x=[{lo.x:.0f},{hi.x:.0f}] y=[{lo.y:.0f},{hi.y:.0f}] z=[{lo.z:.0f},{hi.z:.0f}] tris={m.get_num_triangles(0)}")
        # the wall side is -Y (the backs of the rocks in the wall), the pile towards +Y, sunk into the ground
        if not (hi.y > 40.0 and hi.y > -lo.y and lo.z < -5.0):
            bad.append(f"{n} orientation y=[{lo.y:.0f},{hi.y:.0f}] z=[{lo.z:.0f},{hi.z:.0f}]")
        unreal.EditorAssetLibrary.save_loaded_asset(m)
    if bad:
        raise RuntimeError("; ".join(bad))
    print(f"ENV OK n={len(names)}")


try:
    main()
except Exception as ex:  # noqa: BLE001
    traceback.print_exc()
    print(f"ENV FAIL {ex}")
