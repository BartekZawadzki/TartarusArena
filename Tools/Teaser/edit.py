"""edit.py — the teaser's edit in Blender's sequencer: the recorded takes cut to the soundtrack's bars, the gold title
cards over blurred gameplay, white flashes on the hits, a slow push on every shot and punch-ins on the big moments,
a grade (teal shadows, warm highlights, an S-curve), a vignette, 2.39:1 bars sliding in, English captions.
Run: blender -b --factory-startup --python-exit-code 1 -P edit.py -- <teaser_dir> [--edl cut.py] [--stills 3.0,8.0] [--save x.blend]
Renders PNG frames to <teaser_dir>/edit/ (or stills to <teaser_dir>/stills/); prints EDIT OK / EDIT FAIL.
"""
import math
import os
import random
import sys

import bpy
import numpy as np
from bpy_extras import anim_utils

ARGS = sys.argv[sys.argv.index("--") + 1:]
ROOT = ARGS[0]
STILLS = None
if "--stills" in ARGS:
    STILLS = [float(x) for x in ARGS[ARGS.index("--stills") + 1].split(",")]
SAVE = ARGS[ARGS.index("--save") + 1] if "--save" in ARGS else None
FPS = 30
END = 55.0
FONTS = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Content", "Data", "Fonts"))
random.seed(7)


def F(t):
    return int(round(t * FPS)) + 1


# ------------------------------------------------------------------ the cut (timeline seconds; take, in-point s, speed)
SHOTS = [
    # the arena from above, in from black
    (1.0, 7.0, "aerial", 0.0, 1.0, {"push": (1.0, 1.06)}),
    # the heroes
    (9.0, 11.0, "hero_c", 1.0, 1.0, {"push": (1.0, 1.06)}),
    (11.0, 13.0, "hero_b", 1.6, 1.0, {"punch": 12.4, "push": (1.0, 1.04)}),
    (13.0, 15.0, "hero_c", 3.0, 1.0, {"push": (1.02, 1.08)}),
    (15.0, 17.0, "hero_b", 4.0, 1.0, {"push": (1.02, 1.08)}),
    # 5 vs 5
    (19.0, 23.0, "fight_a", 1.0, 1.0, {"push": (1.0, 1.05)}),
    (23.0, 25.0, "follow_b", 1.9, 1.0, {"push": (1.0, 1.06)}),
    (25.0, 27.0, "fight_a", 4.8, 1.0, {"punch": 25.3, "push": (1.0, 1.04)}),
    (27.0, 29.0, "follow_b", 5.3, 1.0, {"punch": 27.75, "push": (1.0, 1.04)}),
    # Conquest
    (31.0, 34.0, "aerial_c", 0.0, 1.5, {"push": (1.0, 1.05)}),
    (34.0, 36.0, "tower_b", 6.0, 1.0, {"push": (1.0, 1.06)}),
    (36.0, 38.5, "fight_d", 2.4, 1.0, {"punch": 37.55, "push": (1.0, 1.04)}),
    (38.5, 43.0, "fight_c", 6.5, 1.0, {"punch": 41.0, "push": (1.0, 1.06)}),
]
# the montage: a cut on every beat (43-47)
MONTAGE = [("follow_a", 3.0), ("fight_a", 5.1), ("hero_c", 3.0), ("fight_d", 3.8),
           ("fight_b", 10.0), ("follow_a", 5.5), ("hero_b", 2.0), ("fight_a", 8.6)]
CARDS = [
    # start, end, card, background take + in-point, caption
    (7.0, 9.0, "heroes", "hero_b", 0.2, "GREYSTONE  ·  COUNTESS  ·  GIDEON  ·  SPARROW  ·  KWANG  ·  CRUNCH  ·  IGGY & SCORCH  ·  KHAIMERA  ·  MORIGESH  ·  REVENANT  ·  SEVAROG"),
    (17.0, 19.0, "5v5", "fight_a", 0.0, "TEAM FIGHTS   ·   SMART BOTS   ·   LAN PLAY"),
    (29.0, 31.0, "conquest", "aerial_c", 6.0, "PUSH THE LANES   ·   TAKE THE TOWERS   ·   DESTROY THE CORE"),
    (47.0, END, "logo", "hero_c", 5.0, None),
]
CAPTIONS = [
    # start, end, text, size, y
    (2.3, 6.6, "THE   ARENA   AWAITS", 58, 0.5),
    (49.4, 53.6, "11 HEROES   ·   ARENA   ·   CONQUEST", 34, 0.235),
    (50.2, 53.6, "A fan-made MOBA built with Paragon assets released by Epic Games", 20, 0.165),
]
HITS = [7.0, 17.0, 29.0, 47.0]
SOFT_CUTS = [9.0, 19.0, 31.0]
OUT_DIR, STILLS_DIR = "edit", "stills"
# another cut: --edl <file.py> sets SHOTS / MONTAGE / CARDS / CAPTIONS (and OUT_DIR, STILLS_DIR) over these
if "--edl" in ARGS:
    exec(open(ARGS[ARGS.index("--edl") + 1], encoding="utf-8-sig").read())

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


def take_files(take):
    d = os.path.join(ROOT, "frames", take)
    return d, sorted(f for f in os.listdir(d) if f.endswith(".png"))


def footage(name, ch, t0, t1, take, src, speed=1.0):
    d, files = take_files(take)
    n = F(t1) - F(t0)
    idx, pos = [], src * FPS
    for _ in range(n):
        idx.append(min(len(files) - 1, int(round(pos))))
        pos += speed
    st = S.new_image(name, os.path.join(d, files[idx[0]]), ch, F(t0), fit_method="FIT")
    for i in idx[1:]:
        st.elements.append(files[i])
    return st


def color(name, ch, t0, t1, rgb, blend="ALPHA_OVER"):
    st = S.new_effect(name, "COLOR", ch, F(t0), length=F(t1) - F(t0))
    st.color = rgb
    st.blend_type = blend
    return st


def flash(t, peak=1.0, frames=8, ch=11):
    st = color(f"Flash_{t:.2f}", ch, t, t + frames / FPS, (1.0, 0.97, 0.9), "ADD")
    key(st, "blend_alpha", F(t), peak, "EXPO", "EASE_OUT")
    key(st, "blend_alpha", F(t) + frames - 1, 0.0)


def push(st, t0, t1, a, b):
    for prop in ("scale_x", "scale_y"):
        key(st.transform, prop, F(t0), a, "LINEAR")
        key(st.transform, prop, F(t1) - 1, b, "LINEAR")


def punch(st, t, t1, base):
    """A hard zoom on an impact: in over 4 frames, settling, with a shake."""
    f = F(t)
    for prop in ("scale_x", "scale_y"):
        key(st.transform, prop, f - 1, base, "BACK", "EASE_OUT")
        key(st.transform, prop, f + 4, base * 1.16, "SINE", "EASE_OUT")
        key(st.transform, prop, F(t1) - 1, base * 1.12, "LINEAR")
    for k in range(10):
        amp = 14.0 * (1 - k / 10)
        key(st.transform, "offset_x", f + k, random.uniform(-amp, amp), "LINEAR")
        key(st.transform, "offset_y", f + k, random.uniform(-amp, amp), "LINEAR")
    key(st.transform, "offset_x", f + 10, 0.0, "LINEAR")
    key(st.transform, "offset_y", f + 10, 0.0, "LINEAR")


# ------------------------------------------------------------------ footage (channel 1), card backgrounds (1 + blur 2)
for i, (t0, t1, take, src, speed, o) in enumerate(SHOTS):
    st = footage(f"Shot{i:02d}_{take}", 1, t0, t1, take, src, speed)
    a, b = o.get("push", (1.0, 1.0))
    if "punch" in o:
        tp = o["punch"]
        mid = a + (b - a) * (tp - t0) / (t1 - t0)
        for prop in ("scale_x", "scale_y"):
            key(st.transform, prop, F(t0), a, "LINEAR")
        punch(st, tp, t1, mid)
    else:
        push(st, t0, t1, a, b)
for k, (take, src) in enumerate(MONTAGE):
    t0 = 43.0 + k * 0.5
    st = footage(f"Mont{k}_{take}", 1, t0, t0 + 0.5, take, src, 1.0)
    for prop in ("scale_x", "scale_y"):
        key(st.transform, prop, F(t0), 1.14, "EXPO", "EASE_OUT")
        key(st.transform, prop, F(t0) + 8, 1.03, "LINEAR")
        key(st.transform, prop, F(t0 + 0.5) - 1, 1.0, "LINEAR")
    flash(t0, 0.55 if k else 0.9, 4)

for t0, t1, card, bg, src, cap in CARDS:
    b = footage(f"CardBG_{card}", 1, t0, t1, bg, src, 0.6 if card == "logo" else 1.0)
    bl = S.new_effect(f"Blur_{card}", "GAUSSIAN_BLUR", 2, F(t0), length=F(t1) - F(t0), input1=b)
    bl.size_x = bl.size_y = 28.0
    bl.color_multiply = 0.42 if card != "logo" else 0.38
    for prop in ("scale_x", "scale_y"):
        key(b.transform, prop, F(t0), 1.12, "LINEAR")
        key(b.transform, prop, F(t1) - 1, 1.04, "LINEAR")
    # the title (channel 4) and its glow (5)
    d = os.path.join(ROOT, "titles", card)
    files = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    n = F(t1) - F(t0)
    tt = S.new_image(f"Title_{card}", os.path.join(d, files[0]), 4, F(t0), fit_method="FIT")
    for f in files[1:n]:
        tt.elements.append(f)
    if len(files) < n:   # the logo holds its last frame
        for _ in range(n - len(files)):
            tt.elements.append(files[-1])
    tt.blend_type = "ALPHA_OVER"
    # the glow reads a copy of the title (a strip feeding an effect is not drawn itself)
    t2 = S.new_image(f"TitleGlowSrc_{card}", os.path.join(d, files[0]), 5, F(t0), fit_method="FIT")
    for e in tt.elements[1:]:
        t2.elements.append(e.filename)
    gl = S.new_effect(f"Glow_{card}", "GLOW", 6, F(t0), length=n, input1=t2)
    gl.use_only_boost = True
    gl.threshold = 0.35
    gl.boost_factor = 0.9
    gl.blur_radius = 9.0
    gl.quality = 5
    gl.blend_type = "ADD"
    if card == "logo":   # the logo breathes out: its glow swells with the sweep
        key(gl, "blend_alpha", F(t0), 0.2)
        key(gl, "blend_alpha", F(t0 + 2.6), 1.0, "SINE", "EASE_IN_OUT")
        key(gl, "blend_alpha", F(t0 + 5.0), 0.45, "SINE", "EASE_IN_OUT")
    if cap:
        CAPTIONS.append((t0 + 0.45, t1 - 0.05, cap, 24 if card == "heroes" else 30, {"heroes": 0.37, "5v5": 0.315}.get(card, 0.36)))

# ------------------------------------------------------------------ the grade (adjustment layer, channel 3)
adj = S.new_effect("Grade", "ADJUSTMENT", 3, 1, length=F(END) - 1)
adj.color_saturation = 1.12
cb = adj.modifiers.new("Balance", "COLOR_BALANCE")
cb.color_balance.correction_method = "LIFT_GAMMA_GAIN"
cb.color_balance.lift = (0.975, 1.0, 1.045)
cb.color_balance.gamma = (1.0, 1.0, 1.02)
cb.color_balance.gain = (1.07, 1.02, 0.94)
cv = adj.modifiers.new("Contrast", "CURVES")
c = cv.curve_mapping.curves[3]
c.points.new(0.25, 0.205)
c.points.new(0.75, 0.80)
cv.curve_mapping.update()

# ------------------------------------------------------------------ captions (channel 6)
font_b = bpy.data.fonts.load(os.path.join(FONTS, "Cinzel-Bold.ttf"), check_existing=True)
font_s = bpy.data.fonts.load(os.path.join(FONTS, "Barlow-Medium.ttf"), check_existing=True)
font_n = bpy.data.fonts.load(os.path.join(FONTS, "Barlow-SemiBold.ttf"), check_existing=True)   # Cinzel's 1 reads as I
for i, (t0, t1, text, size, y) in enumerate(CAPTIONS):
    tx = S.new_effect(f"Cap{i}", "TEXT", 7 + (i % 2), F(t0), length=F(t1) - F(t0))
    tx.text = text
    tx.font = font_s if size <= 22 else (font_n if any(ch.isdigit() for ch in text) else font_b)
    tx.font_size = size
    tx.color = (0.96, 0.9, 0.78, 1.0) if size > 22 else (0.75, 0.75, 0.78, 1.0)
    tx.use_shadow = True
    tx.shadow_color = (0, 0, 0, 0.8)
    tx.shadow_blur = 0.6
    tx.anchor_x = tx.anchor_y = "CENTER"
    tx.alignment_x = "CENTER"
    tx.location = (0.5, y - 0.012)
    tx.blend_type = "ALPHA_OVER"
    key(tx, "blend_alpha", F(t0), 0.0, "SINE", "EASE_OUT")
    key(tx, "blend_alpha", F(t0) + 12, 1.0)
    key(tx, "blend_alpha", F(t1) - 10, 1.0, "SINE", "EASE_IN")
    key(tx, "blend_alpha", F(t1) - 1, 0.0)
    tx.location = (0.5, y - 0.012)
    tx.keyframe_insert("location", frame=F(t0))
    tx.location = (0.5, y)
    tx.keyframe_insert("location", frame=F(t0) + 18)

# ------------------------------------------------------------------ the vignette (8), the bars (9), flashes (10), black (11)
vig_path = os.path.join(ROOT, "vignette.png")
if not os.path.exists(vig_path):
    w, h = 1920, 1080
    yy, xx = np.mgrid[0:h, 0:w]
    r = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2) / math.sqrt(2)
    v = 1.0 - 0.6 * np.clip((r - 0.42) / 0.58, 0, 1) ** 1.8
    px = np.ones((h, w, 4), dtype=np.float32)
    px[..., 0] = px[..., 1] = px[..., 2] = v
    img = bpy.data.images.new("vig", w, h, alpha=True)
    img.pixels.foreach_set(px.ravel())
    img.filepath_raw = vig_path
    img.file_format = "PNG"
    img.save()
vg = S.new_image("Vignette", vig_path, 9, 1, fit_method="FIT")
vg.frame_final_duration = F(END) - 1
vg.blend_type = "MULTIPLY"

BAR = 138   # 2.39:1 in a 1080 frame
for name, sign in (("BarTop", 1), ("BarBot", -1)):
    b = color(name, 10, 0.0, END, (0, 0, 0))
    b.transform.scale_y = BAR / 1080
    b.transform.origin = (0.5, 0.5)
    key(b.transform, "offset_y", F(1.0), sign * (540 + BAR / 2), "SINE", "EASE_OUT")
    key(b.transform, "offset_y", F(2.6), sign * (540 - BAR / 2), "LINEAR")

for t in HITS:
    flash(t, 1.0, 9)
for t in SOFT_CUTS:
    flash(t, 0.45, 5)

blk = color("Black", 12, 0.0, END, (0, 0, 0))
key(blk, "blend_alpha", 1, 1.0)
key(blk, "blend_alpha", F(1.0), 1.0, "SINE", "EASE_IN_OUT")
key(blk, "blend_alpha", F(2.2), 0.0, "LINEAR")
key(blk, "blend_alpha", F(53.6), 0.0, "SINE", "EASE_IN")
key(blk, "blend_alpha", F(END) - 1, 1.0)

# ------------------------------------------------------------------ the music (12)
snd = S.new_sound("Music", os.path.join(ROOT, "teaser_music.wav"), 13, 1)
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
