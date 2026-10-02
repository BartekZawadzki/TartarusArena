"""audit_root_motion.py — lists every animation heroes.json plays and whether it carries root motion, plus each hero
anim blueprint's root-motion mode. Root motion in an ability montage freezes path following while it plays.
Prints ROOTMOTION <path> enable=<bool> len=<s> and ABP <class> mode=<enum>.
"""
import json
import os
import unreal

DATA = os.path.join(unreal.Paths.project_content_dir(), "Data", "heroes.json")
with open(DATA, encoding="utf-8") as f:
    db = json.load(f)

anims = set()
for h in db["heroes"]:
    cls = unreal.load_class(None, h["animClass"])
    if cls:
        cdo = unreal.get_default_object(cls)
        print(f"ABP {h['id']} mode={cdo.get_editor_property('root_motion_mode')}")
    for a in h["abilities"]:
        for p in ([a.get("anim", "")] + a.get("animVariants", [])):
            if p:
                anims.add(p)
for p in sorted(anims):
    obj = unreal.load_object(None, p)
    if isinstance(obj, unreal.AnimMontage):
        seqs = []
        for track in obj.get_editor_property("slot_anim_tracks"):
            for seg in track.get_editor_property("anim_track").get_editor_property("anim_segments"):
                ref = seg.get_editor_property("anim_reference")
                if ref:
                    seqs.append(f"{ref.get_name()}:{ref.get_editor_property('enable_root_motion')}")
        print(f"ROOTMOTION {p.split('.')[-1]} montage len={obj.get_play_length():.2f} segments={seqs}")
    elif isinstance(obj, unreal.AnimSequenceBase):
        print(f"ROOTMOTION {p.split('.')[-1]} enable={obj.get_editor_property('enable_root_motion')} len={obj.get_play_length():.2f}")
    else:
        print(f"ROOTMOTION {p} MISSING")
