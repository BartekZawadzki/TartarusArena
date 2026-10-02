"""build_manifest.py — fills /Game/Arena/DA_ArenaManifest with hard references to every asset path in
Content/Data/heroes.json, so the cooker packages exactly what the game data uses. Run headless:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" -unattended -nosplash
Prints MANIFEST OK assets=N missing=M and one MANIFEST MISSING line per path that did not load.
"""
import json
import os
import unreal

DATA = os.path.join(unreal.Paths.project_content_dir(), "Data", "heroes.json")
ASSET = "/Game/Arena/DA_ArenaManifest"


def paths(node):
    if isinstance(node, dict):
        for v in node.values():
            yield from paths(v)
    elif isinstance(node, list):
        for v in node:
            yield from paths(v)
    elif isinstance(node, str) and node.startswith(("/Game/", "/Engine/")):
        yield node


def main():
    with open(DATA, encoding="utf-8") as f:
        db = json.load(f)
    loaded, missing = [], []
    wanted = set(paths(db))
    # the voice lines of every hero (v13): <dir>/<Hero>_<Event> next to its <Hero>_Effort_Pain cue
    events = db.get("rules", {}).get("voiceEvents", [])
    for h in db.get("heroes", []):
        pain = h.get("painSound", "")
        if "/" not in pain or "_Effort_Pain" not in pain:
            continue
        folder, name = pain.rsplit("/", 1)
        prefix = name.split("_Effort_Pain")[0]
        for ev in events:
            cue = f"{folder}/{prefix}_{ev}"
            if unreal.EditorAssetLibrary.does_asset_exist(cue):
                wanted.add(f"{cue}.{prefix}_{ev}")
    for p in sorted(wanted):
        # an object first: an asset may itself be named ..._C (Attack_C, Death_C); a blueprint class path loads as a class
        obj = unreal.load_object(None, p) or (unreal.load_class(None, p) if p.endswith("_C") else None)
        (loaded if obj else missing).append(obj if obj else p)
    for m in missing:
        print(f"MANIFEST MISSING {m}")
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET):
        manifest = unreal.load_asset(ASSET)
    else:
        manifest = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DA_ArenaManifest", "/Game/Arena", unreal.ArenaAssetManifest, unreal.DataAssetFactory())
    manifest.set_editor_property("assets", loaded)
    unreal.EditorAssetLibrary.save_loaded_asset(manifest)
    print(f"MANIFEST OK assets={len(loaded)} missing={len(missing)}")


main()
