"""make_hitreacts.py — the Paragon hit reactions ship as additive animations (a delta on top of another pose), which
a full-pose montage slot cannot play. This tool duplicates every clip listed under "hitReacts" in
Content/Data/heroes.json from its pack into /Game/Arena/Generated/Anims and turns the copy into a regular
(non-additive) animation: the raw keys are the complete flinch from the idle stance.
Derived from Epic's packs, so the output is not committed (.gitignore) — run it once per checkout:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" -unattended -nosplash -nullrhi
Prints HITREACT OK made=N skipped=M or HITREACT FAIL <why>.
"""
import json
import os
import unreal

DATA = os.path.join(unreal.Paths.project_content_dir(), "Data", "heroes.json")
OUT = "/Game/Arena/Generated/Anims"
PACK = {
    "Greystone": "/Game/ParagonGreystone/Characters/Heroes/Greystone/Animations/",
    "Countess": "/Game/ParagonCountess/Characters/Heroes/Countess/Animations/",
    "Gideon": "/Game/ParagonGideon/Characters/Heroes/Gideon/Animations/",
    "Sparrow": "/Game/ParagonSparrow/Characters/Heroes/Sparrow/Animations/",
    "Kwang": "/Game/ParagonKwang/Characters/Heroes/Kwang/Animations/",
    "Crunch": "/Game/ParagonCrunch/Characters/Heroes/Crunch/Animations/",
    "IggyScorch": "/Game/ParagonIggyScorch/Characters/Heroes/IggyScorch/Animations/",
    "Khaimera": "/Game/ParagonKhaimera/Characters/Heroes/Khaimera/Animations/",
    "Morigesh": "/Game/ParagonMorigesh/Characters/Heroes/Morigesh/Animations/",
    "Revenant": "/Game/ParagonRevenant/Characters/Heroes/Revenant/Animations/",
    "Sevarog": "/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/",
}


def main():
    with open(DATA, encoding="utf-8") as f:
        db = json.load(f)
    made = skipped = 0
    for h in db["heroes"]:
        for target in h.get("hitReacts", []):
            pkg, name = target.split(".")[0], target.split(".")[-1]
            src_name = name[len(h["id"]) + 1:]
            src = PACK[h["id"]] + src_name + "." + src_name
            if unreal.EditorAssetLibrary.does_asset_exist(pkg):
                skipped += 1
                continue
            if not unreal.EditorAssetLibrary.does_asset_exist(src.split(".")[0]):
                print(f"HITREACT FAIL missing source {src}")
                return
            copy = unreal.EditorAssetLibrary.duplicate_asset(src.split(".")[0], pkg)
            if not copy:
                print(f"HITREACT FAIL duplicate {src} -> {pkg}")
                return
            copy.set_editor_property("additive_anim_type", unreal.AdditiveAnimationType.AAT_NONE)
            unreal.EditorAssetLibrary.save_loaded_asset(copy)
            print(f"HITREACT made {pkg} from {src_name} len={copy.get_play_length():.2f}")
            made += 1
    print(f"HITREACT OK made={made} skipped={skipped}")


main()
