"""cap_textures.py — caps oversized library textures the game pulls in through FX, so building/cooking them fits in
memory on a normal workstation (an 8K normal map needs ~4.6 GB to encode). Visual effect: none at gameplay distance
(debris rocks inside Kwang's ultimate). Idempotent. Run headless:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" -unattended -nosplash -nullrhi
Prints TEXCAP OK capped=N skipped=M.
"""
import unreal

MAX_SIZE = 2048
TEXTURES = [
    "/Game/ParagonKwang/FX/Textures/Debris/T_LargePlainsBoulder002_N",
    "/Game/ParagonKwang/FX/Textures/Debris/T_TilingMoss_Clovers-n-Flowers_01_D",
    "/Game/ParagonKwang/FX/Textures/Debris/T_Agelsjon_Rock_1_Mask",
]

capped, skipped = 0, 0
for path in TEXTURES:
    tex = unreal.load_asset(path)
    if not tex:
        print(f"TEXCAP MISSING {path}")
        skipped += 1
        continue
    if tex.get_editor_property("max_texture_size") == MAX_SIZE:
        skipped += 1
        continue
    tex.set_editor_property("max_texture_size", MAX_SIZE)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    capped += 1
print(f"TEXCAP OK capped={capped} skipped={skipped}")
