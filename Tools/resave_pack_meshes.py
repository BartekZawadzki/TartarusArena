"""resave_pack_meshes.py — re-saves the library meshes that UE 5.8 reports as "recomputing physics on load. It must be
resaved before it will cook deterministically" (KiteDemo rocks and foliage the arena uses). Only re-saves: nothing in
the meshes changes. The packs are not in the repo, so run it once per checkout (after the packs are added). Headless:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" -unattended -nosplash -nullrhi
Prints RESAVE OK n=<count> or RESAVE FAIL <why>.
"""
import traceback

import unreal

MESHES = [
    "/Game/KiteDemo/Environments/Trees/Tree_Stump_01/Tree_Stump_01",
    "/Game/KiteDemo/Environments/Foliage/Flowers/Yarrow/SM_Yarrow3",
    "/Game/KiteDemo/Environments/Trees/Vegetation_Debris_002/SM_Vegetation_Debris_002",
    "/Game/KiteDemo/Environments/Rocks/River_Rock_01/SM_River_Rock_01",
    "/Game/KiteDemo/Environments/Rocks/Mountain_RockFace_002/SM_MountainRock_Closed",
    "/Game/KiteDemo/Environments/Foliage/Flowers/Heather/SM_Heather_Mesh_Clumps2",
    "/Game/KiteDemo/Environments/Foliage/Grass/FieldGrass/SM_FieldGrass_01",
    "/Game/KiteDemo/Environments/Rocks/Medium_Boulder_001/Medium_Boulder_001",
    "/Game/KiteDemo/Environments/Rocks/Medium_Boulder_002/Medium_Boulder_LowPoly",
]

try:
    n = 0
    for p in MESHES:
        a = unreal.load_asset(p)
        if not a:
            print(f"RESAVE WARN missing {p}")
            continue
        unreal.EditorAssetLibrary.save_loaded_asset(a, only_if_is_dirty=False)
        n += 1
    print(f"RESAVE OK n={n}")
except Exception as ex:  # noqa: BLE001
    print(f"RESAVE FAIL {ex} {traceback.format_exc()}")
