"""import_icons.py — imports the ability and item icons (game-icons.net, CC BY 3.0, see CREDITS.md) from Tools/Icons
into /Game/Arena/Icons as UI textures named T_Icon_<name> (dashes become underscores). White glyphs on a transparent
background, 256 px with mips (drawn at 40-120 px: a mip chain keeps the downscaled glyph smooth instead of
aliased); the HUD tints them. Idempotent (re-imports over the same assets). Run headless:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" -unattended -nosplash -nullrhi
Prints ICONS OK n=<count> or ICONS FAIL <why>.
"""
import os
import traceback

import unreal

DEST = "/Game/Arena/Icons"


def main():
    src = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Tools", "Icons")
    files = sorted(f for f in os.listdir(src) if f.lower().endswith(".png"))
    if not files:
        raise RuntimeError(f"no PNG in {src}")
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.filename = os.path.join(src, f)
        t.destination_path = DEST
        t.destination_name = "T_Icon_" + os.path.splitext(f)[0].replace("-", "_")
        t.automated = True
        t.replace_existing = True
        t.save = False
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    n = 0
    for t in tasks:
        path = f"{DEST}/{t.destination_name}"
        tex = unreal.load_asset(path)
        if not tex:
            raise RuntimeError(f"import failed: {t.filename}")
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
        tex.set_editor_property("max_texture_size", 256)
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("never_stream", True)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
        n += 1
    print(f"ICONS OK n={n}")


try:
    main()
except Exception as ex:  # noqa: BLE001
    print(f"ICONS FAIL {ex} {traceback.format_exc()}")
