"""edit_short.py — the short teaser (operator 2026-10-02: "a short teaser, about a minute, with your motion-designer
skills and the music you just picked", from their first recording, "and weave in your best 5v5 scenes").
Motion design in Blender's sequencer: kinetic type (PICK YOUR HERO, a 3-2-1 countdown on the drum hits, FIGHT!,
BUILD YOUR HERO, the POWER OF TARTARUS, OUTPLAY YOUR RIVALS), the shop as a framed panel sliding in, a three-way split
screen with gold dividers, lower thirds with drawn lines, dissolves inside a section, flashes on the hits; the grade,
vignette and 2.39:1 bars as before; the music music_short.py (music_v3's theme and stingers on this timeline).
Run: blender -b --factory-startup --python-exit-code 1 -P edit_short.py -- <teaser_dir> [--stills 3.0,8.0] [--save x.blend]
"""
import os
import sys

import bpy
from bpy_extras import anim_utils

ARGS = sys.argv[sys.argv.index("--") + 1:]
ROOT = ARGS[0]
STILLS = [float(x) for x in ARGS[ARGS.index("--stills") + 1].split(",")] if "--stills" in ARGS else None
SAVE = ARGS[ARGS.index("--save") + 1] if "--save" in ARGS else None
FPS = 30
END = 59.0
XF = 6
FONTS = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Content", "Data", "Fonts"))
MUSIC = "teaser_music_short.wav"
OUT_DIR, STILLS_DIR = "edit_short", "stills_short"
GOLD = (0.97, 0.8, 0.45, 1.0)
GOLD_RGB = (0.97, 0.78, 0.4)
W, H = 1920, 1080


def F(t):
    return int(round(t * FPS)) + 1


# ------------------------------------------------------------------ the cut: sections of (start, end, take, in-point, speed, push [, raise px])
SECTIONS = [
    [(0.0, 4.8, "u1_roster", 0.3, 0.45, (1.0, 1.08))],                       # pick your hero
    [(4.8, 7.2, "s_count", 0.2, 1.0, (1.04, 1.1))],                          # 3, 2, 1
    [(7.2, 9.6, "u1_lineup", 0.5, 1.0, (1.2, 1.08, 40))],                     # FIGHT! (raised: the game's own FIGHT! under the bar)
    [(9.6, 12.0, "fight_e", 5.0, 1.0, (1.0, 1.04)),                          # the game's own...
     (12.0, 14.4, "s_ring", 0.5, 1.0, (1.0, 1.05)),                          # ...and the operator's
     (14.4, 16.8, "follow_b", 5.0, 1.0, (1.0, 1.04))],
    [(21.6, 24.0, "u1_purple", 0.2, 1.0, (1.1, 1.12, 45)),                   # raised: FIRST BLOOD under the bar
     (24.0, 26.4, "fight_g", 0.2, 1.0, (1.0, 1.04)),
     (26.4, 28.8, "u1_dkill", 0.6, 1.0, (1.0, 1.04, -30))],                  # lowered: KHAIMERA: DOUBLE KILL! shows
    [(28.8, 31.8, "u1_cube", 0.0, 1.0, (1.06, 1.12, 45))],                   # the power of Tartarus
    [(31.8, 34.2, "fight_h", 0.4, 1.0, (1.0, 1.04)),
     (34.2, 36.6, "s_g2a", 0.4, 1.0, (1.0, 1.05))],
    [(41.4, 43.8, "s_g2b", 0.3, 1.0, (1.0, 1.05)),
     (43.8, 46.2, "fight_g", 3.6, 1.0, (1.0, 1.04)),
     (46.2, 48.6, "s_g2c", 0.2, 1.0, (1.0, 1.05)),
     (48.6, 51.0, "fight_e", 9.3, 1.0, (1.0, 1.05))],
]
SHOP = (16.8, 21.6, "s_shop", 0.0, 0.48)   # the shop is open for the first 72 frames: slowed to fill the panel
SPLIT = (36.6, 41.4, [("fight_f", 8.2, 1.0), ("s_ring", 2.0, 1.0), ("fight_g", 5.0, 1.0)])
LOGO = (51.0, END, "fight_e", 6.0, 0.4)

# ------------------------------------------------------------------ scene and helpers
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.resolution_x, sc.render.resolution_y = W, H
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
font_black = bpy.data.fonts.load(os.path.join(FONTS, "Cinzel-Black.ttf"), check_existing=True)
font_bold = bpy.data.fonts.load(os.path.join(FONTS, "Cinzel-Bold.ttf"), check_existing=True)
font_semi = bpy.data.fonts.load(os.path.join(FONTS, "Barlow-SemiBold.ttf"), check_existing=True)
font_med = bpy.data.fonts.load(os.path.join(FONTS, "Barlow-Medium.ttf"), check_existing=True)


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
    d = os.path.join(ROOT, "frames", take)
    files = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    idx, pos = [], src * FPS
    for _ in range(f1 - f0):
        idx.append(max(0, min(len(files) - 1, int(round(pos)))))
        pos += speed
    st = S.new_image(name, os.path.join(d, files[idx[0]]), ch, f0, fit_method="FIT")
    for i in idx[1:]:
        st.elements.append(files[i])
    st.blend_type = "ALPHA_OVER"
    return st


def color(name, ch, f0, f1, rgb, blend="ALPHA_OVER"):
    st = S.new_effect(name, "COLOR", ch, f0, length=f1 - f0)
    st.color = rgb
    st.blend_type = blend
    return st


def flash(t, peak, frames):
    st = color(f"Flash_{t:.2f}", 23, F(t), F(t) + frames, (1.0, 0.97, 0.9), "ADD")
    key(st, "blend_alpha", F(t), peak, "EXPO", "EASE_OUT")
    key(st, "blend_alpha", F(t) + frames - 1, 0.0)


def line(name, ch, t0, t1, x, y, w, h, grow="x", draw=0.35, rgb=GOLD_RGB):
    """A gold line (x, y = its centre in px from the frame's centre) drawn on from its middle."""
    st = color(name, ch, F(t0), F(t1), rgb)
    st.transform.origin = (0.5, 0.5)
    st.transform.offset_x, st.transform.offset_y = x, y
    sx, sy = w / W, h / H
    st.transform.scale_x, st.transform.scale_y = sx, sy
    p = "scale_x" if grow == "x" else "scale_y"
    key(st.transform, p, F(t0), 0.0005, "EXPO", "EASE_OUT")
    key(st.transform, p, F(t0 + draw), sx if grow == "x" else sy)
    key(st, "blend_alpha", F(t1) - 8, 1.0, "SINE", "EASE_IN")
    key(st, "blend_alpha", F(t1) - 1, 0.0)
    return st


def text(name, ch, t0, t1, body, font, size, x, y, rgb=GOLD, anchor="CENTER", fade_in=10, fade_out=10, shadow=True):
    st = S.new_effect(name, "TEXT", ch, F(t0), length=F(t1) - F(t0))
    st.text = body
    st.font = font
    st.font_size = size
    st.color = rgb
    st.use_shadow = shadow
    st.shadow_color = (0, 0, 0, 0.85)
    st.shadow_blur = 0.7
    st.anchor_x, st.anchor_y = anchor, "CENTER"
    st.alignment_x = anchor if anchor != "CENTER" else "CENTER"
    st.location = (x, y)
    st.blend_type = "ALPHA_OVER"
    key(st, "blend_alpha", F(t0), 0.0, "SINE", "EASE_OUT")
    key(st, "blend_alpha", F(t0) + fade_in, 1.0)
    key(st, "blend_alpha", F(t1) - fade_out, 1.0, "SINE", "EASE_IN")
    key(st, "blend_alpha", F(t1) - 1, 0.0)
    return st


def punch_text(name, t0, t1, body, font, size, from_scale=1.6, glow=True, fade_out=8, y=0.5):
    """Kinetic type at the centre: in big, slams to size (an exponential ease), glows, fades."""
    made = []
    for ch, suffix in ((16, ""), (19, "_glowsrc")) if glow else ((16, ""),):
        st = text(name + suffix, ch, t0, t1, body, font, size, 0.5, y, fade_in=3, fade_out=fade_out)
        for p in ("scale_x", "scale_y"):
            key(st.transform, p, F(t0), from_scale, "EXPO", "EASE_OUT")
            key(st.transform, p, F(t0) + 9, 1.0, "LINEAR")
            key(st.transform, p, F(t1) - 1, 1.04)
        made.append(st)
    if glow:
        g = S.new_effect(name + "_glow", "GLOW", 20, F(t0), length=F(t1) - F(t0), input1=made[1])
        g.use_only_boost, g.threshold, g.boost_factor, g.blur_radius, g.quality = True, 0.3, 1.0, 12.0, 5
        g.blend_type = "ADD"
    return made[0]


# ------------------------------------------------------------------ the footage (1, 2: dissolves inside a section)
half = XF // 2
for si, shots in enumerate(SECTIONS):
    for i, (t0, t1, take, src, speed, push) in enumerate(shots):
        first, last = i == 0, i == len(shots) - 1
        f0 = F(t0) - (0 if first else half)
        f1 = F(t1) + (0 if last else XF - half)
        ch = 1 + (i % 2)
        st = footage(f"S{si}_{i}_{take}", ch, f0, f1, take, src - (F(t0) - f0) / FPS, speed)
        st.transform.offset_y = push[2] if len(push) > 2 else 0.0
        for p in ("scale_x", "scale_y"):
            key(st.transform, p, f0, push[0], "LINEAR" if push[0] <= push[1] else "EXPO", "AUTO" if push[0] <= push[1] else "EASE_OUT")
            key(st.transform, p, f1 - 1, push[1], "LINEAR")
        if ch == 2:
            if not first:
                key(st, "blend_alpha", f0, 0.0, "SINE", "EASE_IN_OUT")
                key(st, "blend_alpha", f0 + XF, 1.0)
            if not last:
                key(st, "blend_alpha", f1 - XF, 1.0, "SINE", "EASE_IN_OUT")
                key(st, "blend_alpha", f1 - 1, 0.0)
        if si in (0, 1):   # under the big type: the roster a little soft, the countdown blurred (the game's own "Fight in 3")
            bl = S.new_effect(f"Soft{si}", "GAUSSIAN_BLUR", 3, f0, length=f1 - f0, input1=st)
            bl.size_x = bl.size_y = 5.0 if si == 0 else 26.0
            bl.color_multiply = 0.62 if si == 0 else 0.42

# ------------------------------------------------------------------ the shop as a framed panel (4) over its own blurred self (1 + blur 3)
t0, t1, take, src, speed = SHOP
bg = footage("ShopBG", 1, F(t0), F(t1), take, src, speed)
bb = S.new_effect("ShopBlur", "GAUSSIAN_BLUR", 3, F(t0), length=F(t1) - F(t0), input1=bg)
bb.size_x = bb.size_y = 22.0
bb.color_multiply = 0.32
pn = footage("ShopPanel", 4, F(t0), F(t1), take, src, speed)
PS, PX = 0.56, 300.0
pn.transform.scale_x = pn.transform.scale_y = PS
key(pn.transform, "offset_x", F(t0), 1500.0, "EXPO", "EASE_OUT")
key(pn.transform, "offset_x", F(t0) + 15, PX, "LINEAR")
key(pn.transform, "offset_x", F(t1) - 1, PX - 30.0)
pw, ph = W * PS, H * PS
ta = t0 + 0.5
line("PanelTop", 9, ta, t1, PX, ph / 2 + 3, pw + 10, 3)
line("PanelBottom", 10, ta + 0.05, t1, PX, -ph / 2 - 3, pw + 10, 3)
line("PanelLeft", 11, ta + 0.1, t1, PX - pw / 2 - 3, 0, 3, ph + 10, grow="y")
line("PanelRight", 12, ta + 0.15, t1, PX + pw / 2 + 3, 0, 3, ph + 10, grow="y")
text("ShopT1", 16, t0 + 0.45, t1 - 0.1, "BUILD", font_black, 96, 0.2, 0.575)
text("ShopT2", 17, t0 + 0.65, t1 - 0.1, "YOUR HERO", font_bold, 52, 0.2, 0.48)
text("ShopT3", 18, t0 + 0.95, t1 - 0.1, "PARTS  ·  UPGRADES  ·  LEGENDARIES", font_semi, 24, 0.2, 0.415, rgb=(0.86, 0.86, 0.88, 1.0))
line("ShopUnder", 21, t0 + 0.8, t1 - 0.1, (0.2 - 0.5) * W, (0.445 - 0.5) * H, 300, 3)

# ------------------------------------------------------------------ the split screen: three fights in slices sliding in (4, 5, 6), gold dividers
t0, t1, parts = SPLIT
for k, (take, src, speed) in enumerate(parts):
    sl = footage(f"Split{k}_{take}", 4 + k, F(t0), F(t1), take, src, speed)
    sl.crop.min_x = sl.crop.max_x = 640            # the middle third of each fight
    sl.transform.offset_x = (k - 1) * 640.0
    start_y = -H if k % 2 == 0 else H
    key(sl.transform, "offset_y", F(t0) + 3 * k, start_y, "EXPO", "EASE_OUT")
    key(sl.transform, "offset_y", F(t0) + 3 * k + 14, 0.0, "LINEAR")
    for p in ("scale_x", "scale_y"):
        key(sl.transform, p, F(t0), 1.0, "LINEAR")
        key(sl.transform, p, F(t1) - 1, 1.04, "LINEAR")
line("SplitDiv1", 9, t0 + 0.35, t1, -320, 0, 4, 804, grow="y")
line("SplitDiv2", 10, t0 + 0.42, t1, 320, 0, 4, 804, grow="y")
band = color("SplitBand", 11, F(t0 + 1.2), F(t1), (0.0, 0.0, 0.0))
band.transform.scale_y = 120 / H
key(band.transform, "scale_x", F(t0 + 1.2), 0.0005, "EXPO", "EASE_OUT")
key(band.transform, "scale_x", F(t0 + 1.6), 1.0)
band.blend_alpha = 0.6
punch_text("Outplay", t0 + 1.35, t1 - 0.05, "OUTPLAY YOUR RIVALS", font_bold, 62, from_scale=1.25, glow=False, fade_out=10)

# ------------------------------------------------------------------ the logo (13 -> glow 15), over blurred play (1 + blur 3)
t0, t1, take, src, speed = LOGO
lb = footage("LogoBG", 1, F(t0), F(t1), take, src, speed)
lbb = S.new_effect("LogoBlur", "GAUSSIAN_BLUR", 3, F(t0), length=F(t1) - F(t0), input1=lb)
lbb.size_x = lbb.size_y = 28.0
lbb.color_multiply = 0.38
d = os.path.join(ROOT, "titles_v3", "logo")
files = sorted(f for f in os.listdir(d) if f.endswith(".png"))
n = F(t1) - F(t0)
seq = [files[min(i, len(files) - 1)] for i in range(n)]
tt = S.new_image("Logo", os.path.join(d, seq[0]), 13, F(t0), fit_method="FIT")
t2 = S.new_image("LogoGlowSrc", os.path.join(d, seq[0]), 14, F(t0), fit_method="FIT")
for f in seq[1:]:
    tt.elements.append(f)
    t2.elements.append(f)
tt.blend_type = "ALPHA_OVER"
gl = S.new_effect("LogoGlow", "GLOW", 15, F(t0), length=n, input1=t2)
gl.use_only_boost, gl.threshold, gl.boost_factor, gl.blur_radius, gl.quality = True, 0.35, 0.9, 9.0, 5
gl.blend_type = "ADD"
key(gl, "blend_alpha", F(t0), 0.2)
key(gl, "blend_alpha", F(t0 + 2.6), 1.0, "SINE", "EASE_IN_OUT")
key(gl, "blend_alpha", F(t0 + 5.0), 0.45, "SINE", "EASE_IN_OUT")
text("LogoSub", 17, t0 + 2.4, t1 - 0.6, "11 HEROES   ·   ARENA   ·   CONQUEST", font_semi, 34, 0.5, 0.235, rgb=(0.96, 0.9, 0.78, 1.0), fade_in=15, fade_out=14)
text("LogoFine", 18, t0 + 3.2, t1 - 0.6, "A fan-made MOBA built with Paragon assets released by Epic Games", font_med, 20, 0.5, 0.165, rgb=(0.75, 0.75, 0.78, 1.0), fade_in=15, fade_out=14)

# ------------------------------------------------------------------ the kinetic type over the play
# pick your hero
punch_text("Pick", 1.0, 4.6, "PICK YOUR HERO", font_bold, 76, from_scale=1.12, glow=True, fade_out=12)
line("PickUnder", 21, 1.45, 4.6, 0, -58, 560, 3)
text("PickSub", 17, 1.9, 4.55, "11 HEROES  ·  ONE ARENA", font_semi, 28, 0.5, 0.405, rgb=(0.9, 0.9, 0.92, 1.0))
# the countdown on the drum hits, then FIGHT!
text("CountCap", 17, 4.85, 7.15, "THE MATCH BEGINS", font_semi, 28, 0.5, 0.765, rgb=(0.9, 0.9, 0.92, 1.0), fade_in=6, fade_out=6)
for k, (tc, num) in enumerate(((4.8, "3"), (5.4, "2"), (6.0, "1"))):
    punch_text(f"Count{num}", tc, tc + 0.6, num, font_black, 300, from_scale=1.7, glow=True, fade_out=6)
    flash(tc, 0.14, 4)
punch_text("Fight", 7.2, 8.9, "FIGHT!", font_black, 250, from_scale=1.9, glow=True, fade_out=10)
flash(7.2, 0.85, 10)
# lower thirds (a dark band behind them, drawn on)
lt = color("LT_Band", 12, F(9.8), F(11.95), (0.0, 0.0, 0.0))
lt.transform.origin = (0.5, 0.5)
lt.transform.offset_x, lt.transform.offset_y = (0.085 - 0.5) * W + 250, (0.215 - 0.5) * H
lt.transform.scale_y = 125 / H
key(lt.transform, "scale_x", F(9.8), 0.0005, "EXPO", "EASE_OUT")
key(lt.transform, "scale_x", F(10.2), 620 / W)
key(lt, "blend_alpha", F(9.8), 0.55)
key(lt, "blend_alpha", F(11.95) - 8, 0.55, "SINE", "EASE_IN")
key(lt, "blend_alpha", F(11.95) - 1, 0.0)
text("LT_Team", 17, 9.9, 11.9, "TEAM FIGHTS", font_bold, 42, 0.085, 0.24, anchor="LEFT")
line("LT_TeamLine", 21, 10.05, 11.9, (0.085 - 0.5) * W + 150, (0.24 - 0.5) * H - 32, 300, 3)
text("LT_Five", 18, 10.2, 11.9, "5 VS 5  ·  BOTS OR FRIENDS", font_semi, 22, 0.085, 0.19, anchor="LEFT", rgb=(0.88, 0.88, 0.9, 1.0))
# the power of Tartarus
text("TarCap", 17, 29.1, 31.6, "CLAIM THE", font_semi, 30, 0.5, 0.345, rgb=(0.92, 0.92, 0.94, 1.0))
for chn, suffix in ((16, ""), (19, "_glowsrc")):
    text("Tar" + suffix, chn, 29.25, 31.65, "POWER OF TARTARUS", font_black, 82, 0.5, 0.27)
tg = S.new_effect("Tar_glow", "GLOW", 20, F(29.25), length=F(31.65) - F(29.25), input1=S["Tar_glowsrc"])
tg.use_only_boost, tg.threshold, tg.boost_factor, tg.blur_radius, tg.quality = True, 0.3, 1.0, 12.0, 5
tg.blend_type = "ADD"
line("TarUnder", 21, 29.5, 31.65, 0, (0.225 - 0.5) * H, 620, 3)
# the hits
flash(28.8, 0.6, 9)
flash(31.8, 0.3, 6)
flash(51.0, 0.6, 9)
flash(16.8, 0.25, 6)
flash(21.6, 0.2, 5)
flash(36.6, 0.3, 6)
flash(41.4, 0.2, 5)

# ------------------------------------------------------------------ vignette (7), grade (8), bars (22), black (24), music (25)
vg = S.new_image("Vignette", os.path.join(ROOT, "vignette.png"), 7, 1, fit_method="FIT")
vg.frame_final_duration = F(END) - 1
vg.blend_type = "MULTIPLY"
adj = S.new_effect("Grade", "ADJUSTMENT", 8, 1, length=F(END) - 1)
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
BAR = 138
for name, sign in (("BarTop", 1), ("BarBot", -1)):
    b = color(name, 22, 1, F(END), (0, 0, 0))
    b.transform.scale_y = BAR / H
    b.transform.origin = (0.5, 0.5)
    key(b.transform, "offset_y", F(0.6), sign * (540 + BAR / 2), "SINE", "EASE_OUT")
    key(b.transform, "offset_y", F(1.8), sign * (540 - BAR / 2), "LINEAR")
blk = color("Black", 24, 1, F(END), (0, 0, 0))
key(blk, "blend_alpha", 1, 1.0)
key(blk, "blend_alpha", F(0.1), 1.0, "SINE", "EASE_IN_OUT")
key(blk, "blend_alpha", F(0.9), 0.0, "LINEAR")
key(blk, "blend_alpha", F(58.0), 0.0, "SINE", "EASE_IN")
key(blk, "blend_alpha", F(END) - 1, 1.0)
S.new_sound("Music", os.path.join(ROOT, MUSIC), 25, 1)
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
