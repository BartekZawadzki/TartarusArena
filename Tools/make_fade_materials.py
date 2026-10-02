"""make_fade_materials.py — copies of every unit material that can fade out after a death.

A dead body lies on the ground, then fades away (LoL / Smite). The pack materials cannot: the Paragon hero materials
are opaque, and the mannequin material used by the minions has no fade. (The Paragon MF_CharacterEffects has a
"FadeOut" scalar of its own, but it darkens the body to a black silhouette: measured in -ArenaAnimLab, not used.)
This tool writes a masked copy of each material named by heroes.json into /Game/Arena/Generated/Fade (gitignored,
rebuilt per checkout like the other generated assets):
  * a base material: duplicated, blend mode Masked, its opacity mask multiplied by a screen-space dither of
    (1 - ArenaFade) (a fine "screen door" that reads as transparency). A material that was opaque takes the dither
    alone (its opacity-mask output was never used and may hold anything) with a clip value of 0.5; a masked one
    (hair, feathers) keeps its own mask and clip value. Material-attribute graphs (Paragon) get it through a
    Break / Make Material Attributes pair in front of the output;
  * a material instance: duplicated and reparented onto the copy of its parent, so every override stays;
  * a translucent material (eyes, tear lines): no copy; the game hides that slot when the fade starts.
The game (AArenaCharacter::ApplyFadeMaterials) swaps each slot to F_<pack>_<name> when it exists and drives
"ArenaFade" 0 -> 1. With ArenaFade 0 a copy draws exactly like the pack material.
Idempotent. Run headless:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" -unattended -nosplash -nullrhi -culture=en
(-culture=en: expression pin names are localized in a non-English editor.)
Prints FADE OK n=<assets> skipped=<translucent> or FADE FAIL <why>.
"""
import json
import os
import traceback

import unreal

DIR = "/Game/Arena/Generated/Fade"
PARAM = "ArenaFade"
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary

DITHER_HLSL = r"""
// interleaved gradient noise per pixel, shifted every frame: the temporal anti-aliasing averages the screen door
// into a smooth transparency (a fixed pattern read as dots); a share Alpha of the pixels stays drawn
float2 p = Parameters.SvPosition.xy + float2(47.0, 17.0) * float(View.StateFrameIndexMod8);
float n = frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
return (n < Alpha) ? 1.0 : 0.0;
"""

def fade_name(path):
    """/Game/ParagonSparrow/.../M_Legs_Hands.M_Legs_Hands -> F_ParagonSparrow_M_Legs_Hands (the game builds the same)."""
    pkg = path.split(".")[0]
    parts = pkg.split("/")
    return f"F_{parts[2]}_{parts[-1]}"


def link(a, a_out, b, b_in):
    """connect or fail loudly (a silent miss left a body white or invisible)"""
    if not mel.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError(f"link failed {a.get_class().get_name()}.{a_out!r} -> {b.get_class().get_name()}.{b_in!r}")


def dither_node(m):
    """the pixel mask of (1 - ArenaFade): 1 = drawn, 0 = clipped"""
    fade = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -900, 1400)
    fade.set_editor_property("parameter_name", PARAM)
    fade.set_editor_property("default_value", 0.0)
    inv = mel.create_material_expression(m, unreal.MaterialExpressionOneMinus, -700, 1400)
    link(fade, "", inv, "")
    cus = mel.create_material_expression(m, unreal.MaterialExpressionCustom, -500, 1400)
    cus.set_editor_property("code", DITHER_HLSL)
    cus.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", "Alpha")
    cus.set_editor_property("inputs", [ci])
    link(inv, "", cus, "Alpha")
    return cus


def add_dither(m, keep_mask):
    """plain graph: opacity mask = (old mask if it was masked) * dither"""
    cus = dither_node(m)
    old = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_OPACITY_MASK)
    if keep_mask and old is not None:
        pin = mel.get_material_property_input_node_output_name(m, unreal.MaterialProperty.MP_OPACITY_MASK)
        mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 1400)
        link(old, pin, mul, "A")
        link(cus, "", mul, "B")
        mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    else:
        mel.connect_material_property(cus, "", unreal.MaterialProperty.MP_OPACITY_MASK)


def add_dither_attributes(m, keep_mask):
    """material-attribute graph: Break the old result, Make it again with OpacityMask = (old *) dither"""
    old = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    if old is None:
        raise RuntimeError("no material attributes output")
    # the output the material used (a function such as Paragon's MF_CharacterEffects has several; "Result" here)
    pin = mel.get_material_property_input_node_output_name(m, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    brk = mel.create_material_expression(m, unreal.MaterialExpressionBreakMaterialAttributes, 200, 1200)
    brk_in = str(mel.get_material_expression_input_names(brk)[0])   # pin names follow the editor language
    link(old, pin, brk, brk_in)
    mk = mel.create_material_expression(m, unreal.MaterialExpressionMakeMaterialAttributes, 600, 1200)
    outs = [str(x) for x in mel.get_material_expression_output_names(brk)]
    ins = set(str(x) for x in mel.get_material_expression_input_names(mk))
    pins = [p for p in outs if p in ins and p != "OpacityMask"]
    linked = sum(1 for p in pins if mel.connect_material_expressions(brk, p, mk, p))
    if linked != len(pins):
        raise RuntimeError(f"attribute pins linked {linked} of {len(pins)}")
    cus = dither_node(m)
    if keep_mask:
        mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, 400, 1500)
        link(brk, "OpacityMask", mul, "A")
        link(cus, "", mul, "B")
        link(mul, "", mk, "OpacityMask")
    else:
        link(cus, "", mk, "OpacityMask")
    mel.connect_material_property(mk, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    return linked


def fade_base(src):
    name = fade_name(src.get_path_name())
    blend = src.get_editor_property("blend_mode")
    if blend not in (unreal.BlendMode.BLEND_OPAQUE, unreal.BlendMode.BLEND_MASKED):
        # eyes, tear lines, eye occlusion: thin translucent layers; the game hides them when the fade starts
        print(f"FADE skip {name} blend={blend}")
        return None
    path = f"{DIR}/{name}"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)   # our own generated copy: rebuilt from the pack material every run
    m = eal.duplicate_asset(src.get_path_name().split(".")[0], path)
    if not m:
        raise RuntimeError(f"duplicate failed {src.get_path_name()}")
    keep = blend == unreal.BlendMode.BLEND_MASKED
    attrs = m.get_editor_property("use_material_attributes")
    linked = add_dither_attributes(m, keep) if attrs else add_dither(m, keep)
    if not keep:
        m.set_editor_property("opacity_mask_clip_value", 0.5)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("used_with_skeletal_mesh", True)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    print(f"FADE base {name} was={'masked' if keep else 'opaque'} attrs={attrs} linked={linked}")
    return m


def fade_of(mi, cache):
    key = mi.get_path_name()
    if key in cache:
        return cache[key]
    if isinstance(mi, unreal.Material):
        out = fade_base(mi)
    else:
        parent = fade_of(mi.get_editor_property("parent"), cache)
        name = fade_name(key)
        if parent is None:
            cache[key] = None
            return None
        path = f"{DIR}/{name}"
        if eal.does_asset_exist(path):
            eal.delete_asset(path)
        out = eal.duplicate_asset(key.split(".")[0], path)
        mel.set_material_instance_parent(out, parent)
        mel.update_material_instance(out)
        eal.save_loaded_asset(out)
        print(f"FADE inst {name} parent={parent.get_name()}")
    cache[key] = out
    return out


def main():
    root = unreal.Paths.project_content_dir()
    with open(os.path.join(root, "Data", "heroes.json"), encoding="utf-8") as f:
        db = json.load(f)
    meshes = [h["mesh"] for h in db["heroes"]]
    meshes += [db["rules"][k][f] for k in ("meleeMinion", "rangedMinion") for f in ("mesh", "meshAlt") if k in db.get("rules", {}) and db["rules"][k].get(f)]
    # our own generated folder only: start clean, so a copy the rules no longer make (a translucent layer made
    # masked by an older run) cannot linger and be picked up by name
    if eal.does_directory_exist(DIR):
        for p in eal.list_assets(DIR, recursive=True, include_folder=False):
            eal.delete_asset(p.split(".")[0])
    cache = {}
    for mp in meshes:
        sk = unreal.load_asset(mp)
        if not sk:
            raise RuntimeError(f"mesh missing {mp}")
        for sm in sk.get_editor_property("materials"):
            mi = sm.get_editor_property("material_interface")
            if mi:
                fade_of(mi, cache)
    made = [v for v in cache.values() if v is not None]
    print(f"FADE OK n={len(made)} skipped={len(cache) - len(made)}")


try:
    main()
except Exception as ex:  # noqa: BLE001
    traceback.print_exc()
    print(f"FADE FAIL {ex}")
