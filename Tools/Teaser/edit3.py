"""edit3.py — teaser v3's edit (operator 2026-10-02: "more gameplay, a bit less chaotic, the best-looking shots and the
prettiest scenes of the map and the fights"; game-style music under the gameplay, a different music on the
transitions). Calmer than edit.py: shots of 1-2 bars (2.4-4.8 s) cut on the bar lines, soft cross-dissolves inside a
section (no flashes, no shake, no montage), a slow push on every shot; the title cards (3 s) bring the transition
music, a soft flash and blurred gameplay. Grade, vignette and 2.39:1 bars as before.
Run: blender -b --factory-startup --python-exit-code 1 -P edit3.py -- <teaser_dir> [--stills 3.0,8.0] [--save x.blend]
Renders PNG frames to <teaser_dir>/edit_v3/ (stills to stills_v3/); prints EDIT OK / EDIT FAIL.
"""
import math
import os
import sys

import bpy
import numpy as np
from bpy_extras import anim_utils

ARGS = sys.argv[sys.argv.index("--") + 1:]
ROOT = ARGS[0]
STILLS = [float(x) for x in ARGS[ARGS.index("--stills") + 1].split(",")] if "--stills" in ARGS else None
SAVE = ARGS[ARGS.index("--save") + 1] if "--save" in ARGS else None
FPS = 30
END = 79.4
XF = 10                     # the cross-dissolve inside a section, frames
FONTS = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Content", "Data", "Fonts"))
MUSIC = "teaser_music_v3.wav"
TITLES = "titles_v3"
OUT_DIR, STILLS_DIR = "edit_v3", "stills_v3"


def F(t):
    return int(round(t * FPS)) + 1


# ------------------------------------------------------------------ the cut: sections of shots (start, end, take, in-point, speed, push [, raise px])
SECTIONS = [
    # the arena from above, in from black ("THE ARENA AWAITS")
    [(0.0, 7.2, "aerial", 0.0, 1.0, (1.0, 1.06))],
    # the heroes: slow motion and close-ups
    [(10.2, 15.0, "hero_c", 0.6, 1.0, (1.0, 1.05)),
     (15.0, 18.6, "hero_b", 1.5, 1.0, (1.0, 1.04)),
     (18.6, 21.0, "u2_beast", 0.8, 1.0, (1.02, 1.06)),
     (21.0, 24.6, "follow_b", 0.0, 1.0, (1.0, 1.04)),
     (24.6, 27.0, "hero_c", 5.3, 1.0, (1.0, 1.04))],
    # 5 vs 5: the team fights, low and close
    [(30.0, 34.8, "fight_e", 5.0, 1.0, (1.0, 1.04)),
     (34.8, 38.4, "fight_g", 0.2, 1.0, (1.0, 1.04)),
     (38.4, 40.8, "u1_purple", 0.2, 1.0, (1.1, 1.12, 45)),   # raised 45 px: the FIRST BLOOD banner under the bar
     (40.8, 44.4, "fight_h", 0.4, 1.0, (1.0, 1.04)),
     (44.4, 49.2, "follow_b", 3.8, 1.0, (1.0, 1.04))],
    # Conquest: the map, the towers, the jungle
    [(52.2, 57.0, "aerial_c", 0.0, 1.0, (1.0, 1.05)),
     (57.0, 59.4, "u2_beam", 0.1, 1.0, (1.02, 1.06)),
     (59.4, 63.0, "fight_d", 1.9, 1.0, (1.0, 1.04)),
     (63.0, 67.8, "fight_c", 6.0, 1.0, (1.0, 1.05)),
     (67.8, 71.4, "fight_f", 8.1, 1.0, (1.0, 1.04))],
]
CARDS = [
    # start, end, card, background take, in-point, speed, caption, caption y
    (7.2, 10.2, "heroes", "u1_roster", 0.0, 0.8, "GREYSTONE  ·  COUNTESS  ·  GIDEON  ·  SPARROW  ·  KWANG  ·  CRUNCH  ·  IGGY & SCORCH  ·  KHAIMERA  ·  MORIGESH  ·  REVENANT  ·  SEVAROG", 0.37),
    (27.0, 30.0, "5v5", "u1_lineup", 0.0, 1.0, "TEAM FIGHTS   ·   SMART BOTS   ·   LAN PLAY", 0.315),
    (49.2, 52.2, "conquest", "aerial_c", 6.0, 1.0, "PUSH THE LANES   ·   TAKE THE TOWERS   ·   DESTROY THE CORE", 0.36),
    (71.4, END, "logo", "hero_c", 5.0, 0.35, None, 0.0),
]
CAPTIONS = [
    (1.8, 6.6, "THE   ARENA   AWAITS", 58, 0.5),
    (73.8, 78.4, "11 HEROES   ·   ARENA   ·   CONQUEST", 34, 0.235),
    (74.6, 78.4, "A fan-made MOBA built with Paragon assets released by Epic Games", 20, 0.165),
]

# ------------------------------------------------------------------ scene
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.resolution_x, sc.render.resolution_y = 1920, 1080
sc.render.resolution_percentage = 100
sc.render.fps = FPS
sc.frame_start, sc.frame_end = 1, F(END) - 1
sc.view_settings.view_transform = "Standard"
sc.render.image_settings.file_format = "PNG"
sc.render.image_settings.color_mode = "RGB"
sc.render.image_settings.compression = 15
sed = sc.sequence_editor_create()
S = sed.strips
KEYS = {}


def key(owner, prop, frame, value, interp="BEZIER", easing="AUTO"):
    setattr(owner, prop, value)
    owner.keyframe_insert(prop, frame=frame)
    KEYS[(owner.path_from_id(prop), int(frame))] = (interp, easing)


def finish_keys():
    ad = sc.animation_data
    if not ad or not ad.action:
        return
    cb = anim_utils.action_get_channelbag_for_slot(ad.action, ad.action_slot)
    for fc in cb.fcurves:
        for kp in fc.keyframe_points:
            v = KEYS.get((fc.data_path, int(round(kp.co[0]))))
            if v:
                kp.interpolation, kp.easing = v


def footage(name, ch, f0, f1, take, src, speed=1.0):
    """Frames f0..f1-1 of the timeline from the take, starting at src seconds (held at the take's ends)."""
    d = os.path.join(ROOT, "frames", take)
    files = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    idx, pos = [], src * FPS
    for _ in range(f1 - f0):
        idx.append(max(0, min(len(files) - 1, int(round(pos)))))
        pos += speed
    st = S.new_image(name, os.path.join(d, files[idx[0]]), ch, f0, fit_method="FIT")
    for i in idx[1:]:
        st.elements.append(files[i])
    return st


def color(name, ch, t0, t1, rgb, blend="ALPHA_OVER"):
    st = S.new_effect(name, "COLOR", ch, F(t0), length=F(t1) - F(t0))
    st.color = rgb
    st.blend_type = blend
    return st


def flash(t, peak, frames):
    st = color(f"Flash_{t:.2f}", 12, t, t + frames / FPS, (1.0, 0.97, 0.9), "ADD")
    key(st, "blend_alpha", F(t), peak, "EXPO", "EASE_OUT")
    key(st, "blend_alpha", F(t) + frames - 1, 0.0)


# ------------------------------------------------------------------ the gameplay (channels 1 and 2, dissolving)
half = XF // 2
for si, shots in enumerate(SECTIONS):
    for i, (t0, t1, take, src, speed, push) in enumerate(shots):
        pa, pb = push[:2]
        first, last = i == 0, i == len(shots) - 1
        f0 = F(t0) - (0 if first else half)
        f1 = F(t1) + (0 if last else XF - half)
        ch = 1 + (i % 2)
        st = footage(f"S{si}_{i}_{take}", ch, f0, f1, take, src - (F(t0) - f0) / FPS, speed)   # the in-point moves back with a dissolve
        st.blend_type = "ALPHA_OVER"
        st.transform.offset_y = push[2] if len(push) > 2 else 0.0   # a raise (px) hides a banner under the top bar
        for prop in ("scale_x", "scale_y"):
            key(st.transform, prop, f0, pa, "LINEAR")
            key(st.transform, prop, f1 - 1, pb, "LINEAR")
        if ch == 2:
            # the upper strip carries the dissolves: in over the previous shot, out to the next one
            if not first:
                key(st, "blend_alpha", f0, 0.0, "SINE", "EASE_IN_OUT")
                key(st, "blend_alpha", f0 + XF, 1.0)
            if not last:
                key(st, "blend_alpha", f1 - XF, 1.0, "SINE", "EASE_IN_OUT")
                key(st, "blend_alpha", f1 - 1, 0.0)

# ------------------------------------------------------------------ the cards: blurred gameplay (1 + blur 3), the title (5), its glow (6 -> 7)
for t0, t1, card, bg, src, speed, cap, capy in CARDS:
    b = footage(f"CardBG_{card}", 1, F(t0), F(t1), bg, src, speed)
    bl = S.new_effect(f"Blur_{card}", "GAUSSIAN_BLUR", 3, F(t0), length=F(t1) - F(t0), input1=b)
    bl.size_x = bl.size_y = 28.0
    bl.color_multiply = 0.42 if card != "logo" else 0.38
    for prop in ("scale_x", "scale_y"):
        key(b.transform, prop, F(t0), 1.12, "LINEAR")
        key(b.transform, prop, F(t1) - 1, 1.04, "LINEAR")
    d = os.path.join(ROOT, TITLES, card)
    files = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    n = F(t1) - F(t0)
    seq = [files[min(i, len(files) - 1)] for i in range(n)]
    tt = S.new_image(f"Title_{card}", os.path.join(d, seq[0]), 5, F(t0), fit_method="FIT")
    t2 = S.new_image(f"TitleGlowSrc_{card}", os.path.join(d, seq[0]), 6, F(t0), fit_method="FIT")
    for f in seq[1:]:
        tt.elements.append(f)
        t2.elements.append(f)
    tt.blend_type = "ALPHA_OVER"
    gl = S.new_effect(f"Glow_{card}", "GLOW", 7, F(t0), length=n, input1=t2)
    gl.use_only_boost = True
    gl.threshold = 0.35
    gl.boost_factor = 0.9
    gl.blur_radius = 9.0
    gl.quality = 5
    gl.blend_type = "ADD"
    if card == "logo":
        key(gl, "blend_alpha", F(t0), 0.2)
        key(gl, "blend_alpha", F(t0 + 2.6), 1.0, "SINE", "EASE_IN_OUT")
        key(gl, "blend_alpha", F(t0 + 5.0), 0.45, "SINE", "EASE_IN_OUT")
    if cap:
        CAPTIONS.append((t0 + 0.5, t1 - 0.08, cap, 24 if card == "heroes" else 30, capy))
    flash(t0, 0.6, 8)                       # into the card (the transition music's hit)
    if card != "logo":
        flash(t1, 0.28, 5)                  # back to the game

# ------------------------------------------------------------------ the grade (adjustment layer, 4)
adj = S.new_effect("Grade", "ADJUSTMENT", 4, 1, length=F(END) - 1)
adj.color_saturation = 1.1
cbm = adj.modifiers.new("Balance", "COLOR_BALANCE")
cbm.color_balance.correction_method = "LIFT_GAMMA_GAIN"
cbm.color_balance.lift = (0.975, 1.0, 1.04)
cbm.color_balance.gamma = (1.0, 1.0, 1.02)
cbm.color_balance.gain = (1.06, 1.02, 0.95)
cv = adj.modifiers.new("Contrast", "CURVES")
c = cv.curve_mapping.curves[3]
c.points.new(0.25, 0.21)
c.points.new(0.75, 0.795)
cv.curve_mapping.update()

# ------------------------------------------------------------------ captions (8, 9)
font_b = bpy.data.fonts.load(os.path.join(FONTS, "Cinzel-Bold.ttf"), check_existing=True)
font_s = bpy.data.fonts.load(os.path.join(FONTS, "Barlow-Medium.ttf"), check_existing=True)
font_n = bpy.data.fonts.load(os.path.join(FONTS, "Barlow-SemiBold.ttf"), check_existing=True)   # Cinzel's 1 reads as I
for i, (t0, t1, text, size, y) in enumerate(CAPTIONS):
    tx = S.new_effect(f"Cap{i}", "TEXT", 8 + (i % 2), F(t0), length=F(t1) - F(t0))
    tx.text = text
    tx.font = font_s if size <= 22 else (font_n if any(ch.isdigit() for ch in text) else font_b)
    tx.font_size = size
    tx.color = (0.96, 0.9, 0.78, 1.0) if size > 22 else (0.75, 0.75, 0.78, 1.0)
    tx.use_shadow = True
    tx.shadow_color = (0, 0, 0, 0.8)
    tx.shadow_blur = 0.6
    tx.anchor_x = tx.anchor_y = "CENTER"
    tx.alignment_x = "CENTER"
    tx.blend_type = "ALPHA_OVER"
    key(tx, "blend_alpha", F(t0), 0.0, "SINE", "EASE_OUT")
    key(tx, "blend_alpha", F(t0) + 15, 1.0)
    key(tx, "blend_alpha", F(t1) - 12, 1.0, "SINE", "EASE_IN")
    key(tx, "blend_alpha", F(t1) - 1, 0.0)
    tx.location = (0.5, y - 0.012)
    tx.keyframe_insert("location", frame=F(t0))
    tx.location = (0.5, y)
    tx.keyframe_insert("location", frame=F(t0) + 24)

# ------------------------------------------------------------------ vignette (10), bars (11), black (13), music (14)
vig_path = os.path.join(ROOT, "vignette.png")
vg = S.new_image("Vignette", vig_path, 10, 1, fit_method="FIT")
vg.frame_final_duration = F(END) - 1
vg.blend_type = "MULTIPLY"
BAR = 138
for name, sign in (("BarTop", 1), ("BarBot", -1)):
    b = color(name, 11, 0.0, END, (0, 0, 0))
    b.transform.scale_y = BAR / 1080
    b.transform.origin = (0.5, 0.5)
    key(b.transform, "offset_y", F(0.8), sign * (540 + BAR / 2), "SINE", "EASE_OUT")
    key(b.transform, "offset_y", F(2.4), sign * (540 - BAR / 2), "LINEAR")
blk = color("Black", 13, 0.0, END, (0, 0, 0))
key(blk, "blend_alpha", 1, 1.0)
key(blk, "blend_alpha", F(0.3), 1.0, "SINE", "EASE_IN_OUT")
key(blk, "blend_alpha", F(1.8), 0.0, "LINEAR")
key(blk, "blend_alpha", F(78.0), 0.0, "SINE", "EASE_IN")
key(blk, "blend_alpha", F(END) - 1, 1.0)
S.new_sound("Music", os.path.join(ROOT, MUSIC), 14, 1)
finish_keys()

if SAVE:
    bpy.ops.wm.save_as_mainfile(filepath=SAVE, copy=True)
try:
    if STILLS:
        os.makedirs(os.path.join(ROOT, STILLS_DIR), exist_ok=True)
        for t in STILLS:
            sc.frame_set(F(t))
            sc.render.filepath = os.path.join(ROOT, STILLS_DIR, f"t{t:05.2f}.png")
            bpy.ops.render.render(write_still=True)
        print(f"EDIT OK stills={len(STILLS)}")
    else:
        out = os.path.join(ROOT, OUT_DIR)
        os.makedirs(out, exist_ok=True)
        sc.render.filepath = os.path.join(out, "")
        bpy.ops.render.render(animation=True)
        print(f"EDIT OK frames={sc.frame_end}")
except Exception as ex:
    import traceback
    traceback.print_exc()
    print(f"EDIT FAIL {ex}")
    sys.exit(1)
