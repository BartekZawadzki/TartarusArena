"""make_materials.py — the project's own materials, built from engine functions and pack textures:
  M_ArenaCliff      world-aligned (triplanar) rock for the terrain blocks, so a 70 m slab never stretches a texture
  M_ArenaIndicator  deferred decal for ability targeting: disc, ring, lane and cone drawn by one custom node
  M_ArenaHitFlash   overlay drawn over a character for a moment when it is hit (white rim glow, fades by Flash)
  M_ArenaBackdrop   unlit portrait backdrop: a soft glow of the hero's colour behind the head, dark at the edges
  M_ArenaTraining   the training centre's grey: world-space grid lines every metre, stronger every 5 m (no textures)
Idempotent (rebuilds the graphs). Run headless:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path>" -unattended -nosplash -nullrhi
Prints MATERIALS OK or MATERIALS FAIL <why>.
"""
import traceback

import unreal

DIR = "/Game/Arena/Materials"
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def material(name):
    path = f"{DIR}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
        return m
    return tools.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())


def expr(m, cls, x, y):
    return mel.create_material_expression(m, cls, x, y)


def cliff():
    m = material("M_ArenaCliff")
    m.set_editor_property("used_with_nanite", True)   # the Blender rock skirts are Nanite meshes: without it SM6 drew the default material
    tex = unreal.load_asset("/Game/ParagonProps/Agora/Rocks/Textures/T_Agelsjon02_Cliff_D.T_Agelsjon02_Cliff_D")
    if not tex:
        raise RuntimeError("cliff texture missing")
    obj = expr(m, unreal.MaterialExpressionTextureObject, -900, 0)
    obj.set_editor_property("texture", tex)
    size = expr(m, unreal.MaterialExpressionConstant3Vector, -900, 200)
    size.set_editor_property("constant", unreal.LinearColor(420.0, 420.0, 420.0, 0.0))
    fn = expr(m, unreal.MaterialExpressionMaterialFunctionCall, -550, 0)
    fn.set_material_function(unreal.load_asset("/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture.WorldAlignedTexture"))
    mel.connect_material_expressions(obj, "", fn, "TextureObject")
    mel.connect_material_expressions(size, "", fn, "TextureSize")
    tint = expr(m, unreal.MaterialExpressionConstant3Vector, -550, 250)
    tint.set_editor_property("constant", unreal.LinearColor(0.82, 0.78, 0.72, 1.0))
    mul = expr(m, unreal.MaterialExpressionMultiply, -250, 0)
    mel.connect_material_expressions(fn, "XYZ Texture", mul, "A")
    mel.connect_material_expressions(tint, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(m, unreal.MaterialExpressionConstant, -250, 200)
    rough.set_editor_property("r", 0.92)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    print("MATERIALS cliff ok")


INDICATOR_HLSL = r"""
// decal UVs: U runs along the aim direction (the decal's Z), V sideways (its Y); p in [-1, 1].
// Returns three masks: x = the area fill, y = the bright rim, z = a dark outline just outside the rim. A MOBA
// indicator has to read on the bright stone of the bases and on the dark jungle alike: the fill darkens and tints
// the ground, the rim glows in the ability's colour, the outline separates it from any floor.
float2 p = UV * 2.0 - 1.0;
float r = length(p);
float e = Edge;
float fill = 0.0, rim = 0.0, dark = 0.0;
if (Shape < 0.5)
{
    // area disc; a telegraph (Fill < 1) fills from the centre and reaches the rim the moment it lands
    if (r > 1.0) return float3(0, 0, 0);
    dark = smoothstep(1.0 - e * 0.36, 1.0 - e * 0.3, r);
    rim = smoothstep(1.0 - e * 1.35, 1.0 - e * 1.0, r) * (1.0 - dark);
    if (Fill >= 0.999) { fill = 1.0; }
    else
    {
        fill = r < Fill ? 1.0 : 0.3;
        float front = smoothstep(Fill - e * 1.3, Fill - e * 0.3, r) * (1.0 - smoothstep(Fill, Fill + e * 0.25, r));
        rim = max(rim, front);
    }
}
else if (Shape < 1.5)
{
    // range ring: a bright band between two dark lines
    if (r > 1.0) return float3(0, 0, 0);
    float band = smoothstep(1.0 - e * 0.9, 1.0 - e * 0.65, r) * (1.0 - smoothstep(1.0 - e * 0.35, 1.0 - e * 0.2, r));
    float outer = smoothstep(1.0 - e * 0.2, 1.0 - e * 0.1, r);
    float inner = smoothstep(1.0 - e * 1.15, 1.0 - e * 1.0, r) * (1.0 - smoothstep(1.0 - e * 0.9, 1.0 - e * 0.75, r));
    rim = band;
    dark = max(outer, inner);
}
else if (Shape < 2.5)
{
    // lane (skillshot / dash): bright long sides and far end, dark outline around them
    float2 a = abs(p);
    if (a.x > 1.0 || a.y > 1.0) return float3(0, 0, 0);
    float tipU = p.x * Flip;
    float et = e * Aspect;   // the same width in cm at the far end as on the sides
    dark = max(smoothstep(1.0 - e * 0.36, 1.0 - e * 0.3, a.y), smoothstep(1.0 - et * 0.36, 1.0 - et * 0.3, tipU));
    rim = max(smoothstep(1.0 - e * 1.35, 1.0 - e * 1.0, a.y), smoothstep(1.0 - et * 1.35, 1.0 - et * 1.0, tipU)) * (1.0 - dark);
    fill = 1.0;
}
else
{
    // cone: apex at the centre, opening along +U * Flip, half angle Angle (radians)
    float ang = abs(atan2(p.y, p.x * Flip));
    if (ang > Angle || r > 1.0) return float3(0, 0, 0);
    dark = max(smoothstep(1.0 - e * 0.36, 1.0 - e * 0.3, r), smoothstep(Angle - 0.03, Angle - 0.018, ang));
    rim = max(smoothstep(1.0 - e * 1.35, 1.0 - e * 1.0, r), smoothstep(Angle - 0.1, Angle - 0.055, ang)) * (1.0 - dark);
    fill = 1.0;
}
fill = saturate(fill - rim - dark);
return float3(fill, saturate(rim), saturate(dark));
"""


def indicator():
    m = material("M_ArenaIndicator")
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)   # UE 5.8: a translucent decal writes colour, emissive and opacity
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1300, -200)
    params = {}
    for i, (name, default) in enumerate((("Shape", 0.0), ("Angle", 0.8), ("Edge", 0.08), ("Flip", 1.0), ("Opacity", 0.75), ("Glow", 3.0), ("Fill", 1.0), ("Aspect", 1.0))):
        p = expr(m, unreal.MaterialExpressionScalarParameter, -1300, 0 + i * 110)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", default)
        params[name] = p
    color = expr(m, unreal.MaterialExpressionVectorParameter, -1300, 800)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.3, 0.8, 1.0, 1.0))
    custom = expr(m, unreal.MaterialExpressionCustom, -950, 0)
    custom.set_editor_property("code", INDICATOR_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "ArenaIndicatorMasks")
    names = ["UV", "Shape", "Angle", "Edge", "Flip", "Fill", "Aspect"]
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    custom.set_editor_property("inputs", ins)
    mel.connect_material_expressions(uv, "", custom, "UV")
    for n in names[1:]:
        mel.connect_material_expressions(params[n], "", custom, n)

    def mask(ch, y):
        k = expr(m, unreal.MaterialExpressionComponentMask, -750, y)
        for c in "rgba":
            k.set_editor_property(c, c == ch)
        mel.connect_material_expressions(custom, "", k, "")
        return k

    def const(v, x, y):
        c = expr(m, unreal.MaterialExpressionConstant, x, y)
        c.set_editor_property("r", v)
        return c

    def mul(a, b, x, y):
        n = expr(m, unreal.MaterialExpressionMultiply, x, y)
        mel.connect_material_expressions(a, "", n, "A")
        mel.connect_material_expressions(b, "", n, "B")
        return n

    def add(a, b, x, y):
        n = expr(m, unreal.MaterialExpressionAdd, x, y)
        mel.connect_material_expressions(a, "", n, "A")
        mel.connect_material_expressions(b, "", n, "B")
        return n

    fill, rim, dark = mask("r", -100), mask("g", 100), mask("b", 300)
    # opacity: a light tint of the area, a solid rim, a strong dark outline; times the per-draw opacity x 1.5
    op = add(add(mul(fill, const(0.3, -750, 450), -600, -100), rim, -450, 0), mul(dark, const(0.8, -750, 550), -600, 300), -300, 0)
    sat = expr(m, unreal.MaterialExpressionSaturate, -150, 0)
    mel.connect_material_expressions(op, "", sat, "")
    per = mul(params["Opacity"], const(1.5, -750, 650), -450, 500)
    persat = expr(m, unreal.MaterialExpressionSaturate, -300, 500)
    mel.connect_material_expressions(per, "", persat, "")
    mel.connect_material_property(mul(sat, persat, 0, 100), "", unreal.MaterialProperty.MP_OPACITY)
    # base colour: dark tint in the fill, full colour on the rim, black on the outline
    inv_dark = expr(m, unreal.MaterialExpressionOneMinus, -450, 300)
    mel.connect_material_expressions(dark, "", inv_dark, "")
    shade = add(const(0.2, -600, 700), mul(rim, const(0.8, -750, 750), -600, 150), -450, 150)
    base = mul(mul(color, shade, -300, 700), inv_dark, -150, 700)
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # emissive: the rim glows (bloom), the fill barely
    glow_rim = mul(mul(color, params["Glow"], -450, 850), rim, -300, 850)
    glow_fill = mul(mul(color, const(0.25, -600, 950), -450, 950), fill, -300, 950)
    mel.connect_material_property(add(glow_rim, glow_fill, -150, 900), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    print("MATERIALS indicator ok")


def hit_flash():
    m = material("M_ArenaHitFlash")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("used_with_skeletal_mesh", True)   # an overlay on the heroes: without it the cooked game draws the default material
    flash = expr(m, unreal.MaterialExpressionScalarParameter, -900, 0)
    flash.set_editor_property("parameter_name", "Flash")
    flash.set_editor_property("default_value", 1.0)
    color = expr(m, unreal.MaterialExpressionVectorParameter, -900, 200)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.95, 0.85, 1.0))
    fres = expr(m, unreal.MaterialExpressionFresnel, -900, 400)
    fres.set_editor_property("exponent", 2.0)
    base = expr(m, unreal.MaterialExpressionConstant, -900, 550)
    base.set_editor_property("r", 0.45)
    rim = expr(m, unreal.MaterialExpressionAdd, -650, 400)
    mel.connect_material_expressions(fres, "", rim, "A")
    mel.connect_material_expressions(base, "", rim, "B")
    glow = expr(m, unreal.MaterialExpressionConstant, -650, 550)
    glow.set_editor_property("r", 4.0)
    c1 = expr(m, unreal.MaterialExpressionMultiply, -450, 200)
    mel.connect_material_expressions(color, "", c1, "A")
    mel.connect_material_expressions(rim, "", c1, "B")
    c2 = expr(m, unreal.MaterialExpressionMultiply, -300, 200)
    mel.connect_material_expressions(c1, "", c2, "A")
    mel.connect_material_expressions(glow, "", c2, "B")
    c3 = expr(m, unreal.MaterialExpressionMultiply, -150, 200)
    mel.connect_material_expressions(c2, "", c3, "A")
    mel.connect_material_expressions(flash, "", c3, "B")
    mel.connect_material_property(c3, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    op = expr(m, unreal.MaterialExpressionMultiply, -300, 0)
    mel.connect_material_expressions(flash, "", op, "A")
    mel.connect_material_expressions(rim, "", op, "B")
    mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    print("MATERIALS hit flash ok")


def backdrop():
    m = material("M_ArenaBackdrop")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    tint = expr(m, unreal.MaterialExpressionVectorParameter, -900, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.5, 0.55, 0.7, 1.0))
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -900, 250)
    ctr = expr(m, unreal.MaterialExpressionConstant2Vector, -900, 400)
    ctr.set_editor_property("r", 0.5)
    ctr.set_editor_property("g", 0.42)
    dist = expr(m, unreal.MaterialExpressionDistance, -700, 300)
    mel.connect_material_expressions(uv, "", dist, "A")
    mel.connect_material_expressions(ctr, "", dist, "B")
    k = expr(m, unreal.MaterialExpressionConstant, -700, 450)
    k.set_editor_property("r", 1.7)
    d2 = expr(m, unreal.MaterialExpressionMultiply, -550, 300)
    mel.connect_material_expressions(dist, "", d2, "A")
    mel.connect_material_expressions(k, "", d2, "B")
    inv = expr(m, unreal.MaterialExpressionOneMinus, -420, 300)
    mel.connect_material_expressions(d2, "", inv, "")
    sat = expr(m, unreal.MaterialExpressionSaturate, -300, 300)
    mel.connect_material_expressions(inv, "", sat, "")
    glow = expr(m, unreal.MaterialExpressionMultiply, -180, 300)
    mel.connect_material_expressions(sat, "", glow, "A")
    g = expr(m, unreal.MaterialExpressionConstant, -300, 450)
    g.set_editor_property("r", 0.16)
    mel.connect_material_expressions(g, "", glow, "B")
    floor = expr(m, unreal.MaterialExpressionConstant, -180, 450)
    floor.set_editor_property("r", 0.012)
    lvl = expr(m, unreal.MaterialExpressionAdd, -60, 300)
    mel.connect_material_expressions(glow, "", lvl, "A")
    mel.connect_material_expressions(floor, "", lvl, "B")
    out = expr(m, unreal.MaterialExpressionMultiply, 60, 100)
    mel.connect_material_expressions(tint, "", out, "A")
    mel.connect_material_expressions(lvl, "", out, "B")
    mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    print("MATERIALS backdrop ok")


TRAINING_HLSL = r"""
// grey with a world-space grid: thin lines every metre, stronger every 5 m, on floors and walls alike
float3 w = WP;
float3 g1 = abs(frac(w / 100.0 + 0.5) - 0.5) * 100.0;
float3 g5 = abs(frac(w / 500.0 + 0.5) - 0.5) * 500.0;
float l1 = 1.0 - smoothstep(0.7, 1.6, min(min(g1.x, g1.y), g1.z));
float l5 = 1.0 - smoothstep(1.4, 3.2, min(min(g5.x, g5.y), g5.z));
return Tone * (1.0 + 0.35 * l1 + 0.9 * l5);
"""


def training():
    m = material("M_ArenaTraining")
    wp = expr(m, unreal.MaterialExpressionWorldPosition, -800, 0)
    tone = expr(m, unreal.MaterialExpressionScalarParameter, -800, 200)
    tone.set_editor_property("parameter_name", "Tone")
    tone.set_editor_property("default_value", 0.2)
    cus = expr(m, unreal.MaterialExpressionCustom, -500, 0)
    cus.set_editor_property("code", TRAINING_HLSL)
    cus.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    ins = []
    for n in ("WP", "Tone"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    cus.set_editor_property("inputs", ins)
    mel.connect_material_expressions(wp, "", cus, "WP")
    mel.connect_material_expressions(tone, "", cus, "Tone")
    mel.connect_material_property(cus, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expr(m, unreal.MaterialExpressionConstant, -500, 250)
    rough.set_editor_property("r", 0.85)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    print("MATERIALS training ok")


try:
    cliff()
    indicator()
    hit_flash()
    backdrop()
    training()
    print("MATERIALS OK")
except Exception as ex:  # noqa: BLE001
    print(f"MATERIALS FAIL {ex} {traceback.format_exc()}")
