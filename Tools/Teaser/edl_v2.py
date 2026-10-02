# edl_v2.py — the second cut (operator 2026-10-01: "the camera was a bit too high, fix the 5v5 fights and use parts of
# my two recordings"): the 5v5 fights re-filmed low and close (fight_e, fight_f), the operator's own play (u1_*, u2_*:
# their screen recordings, cropped 1.3x so the HUD sits under the bars) cut between the director's takes.
# Same soundtrack and bars: the hits at 7 / 17 / 29 / 47 s, the montage 43-47 s.
OUT_DIR, STILLS_DIR = "edit_v2", "stills_v2"
SHOTS = [
    # the arena from above, in from black
    (1.0, 7.0, "aerial", 0.0, 1.0, {"push": (1.0, 1.06)}),
    # the heroes
    (9.0, 11.0, "hero_c", 1.0, 1.0, {"push": (1.0, 1.06)}),
    (11.0, 13.0, "u2_beast", 1.0, 1.0, {"punch": 11.5, "push": (1.0, 1.04)}),
    (13.0, 15.0, "hero_b", 2.0, 1.0, {"punch": 14.0, "push": (1.0, 1.04)}),
    (15.0, 17.0, "u2_beast2", 0.6, 1.0, {"punch": 15.9, "push": (1.0, 1.04)}),
    # 5 vs 5: re-filmed at 9.5 m, 2 m above the fight (7.6 m / 1.2 m was too close: heroes walked into the lens)
    (19.0, 21.5, "fight_e", 5.0, 1.0, {"push": (1.0, 1.05)}),
    (21.5, 23.0, "u1_dkill", 1.4, 1.0, {"push": (1.04, 1.08)}),   # (u1_wraith: FIRST BLOOD banner too low; u2_bolt2: a tooltip, then a spinning camera)
    (23.0, 25.0, "fight_g", 0.6, 1.0, {"punch": 23.4, "push": (1.0, 1.04)}),
    (25.0, 26.5, "u1_purple", 0.2, 1.0, {"punch": 25.3, "push": (1.08, 1.1)}),   # 1.08: the FIRST BLOOD banner under the bar
    (26.5, 28.0, "fight_h", 2.6, 1.0, {"push": (1.0, 1.05)}),
    (28.0, 29.0, "follow_b", 5.8, 1.0, {"punch": 28.2, "push": (1.0, 1.03)}),
    # Conquest
    (31.0, 34.0, "aerial_c", 0.0, 1.5, {"push": (1.0, 1.05)}),
    (34.0, 35.5, "u2_beam", 0.1, 1.0, {"punch": 34.4, "push": (1.0, 1.04)}),
    (35.5, 37.0, "fight_c", 2.8, 1.0, {"push": (1.0, 1.06)}),   # (tower_b at 6.3 framed the rocks)
    (37.0, 39.0, "fight_d", 2.9, 1.0, {"punch": 38.05, "push": (1.0, 1.04)}),
    (39.0, 43.0, "fight_c", 6.8, 1.0, {"punch": 41.5, "push": (1.0, 1.06)}),   # (u2_laser: a tutorial hint over the picture)
]
MONTAGE = [("u2_bolt2", 1.2), ("fight_e", 10.2), ("u2_bolt", 1.3), ("hero_c", 3.0),
           ("fight_g", 4.6), ("u1_fire", 2.4), ("fight_f", 9.0), ("u1_cube", 0.8)]
CARDS = [
    (7.0, 9.0, "heroes", "u1_roster", 0.0, "GREYSTONE  ·  COUNTESS  ·  GIDEON  ·  SPARROW  ·  KWANG  ·  CRUNCH  ·  IGGY & SCORCH  ·  KHAIMERA  ·  MORIGESH  ·  REVENANT  ·  SEVAROG"),
    (17.0, 19.0, "5v5", "u1_lineup", 0.0, "TEAM FIGHTS   ·   SMART BOTS   ·   LAN PLAY"),
    (29.0, 31.0, "conquest", "aerial_c", 6.0, "PUSH THE LANES   ·   TAKE THE TOWERS   ·   DESTROY THE CORE"),
    (47.0, END, "logo", "hero_c", 5.0, None),
]
