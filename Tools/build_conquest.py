"""build_conquest.py — builds /Game/Maps/Conquest, the Conquest map (v17, operator: "bigger maps where the towers,
inhibitors, the core and the jungle are"). 250 x 160 m (the Arena is 140 x 92): three lanes, two towers and an
inhibitor per lane, the core on each base plateau, a jungle in four quadrants with the buff and minion camps, the
boss plaza in the north. Built from the same library packs and helpers as the Arena (build_arena.py).

Run headless (idempotent, the map is cleared and rebuilt):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path to this file>" -unattended -nosplash -nullrhi
Prints CONQUEST_MAP lines; the last one is CONQUEST_MAP OK path=... actors=N or CONQUEST_MAP FAIL <why>.

Layout (cm, mirrored in x and y; team A = Dawn at x < 0, team B = Dusk at x > 0):
  base plateaus   |x| 9000..12500, |y| < 3200, top +300; ramps to the mid lane and down to both side lanes
  mid lane        |y| < 650 from ramp foot to ramp foot (x 7700 .. -7700); a medallion plaza in the centre
  side lanes      up from the side ramps at |x| 10700 to the corner at |y| 7000, then across the map
  jungle          |x| 700..8600, |y| 1300..5700 per quadrant: walls along the lanes with entrances, camps, paths
  boss plaza      (0, 3900); an old ruin plaza at (0, -3900)
"""
import builtins
import math
import os
import random
import sys

import unreal

builtins.ARENA_NO_MAIN = True
sys.path.append(os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Tools"))
import build_arena as A  # noqa: E402

MAP = "/Game/Maps/Conquest"
X_BASE, X_END = 9000.0, 12500.0          # plateau front, map end
Y_PLAT, Y_EDGE = 3200.0, 8000.0          # plateau half-width, map half-height
SIDE_X, SIDE_Y = 10700.0, 7000.0         # side lane: vertical run at |x| SIDE_X, horizontal run at |y| SIDE_Y
LANE_HALF = 650.0
RAMP_RUN = 1300.0
rng = random.Random(17)
A.rng = rng
KITE = "/Game/KiteDemo/Environments/"
RUINS = "/Game/ParagonProps/Monolith/Ruins/Meshes/"


MONO_ROCKS = "/Game/ParagonProps/Monolith/Rocks/Meshes/"
AGORA = "/Game/ParagonProps/Agora/Props/Meshes/"
BIG_ROCKS = [MONO_ROCKS + f"SM_RockNordic_0{i}.SM_RockNordic_0{i}" for i in range(1, 9)]
SMALL_ROCKS = [MONO_ROCKS + f"SM_RockNordic_Small_0{i}.SM_RockNordic_Small_0{i}" for i in range(1, 5)] + \
    [KITE + "Rocks/Medium_Boulder_001/Medium_Boulder_001.Medium_Boulder_001"]
RUIN_PIECES = [RUINS + n + "." + n for n in ("JunglePillarBlockCrumble01_A", "JunglePillarBlock_01A", "JunglePillarBlock_02A",
                                             "MonoStatue_Damaged", "MonoStatueTorso_Damaged", "JunglePillarBlockPiece_CombinedA",
                                             "JunglePillarBlockPiece_CombinedB")] + \
    [AGORA + n + "." + n for n in ("RuinRemains1", "RuinRemains3", "RuinRemains5", "Carved_Rock_Symbol_A")]
FLOOR_BITS = [(KITE + "Rocks/GroundRevealRock001/SM_GroundRevealRock001.SM_GroundRevealRock001", 2.0, 3.5),
              (KITE + "Trees/Vegetation_Debris_002/SM_Vegetation_Debris_002.SM_Vegetation_Debris_002", 2.5, 4.0)]
RUBBLE = [RUINS + n + "." + n for n in ("JungleRubblePile_A", "JunglePillarBlockPiece_01A", "JunglePillarBlockPiece_02B",
                                        "JunglePillarBlockPiece_04A")] + SMALL_ROCKS[:2]


def log(msg):
    print(f"CONQUEST_MAP {msg}")


_logged = set()


def flat_at(path, x, y, width):
    """A flat floor piece (scree, a rock breaking through, fallen branches) of the given width, no collision."""
    m, o, e = A.mesh_info(path)
    if not m or not A.is_clear(x, y, 40.0):
        return
    s = width * 50.0 / max(e.x, e.y, 1.0)
    if path not in _logged:
        _logged.add(path)
        log(f"flat {path.split('.')[-1]} extent={e.x:.0f}x{e.y:.0f}x{e.z:.0f} scale={s:.2f}")
    A.inst(path, x, y, -8, yaw=rng.uniform(0, 360), scale=s, shadows=False, cull=6000.0)


def rock_at(path, x, y, height, yaw, taken, collide=True):
    """A rock, ruin or stump standing on the ground with its whole footprint clear of the lanes, paths and camp
    clearings (a big one is scaled down once, else left out); the footprint keeps ground cover out of it."""
    m, o, e = A.mesh_info(path)
    if not m:
        return False
    for _ in range(2):
        # the height asked for, but never wider than 2.2 x that height (a flat, wide ruin scaled by its height alone
        # came out 54 m across and lay over a side lane: its four corners happened to be outside the keep-outs)
        s = min(height * 50.0 / max(e.z, 1.0), height * 2.2 * 50.0 / max(e.x, e.y, 1.0))
        r = max(e.x, e.y) * s * 0.85
        probes = [(x + dx * r, y + dy * r) for dx in (-1, 0, 1) for dy in (-1, 0, 1)]
        if all(A.is_clear(px, py, 30.0) for px, py in probes) and not A.in_solid(x, y, 60.0):
            A.inst(path, x, y, -15, yaw=yaw, scale=s, collide=collide)
            A.SOLID.append((x - r * 0.9, x + r * 0.9, y - r * 0.9, y + r * 0.9))
            taken.append((x, y))
            return True
        height *= 0.7
    return False


# ---- collision: the ground, the plateaus, the ramps, the border -------------------------------------------------
def build_collision():
    T = 300.0
    A.block("Ground", -X_END, X_END, -Y_EDGE, Y_EDGE, -T, 0)
    for sx in (-1, 1):
        A.block(f"Plateau_{sx}", sx * X_BASE, sx * X_END, -Y_PLAT, Y_PLAT, -T, 300)
        top, foot = (sx * X_BASE, 0, 300), (sx * (X_BASE - RAMP_RUN), 0, 0)
        A.ramp(f"BaseRamp_Mid_{sx}", top, foot, 1400)
        A.ramp_walls(f"BaseRamp_Mid_{sx}", top, foot, 1400)
        for sy in (-1, 1):
            top, foot = (sx * SIDE_X, sy * Y_PLAT, 300), (sx * SIDE_X, sy * (Y_PLAT + RAMP_RUN), 0)
            A.ramp(f"BaseRamp_Lane_{sx}_{sy}", top, foot, 1400)
            A.ramp_walls(f"BaseRamp_Lane_{sx}_{sy}", top, foot, 1400)
    A.block("Border_N", -X_END - 400, X_END + 400, Y_EDGE, Y_EDGE + 300, -T, 2500, visible=False)
    A.block("Border_S", -X_END - 400, X_END + 400, -Y_EDGE - 300, -Y_EDGE, -T, 2500, visible=False)
    A.block("Border_E", X_END, X_END + 300, -Y_EDGE - 300, Y_EDGE + 300, -T, 2500, visible=False)
    A.block("Border_W", -X_END - 300, -X_END, -Y_EDGE - 300, Y_EDGE + 300, -T, 2500, visible=False)
    # keep-out for trees and rocks: lanes, plateaus with their ramps, the plazas, the jungle paths and camp clearings
    A.clear_rect(-X_BASE, X_BASE, -1150, 1150, mirror=False)                                   # mid lane + its walls
    A.clear_rect(-X_END, X_END, SIDE_Y - 1150, Y_EDGE, mirror=False)                           # side lanes
    A.clear_rect(-X_END, X_END, -Y_EDGE, -SIDE_Y + 1150, mirror=False)
    A.clear_rect(SIDE_X - 1300, X_END, Y_PLAT - 200, Y_EDGE)                                   # side lane climbs
    A.clear_rect(X_BASE - RAMP_RUN - 400, X_END, -Y_PLAT - 200, Y_PLAT + 200, mirror=False)   # plateaus
    A.clear_rect(-X_END, -X_BASE + RAMP_RUN + 400, -Y_PLAT - 200, Y_PLAT + 200, mirror=False)
    A.clear_rect(-1300, 1300, 2900, 4900, mirror=False)                                        # boss plaza
    A.clear_rect(-1300, 1300, -4900, -2900, mirror=False)                                      # south plaza
    for x0, x1 in ((1700, 2400), (5300, 6000)):                                               # N-S jungle paths
        A.clear_rect(x0, x1, 1150, SIDE_Y - 1150)
    A.clear_rect(700, 8600, 3500, 4300)                                                        # E-W jungle path
    for x, y in ((5600, 3900), (2050, 3000)):                                                  # camp clearings
        A.clear_rect(x - 900, x + 900, y - 900, y + 900)


# ---- floors -----------------------------------------------------------------------------------------------------
def build_floors():
    leafy = A.asset(KITE + "GroundTiles/LeafyPath/MI_Tile_LeafPathStones.MI_Tile_LeafPathStones")
    rocky = A.asset(KITE + "GroundTiles/RockyPath/MI_PSM_RockyPath_Tile.MI_PSM_RockyPath_Tile")
    stone = A.asset("/Game/ParagonProps/Monolith/Ruins/Materials/MI_Ruins_DecoFloor.MI_Ruins_DecoFloor")
    stone2 = A.asset("/Game/ParagonProps/Monolith/Ruins/Materials/MI_Ruins_BuffFloorB.MI_Ruins_BuffFloorB")
    ruins = A.asset("/Game/ParagonProps/Monolith/Ruins/Materials/MI_Ruins_BuffFloorA.MI_Ruins_BuffFloorA")
    # the jungle floor at ground level everywhere below the plateaus
    A.tiles(-X_BASE, X_BASE, -Y_EDGE, Y_EDGE, 0, leafy)
    for sx in (-1, 1):
        for sy in (-1, 1):
            A.tiles(sx * X_BASE, sx * X_END, sy * Y_PLAT, sy * Y_EDGE, 0, leafy)
        A.tiles(sx * X_BASE, sx * X_END, -Y_PLAT, Y_PLAT, 300, stone)                        # plateau
        A.ramp_tiles((sx * X_BASE, 0, 300), (sx * (X_BASE - RAMP_RUN), 0, 0), 1400, stone2)
        for sy in (-1, 1):
            A.ramp_tiles((sx * SIDE_X, sy * Y_PLAT, 300), (sx * SIDE_X, sy * (Y_PLAT + RAMP_RUN), 0), 1400, stone2)
            A.tiles(sx * (SIDE_X - LANE_HALF), sx * (SIDE_X + LANE_HALF), sy * (Y_PLAT + RAMP_RUN), sy * (SIDE_Y + LANE_HALF), 4, rocky)
        # the side lanes' horizontal runs (to the vertical run: no overlap, no z-fighting)
    for sy in (-1, 1):
        A.tiles(-(SIDE_X - LANE_HALF), SIDE_X - LANE_HALF, sy * (SIDE_Y - LANE_HALF), sy * (SIDE_Y + LANE_HALF), 4, rocky)
        A.tiles(-1100, 1100, sy * 3100, sy * 4700, 3, stone2 if sy > 0 else ruins)               # the plazas
        for x0, x1 in ((1750, 2350), (5350, 5950)):                                           # jungle paths
            for sx in (-1, 1):
                A.tiles(sx * x0, sx * x1, sy * 1200, sy * (SIDE_Y - 1200), 2, rocky)
        A.tiles(-8500, 8500, sy * 3650, sy * 4150, 2, rocky)
    A.tiles(-(X_BASE - RAMP_RUN), X_BASE - RAMP_RUN, -LANE_HALF, LANE_HALF, 4, rocky)       # mid lane
    A.place("MidMedallion", RUINS + "Ruins_DecoFloor.Ruins_DecoFloor", 0, 0, 5, size=(14.0, 14.0, 0.06))
    for x0, x1, y0, y1 in ((-60000, 60000, Y_EDGE, 60000), (-60000, 60000, -60000, -Y_EDGE), (-60000, -X_END, -Y_EDGE, Y_EDGE), (X_END, 60000, -Y_EDGE, Y_EDGE)):
        A.tiles(x0, x1, y0, y1, -40, leafy, size=10000.0)
    return rocky


# ---- bases ------------------------------------------------------------------------------------------------------
def build_bases():
    for team, sx in ((0, -1), (1, 1)):
        dawn = team == 0
        M = "/Game/ParagonProps/Monolith/"
        A.place(f"BaseFloor_{team}", M + ("Dawn/Meshes/Dawn_Inhibitor_Ring_B.Dawn_Inhibitor_Ring_B" if dawn else "Dusk/Meshes/SternInhibitorRing1.SternInhibitorRing1"),
                sx * 11500, 0, 300.5, size=(20.0, 20.0, 0.05))
        A.place(f"BaseGate_{team}", M + ("Dawn/Meshes/Dawn_Gate.Dawn_Gate" if dawn else "Dusk/Meshes/Evil_Gate_A.Evil_Gate_A"),
                sx * 12300, 0, 300, yaw=90.0, height=12.0, collide=True)
        wall = M + ("Dawn/Meshes/Dawn_Wall_Simple_Straight_A.Dawn_Wall_Simple_Straight_A" if dawn else "Dusk/Meshes/Evil_Gate_Wall_Str8_B.Evil_Gate_Wall_Str8_B")
        # plateau faces in the team's style: the front (either side of the mid ramp) and both sides (either side
        # of the lane ramp)
        for y0, y1 in ((-Y_PLAT, -780), (780, Y_PLAT)):
            A.place(f"BaseFace_Front_{team}_{y0}", wall, sx * (X_BASE - 52), (y0 + y1) / 2, -20, yaw=90.0, size=(abs(y1 - y0) / 100.0, 1.0, 3.4), collide=True)
        for sy in (-1, 1):
            for x0, x1 in ((X_BASE, SIDE_X - 780), (SIDE_X + 780, X_END)):
                A.place(f"BaseFace_Side_{team}_{sy}_{x0}", wall, sx * (x0 + x1) / 2, sy * (Y_PLAT + 52), -20, size=((x1 - x0) / 100.0, 1.0, 3.4), collide=True)
            A.place(f"BaseSconce_{team}_{sy}", M + ("Dawn/Meshes/DawnSide_Sconce.DawnSide_Sconce" if dawn else "Dusk/Meshes/SM_Dusk_WallA_Endcap_Firepit.SM_Dusk_WallA_Endcap_Firepit"),
                    sx * 12000, sy * 2700, 300, height=5.0, collide=True)
            A.point_light((sx * 12000, sy * 2700, 700), (0.35, 0.6, 1.0) if dawn else (1.0, 0.35, 0.12), 8000.0, 2200.0)
            A.place(f"BasePillar_{team}_{sy}", M + ("Dawn/Meshes/Pillar_Large.Pillar_Large" if dawn else "Dusk/Meshes/Evil_Inhibitor_Gate_Sconce_A.Evil_Inhibitor_Gate_Sconce_A"),
                    sx * 9500, sy * 2650, 300, height=5.5, collide=True)
            A.place(f"BaseTower_{team}_{sy}", M + ("Dawn/Meshes/Dawn_Tower.Dawn_Tower" if dawn else "Dusk/Meshes/Grim_Barbican_A.Grim_Barbican_A"),
                    sx * 13600, sy * 5200, -40, height=22.0)
        back = M + ("Dawn/Meshes/Dawn_Wall_Elaborate_Straight_A.Dawn_Wall_Elaborate_Straight_A" if dawn else "Dusk/Meshes/Dusk_WallA_Strait01.Dusk_WallA_Strait01")
        for i, y in enumerate(range(-7500, 7501, 1000)):
            if abs(y) < 900:
                continue
            A.place(f"BaseBackWall_{team}_{i}", back, sx * 12850, y, -40, yaw=90.0, size=(10.2, 3.0, 9.0))
        A.point_light((sx * 11500, 0, 900), (0.4, 0.65, 1.0) if dawn else (1.0, 0.3, 0.1), 9000.0, 2800.0)
        ps = A.spawn(unreal.PlayerStart, (sx * 11500, 0, 420), A.R(yaw=0.0 if dawn else 180.0))
        ps.set_editor_property("player_start_tag", "BaseA" if dawn else "BaseB")


# ---- lanes and the jungle's walls ---------------------------------------------------------------------------------
def wall_run(label, x0, x1, y, gaps, height=3.0):
    """ruined walls along a lane edge from x0 to x1 at y, with openings (x centres, 10 m wide) into the jungle"""
    seg = 800.0
    x = x0
    i = 0
    while x + seg <= x1 + 1:
        cx = x + seg / 2
        if not any(abs(cx - g) < 900 for g in gaps):
            A.place(f"{label}_{i}", RUINS + ("JungleWall_02A.JungleWall_02A" if i % 2 else "JungleWall_02B.JungleWall_02B"),
                    cx, y, -5, yaw=0.0 if y > 0 else 180.0, size=(8.1, 1.0, height), collide=True)
        x += seg
        i += 1


def build_lanes_and_walls():
    for sy in (-1, 1):
        # the mid lane's edges: entrances to the jungle paths and near the towers
        wall_run(f"MidWall_{sy}", -7600, 7600, sy * 1150, gaps=(-6100, -2050, 2050, 6100, 0))
        # the side lane's inner edge: entrances at the jungle paths and the plazas
        wall_run(f"SideWall_{sy}", -9600, 9600, sy * (SIDE_Y - 1150), gaps=(-5650, -2050, 0, 2050, 5650))
    # cover along the lanes: boulders and rock arches over the side lanes, broken walls at the mid lane
    for sx in (-1, 1):
        for sy in (-1, 1):
            A.place(f"LaneArch_{sx}_{sy}", "/Game/ParagonProps/Agora/Props/Meshes/Rock_Formation_Strip_C_Arch_A.Rock_Formation_Strip_C_Arch_A",
                    sx * 6000, sy * SIDE_Y, -20, yaw=90.0, size=(14.0, 1.4, 9.0), collide=True)
            A.place(f"LaneRock_{sx}_{sy}", KITE + "Rocks/Large_Volcanic_Rock_001/LargeVolcanicRock_001.LargeVolcanicRock_001",
                    sx * 3200, sy * (SIDE_Y + 400), -30, yaw=rng.uniform(0, 360), size=(2.4, 3.0, 1.6), collide=True)
            A.place(f"MidCover_{sx}_{sy}", RUINS + "Aclove_Wall_Broken.Aclove_Wall_Broken", sx * 4200, sy * 420, 0, yaw=90.0 * (sy + 1),
                    size=(2.6, 2.6, 1.5), collide=True)
        A.place(f"CornerMonolith_N_{sx}", "/Game/ParagonProps/Agora/Props/Meshes/Tri_Rock_1.Tri_Rock_1", sx * (SIDE_X + 1100), Y_EDGE - 600, -300, yaw=rng.uniform(0, 360), height=24.0)
        A.place(f"CornerMonolith_S_{sx}", "/Game/ParagonProps/Agora/Props/Meshes/Tri_Rock_1.Tri_Rock_1", sx * (SIDE_X + 1100), -Y_EDGE + 600, -300, yaw=rng.uniform(0, 360), height=24.0)


def build_plazas():
    # the boss plaza (north): statues, braziers and a ring; the south plaza: a ruin with pillars
    for sy, name in ((1, "Boss"), (-1, "Ruin")):
        cy = sy * 3900
        A.place(f"{name}Medallion", RUINS + "Ruins_DecoFloor.Ruins_DecoFloor", 0, cy, 4, size=(11.0, 11.0, 0.06))
        for (x, dy) in ((-1000, -700), (1000, -700), (-1000, 700), (1000, 700)):
            A.place(f"{name}Statue_{x}_{dy}", RUINS + "MonoStatue.MonoStatue", x, cy + dy, 0, yaw=45.0 if x * dy > 0 else -45.0, height=5.0, collide=True)
        for x in (-1250, 1250):
            A.place(f"{name}Brazier_{x}", "/Game/ParagonProps/Monolith/Dusk/Meshes/SM_Dusk_WallA_Endcap_Firepit.SM_Dusk_WallA_Endcap_Firepit", x, cy, 0, height=2.6, collide=True)
            A.point_light((x, cy, 330), (1.0, 0.5, 0.18) if sy > 0 else (0.5, 0.7, 1.0), 6000.0, 1600.0)
        if sy > 0:
            A.place("BossRing", "/Game/ParagonProps/Monolith/Dawn/Meshes/Dawn_Core_Ring_A.Dawn_Core_Ring_A", 0, cy, 6, size=(9.0, 9.0, 0.02))
        else:
            for i, x in enumerate((-500, 500)):
                A.place(f"RuinPillar_{i}", RUINS + "JunglePillarBlockCrumble01_A.JunglePillarBlockCrumble01_A", x, cy + 150, 0, yaw=rng.uniform(0, 90), size=(2.0, 2.0, 2.2), collide=True)


def build_jungle():
    trees = [(KITE + "Trees/HillTree_Tall_02/HillTree_Tall_02.HillTree_Tall_02", 17.0, 23.0),
             (KITE + "Trees/ScotsPineTall_01/ScotsPineTall_01.ScotsPineTall_01", 16.0, 22.0)]
    taken = []
    quad = [(700, 8600, 1300, 5700)]
    # (v18, operator: "more natural; the excess vegetation may become rocks and objects from the Paragon pack") —
    # groves with clearings between them instead of an even stand of 200 trees, rock outcrops and old ruins in the
    # clearings; everything mirrored (a fair map), clear of the lanes, paths and camp clearings
    centres = A.scatter_points(quad, 60, 1150, [], pad=200.0)            # 15 a quadrant, mirrored
    grove, outcrop, ruin = centres[:24], centres[24:44], centres[44:]
    for i in range(0, len(grove), 4):
        cx, cy = grove[i]
        members = [(rng.uniform(-480, 480), rng.uniform(-480, 480)) for _ in range(rng.randint(3, 6))]
        picks = [rng.choice(trees) for _ in members]
        hs = [rng.uniform(p[1], p[2]) for p in picks]
        yaws = [rng.uniform(0, 360) for _ in members]
        for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
            for (dx, dy), p, h, yw in zip(members, picks, hs, yaws):
                x, y = sx * abs(cx) + sx * dx, sy * abs(cy) + sy * dy
                if not A.is_clear(x, y, 60.0) or A.in_solid(x, y, 150.0) or any(math.hypot(x - tx, y - ty) < 300 for tx, ty in taken):
                    continue
                A.inst(p[0], x, y, -10, yaw=yw, height=h, collide=True)
                taken.append((x, y))
    for i in range(0, len(outcrop), 4):
        cx, cy = outcrop[i]
        big = rng.choice(BIG_ROCKS)
        bh, byaw = rng.uniform(2.8, 4.6), rng.uniform(0, 360)
        smalls = [(rng.choice(SMALL_ROCKS), rng.uniform(-420, 420), rng.uniform(-420, 420), rng.uniform(0.9, 1.7), rng.uniform(0, 360)) for _ in range(rng.randint(2, 4))]
        for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
            x, y = sx * abs(cx), sy * abs(cy)
            rock_at(big, x, y, bh, byaw * sx * sy, taken)
            for p, dx, dy, h, yw in smalls:
                rock_at(p, x + sx * dx, y + sy * dy, h, yw, taken)
    for i in range(0, len(ruin), 4):
        cx, cy = ruin[i]
        piece = rng.choice(RUIN_PIECES)
        ph, pyaw = rng.uniform(2.2, 3.6), rng.uniform(0, 360)
        rubble = [(rng.choice(RUBBLE), rng.uniform(-380, 380), rng.uniform(-380, 380), rng.uniform(0.6, 1.2), rng.uniform(0, 360)) for _ in range(rng.randint(2, 3))]
        for sx, sy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
            x, y = sx * abs(cx), sy * abs(cy)
            rock_at(piece, x, y, ph, pyaw * sx * sy, taken)
            for p, dx, dy, h, yw in rubble:
                rock_at(p, x + sx * dx, y + sy * dy, h, yw, taken)
    # behind the plateaus: rock shoulders and a few trees instead of a thin even row (in play: collision)
    edge = [(X_BASE + 200, X_END - 200, Y_PLAT + 400, SIDE_Y - 1400)]
    for j, (x, y) in enumerate(A.scatter_points(edge, 16, 800, taken, pad=60.0)):
        if j % 2 == 0:
            rock_at(rng.choice(BIG_ROCKS), x, y, rng.uniform(3.0, 5.0), rng.uniform(0, 360), taken)
        else:
            path, h0, h1 = rng.choice(trees)
            A.inst(path, x, y, -10, yaw=rng.uniform(0, 360), height=rng.uniform(h0, h1), collide=True)
    stumps = [(KITE + "Trees/Tree_Stump_01/Tree_Stump_01.Tree_Stump_01", 1.6, 2.4),
              ("/Game/ParagonProps/Monolith/Rocks/Meshes/SM_RockNordic_Small_02.SM_RockNordic_Small_02", 1.0, 1.5)]
    for x, y in A.scatter_points(quad, 28, 600, taken, pad=60.0):
        path, h0, h1 = rng.choice(stumps)
        rock_at(path, x, y, rng.uniform(h0, h1), rng.uniform(0, 360), taken)
    # the clearings' floor: flat rocks breaking through, fallen branches (the KiteDemo scree meshes came out as
    # 30 m yellow discs over the jungle: their bounds do not match what they draw — not used)
    # (visual only: nobody trips on them, the ground cover may grow over them)
    for x, y in A.scatter_points(quad, 48, 450, taken, pad=80.0):
        path, w0, w1 = rng.choice(FLOOR_BITS)
        flat_at(path, x, y, rng.uniform(w0, w1))
    log(f"jungle groves={len(grove) // 4 * 4} outcrops={len(outcrop)} ruins={len(ruin)}")
    # the forest beyond the border (visual only)
    for x in range(-20000, 20001, 800):
        for y in range(int(Y_EDGE) + 700, 16001, 750):
            for sy in (-1, 1):
                if rng.random() < 0.75:
                    path, h0, h1 = rng.choice(trees)
                    A.inst(path, x + rng.uniform(-300, 300), sy * (y + rng.uniform(-250, 250)), -40, yaw=rng.uniform(0, 360), height=rng.uniform(h0 + 2, h1 + 6), shadows=False)
    for x in range(int(X_END) + 900, 20001, 750):
        for y in range(-int(Y_EDGE), int(Y_EDGE) + 1, 800):
            for sx in (-1, 1):
                if rng.random() < 0.75:
                    path, h0, h1 = rng.choice(trees)
                    A.inst(path, sx * (x + rng.uniform(-250, 250)), y + rng.uniform(-300, 300), -40, yaw=rng.uniform(0, 360), height=rng.uniform(h0 + 2, h1 + 6), shadows=False)
    for i, x in enumerate(range(-12000, 12001, 2000)):
        for sy in (-1, 1):
            A.place(f"BorderCliff_{i}_{sy}", KITE + "Cliffs/Cliff01/SM_Cliff01.SM_Cliff01", x, sy * (Y_EDGE + 550), -300, yaw=180.0 if sy > 0 else 0.0, size=(21.0, 7.0, 13.0))
    # ground cover in the jungle (visual only), kept off lanes, paths, plazas and plateaus
    def ground_ok(x, y):
        if A.on_ramp(x, y, 60.0) or A.in_solid(x, y, 40.0):
            return False
        if abs(y) < 1180 or abs(y) > SIDE_Y - 1180:
            return False
        if abs(x) > X_BASE - 100:
            return False
        if abs(x) < 1350 and 2850 < abs(y) < 4950:
            return False
        if 1700 < abs(x) < 2400 or 5300 < abs(x) < 6000 or 3600 < abs(y) < 4200:
            return False
        return True
    grass = KITE + "Foliage/Grass/FieldGrass/SM_FieldGrass_01.SM_FieldGrass_01"
    cover = [(KITE + "Foliage/Ferns/SM_Fern_01.SM_Fern_01", 900, 0.7, 1.3, 6000.0, False),
             (KITE + "Foliage/Ferns/SM_Fern_02.SM_Fern_02", 700, 0.7, 1.3, 6000.0, False),
             (KITE + "Foliage/Flowers/Heather/SM_Heather_Mesh_Clumps2.SM_Heather_Mesh_Clumps2", 700, 0.8, 1.3, 6000.0, False),
             (KITE + "Foliage/BogMyrtleBush_01/BogMyrtleBush_01.BogMyrtleBush_01", 350, 0.7, 1.1, 8000.0, True),
             (KITE + "Foliage/Leaves/SM_DeadLeaves_Flat.SM_DeadLeaves_Flat", 1600, 0.8, 1.6, 4000.0, False),
             (KITE + "Rocks/River_Rock_01/SM_River_Rock_01.SM_River_Rock_01", 400, 0.6, 1.4, 4500.0, False)]
    for path, n, s0, s1, cull, shadows in cover:
        placed = 0
        while placed < n:
            x, y = rng.uniform(-X_BASE, X_BASE), rng.uniform(-Y_EDGE + 200, Y_EDGE - 200)
            if not ground_ok(x, y):
                continue
            A.inst(path, x, y, -2, yaw=rng.uniform(0, 360), scale=rng.uniform(s0, s1), shadows=shadows, cull=cull)
            placed += 1
    clumps = 0
    while clumps < 1600:
        cx, cy = rng.uniform(-X_BASE, X_BASE), rng.uniform(-Y_EDGE + 200, Y_EDGE - 200)
        if not ground_ok(cx, cy):
            continue
        for _ in range(rng.randint(5, 9)):
            x, y = cx + rng.gauss(0, 75), cy + rng.gauss(0, 75)
            if ground_ok(x, y):
                A.inst(grass, x, y, -2, yaw=rng.uniform(0, 360), scale=rng.uniform(1.0, 1.7), shadows=False, cull=5500.0)
        clumps += 1
    log(f"jungle trees_and_rocks={len(taken)} grass_clumps={clumps}")


def build_edges():
    """grass, ferns, leaves and pebbles over the seams of the lanes and paths (visual only)"""
    grass = KITE + "Foliage/Grass/FieldGrass/SM_FieldGrass_01.SM_FieldGrass_01"
    fern = KITE + "Foliage/Ferns/SM_Fern_03.SM_Fern_03"
    pebble = KITE + "Rocks/River_Rock_01/SM_River_Rock_01.SM_River_Rock_01"
    n = 0

    rejected = {"ramp": 0, "solid": 0}

    def tuft(x, y):
        nonlocal n
        if A.on_ramp(x, y, 20.0):
            rejected["ramp"] += 1
            return
        if A.in_solid(x, y, 30.0):
            rejected["solid"] += 1
            hit = [r for r in A.SOLID if r[0] - 30 <= x <= r[1] + 30 and r[2] - 30 <= y <= r[3] + 30]
            if rejected["solid"] <= 3:
                log(f"edge tuft in solid at ({x:.0f},{y:.0f}): {hit[:2]}")
            return
        if math.hypot(x, y) < 760.0:
            return   # under the mid medallion's rim (the foliage check found them buried)
        r = rng.random()
        path = grass if r < 0.55 else (fern if r < 0.8 else pebble)
        A.inst(path, x, y, -2, yaw=rng.uniform(0, 360), scale=rng.uniform(0.8, 1.4), shadows=False, cull=5000.0)
        n += 1
    for sy in (-1, 1):
        for _ in range(900):
            tuft(rng.uniform(-(X_BASE - RAMP_RUN), X_BASE - RAMP_RUN), sy * rng.uniform(LANE_HALF - 30, LANE_HALF + 120))
            tuft(rng.uniform(-(SIDE_X - LANE_HALF), SIDE_X - LANE_HALF), sy * rng.uniform(SIDE_Y - LANE_HALF - 120, SIDE_Y - LANE_HALF + 30))
            tuft(rng.uniform(-(SIDE_X - LANE_HALF), SIDE_X - LANE_HALF), sy * rng.uniform(SIDE_Y + LANE_HALF - 30, SIDE_Y + LANE_HALF + 120))
    log(f"edges={n} rejected={rejected}")


# ---- the horizon --------------------------------------------------------------------------------------------------
def build_horizon():
    """A ring of Agora's rock formations (the original map's own border rocks) just past the forest: the flat ground
    beyond it showed to the horizon from the overview and hero-select cameras. The Arena's skyline sat inside this
    map's forest; the landmarks stand past the ring behind each base instead. Visual only, no shadows."""
    # (v17 review: Agora strips stretched to 140 m stood as a flat streaked wall over the lanes; the shared cliff ring)
    A.build_horizon(33000.0, 28000.0, 40, 48.0, 72.0)
    for label, path, x, y, h, yaw in (
            ("Skyline_Dusk", "/Game/ParagonProps/Monolith/Dusk/Meshes/Grim_Barbican_A.Grim_Barbican_A", 50000, 6000, 110.0, 90.0),
            ("Skyline_Dawn", "/Game/ParagonProps/Monolith/Dawn/Meshes/Dawn_TermTower1.Dawn_TermTower1", -48000, -6000, 130.0, 90.0)):
        A.place(label, path, x, y, -2500, yaw=yaw, height=h)
    for a in A.actors.get_all_level_actors():
        if a.get_actor_label().startswith("Horizon_") or a.get_actor_label().startswith("Skyline_"):
            c = a.static_mesh_component
            c.set_editor_property("cast_shadow", False)
            c.set_collision_profile_name("NoCollision")


# ---- gameplay actors ----------------------------------------------------------------------------------------------
def build_gameplay_actors():
    lanes = {"Mid": [(-(X_BASE - RAMP_RUN) + 300, 0, 20), (-3700, 0, 20), (0, 0, 20), (3700, 0, 20), ((X_BASE - RAMP_RUN) - 300, 0, 20)],
             "North": [(-SIDE_X, Y_PLAT + RAMP_RUN + 300, 20), (-SIDE_X + 300, SIDE_Y - 100, 20), (-6000, SIDE_Y, 20), (0, SIDE_Y, 20),
                       (6000, SIDE_Y, 20), (SIDE_X - 300, SIDE_Y - 100, 20), (SIDE_X, Y_PLAT + RAMP_RUN + 300, 20)],
             "South": [(-SIDE_X, -(Y_PLAT + RAMP_RUN + 300), 20), (-SIDE_X + 300, -(SIDE_Y - 100), 20), (-6000, -SIDE_Y, 20), (0, -SIDE_Y, 20),
                       (6000, -SIDE_Y, 20), (SIDE_X - 300, -(SIDE_Y - 100), 20), (SIDE_X, -(Y_PLAT + RAMP_RUN + 300), 20)]}
    for name, pts in lanes.items():
        for i, p in enumerate(pts):
            t = A.spawn(unreal.TargetPoint, p, label=f"Lane_{name}_{i:02d}")
            t.set_editor_property("tags", [f"Lane_{name}_{i:02d}"])
    A.camera("SelectCam", (0, -17000, 9000), (0, 0, -200), ["SelectCam"], fov=75.0)
    tour = [((-16000, -15000, 11000), (0, 0, 0), 70.0), ((-9000, 0, 1400), (0, 0, 0), 85.0), ((0, 2000, 1200), (0, 3900, 0), 85.0),
            ((-8000, 7000, 600), (0, 7000, 60), 85.0), ((-5200, 1500, 420), (-6600, 4800, 120), 80.0),
            ((2600, -6100, 450), (5600, -3900, 100), 80.0), ((0, 0, 26000), (1, 0, 0), 85.0)]
    for i, (frm, to, fov) in enumerate(tour):
        c = A.camera(f"Tour_{i:02d}", frm, to, ["Tour", f"Tour_{i:02d}"], fov)
        if i == len(tour) - 1:
            c.set_actor_rotation(A.R(yaw=0.0, pitch=-90.0), False)
    nav = A.spawn(unreal.NavMeshBoundsVolume, (0, 0, 250), label="NavBounds")
    nav.set_actor_scale3d(A.V((X_END * 2 + 400) / 200.0, (Y_EDGE * 2 + 400) / 200.0, 8.0))
    for a in A.actors.get_all_level_actors():
        if isinstance(a, unreal.RecastNavMesh):
            try:
                a.set_editor_property("agent_radius", 42.0)
            except Exception as ex:  # noqa: BLE001
                log(f"WARN navmesh radius: {ex}")


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        if not A.levels.load_level(MAP):
            log("FAIL load_level")
            return
        for a in A.actors.get_all_level_actors():
            if not isinstance(a, unreal.WorldSettings) and (type(a) is not unreal.Brush or isinstance(a, unreal.Volume)):
                A.actors.destroy_actor(a)
    else:
        try:
            ok = A.levels.new_level(MAP, False)
        except TypeError:
            ok = A.levels.new_level(MAP)
        if not ok:
            log("FAIL new_level")
            return
    A.BLOCK_MAT = A.color_mat("MI_Stone", (0.16, 0.13, 0.15))
    A.CLIFF_MAT = A.asset(f"{A.MAT_DIR}/M_ArenaCliff.M_ArenaCliff") or A.BLOCK_MAT
    build_collision()
    build_floors()
    build_bases()
    build_lanes_and_walls()
    build_plazas()
    build_jungle()
    build_edges()
    A.build_skirts()
    build_horizon()
    A.build_lighting()
    build_gameplay_actors()
    A.flush_scatter()
    if not A.levels.save_current_level():
        log("FAIL save")
        return
    log(f"OK path={MAP} actors={A.count['n']}")


main()
