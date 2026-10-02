# 08 — QA: v4 map holes, ability targeting, combat and contact

Scope (operator request of 2026-09-27): close every hole and gap in the map; show each ability's range and area
before it is used, like League of Legends or Smite; improve the code behind mechanics, combat and physics; better
collision and contact between units in a melee; verify while improving; build a new exe.
Every line is reproducible from the repository root; the logs were kept outside the repository.

## Evidence

| Check | Command | Result |
|---|---|---|
| Compile | `Build.bat ParagonArenaEditor Win64 Development ... -NoHotReloadFromIDE` | Succeeded, 0 errors, 0 warnings in `Source/` |
| Materials | `-run=pythonscript -script=Tools/make_materials.py` | `MATERIALS OK` (M_ArenaCliff, M_ArenaIndicator) |
| Map | `-run=pythonscript -script=Tools/build_arena.py` | `ARENA_MAP OK actors=339` |
| Map review at the former holes | `UnrealEditor <uproject> -game -ArenaCamShots=Saved/cams_holes.txt` | 11 `CAM_*` screenshots + 3 re-checks: ramp sides, ridge ends, pit ramp walls, the border edge and overviews show rock or dressing, no see-through gap |
| Unit/spec tests | `-ExecCmds="Automation RunTests Arena;Quit" -nullrhi` | 22/22 Success (new: a melee bot closes to body contact) |
| Mechanics lab | `UnrealEditor <uproject> -game -ArenaMechLab` | `LAB_SUMMARY fails=0`, 35 PASS; screenshots `MECH_Slot1..4`, `MECH_Walls`, `MECH_DashWall`, `MECH_Contact` |
| Animation lab (regression) | `-game -nullrhi -ArenaAnimLab` | 28/28 PASS twice |
| Headless 5v5, 5 min, seeds 7 / 12 / 21 (final code) | `-game -nullrhi -benchmark -fps=60 -ArenaBotMatch -Seed=N -Minutes=5` | `stuck=0` on all three, 104–108 dashes per match, `all4=10` (logs `reg7_bot_*.log`) |
| Headless 5v5, seeds 21 / 44 (5 min) and 33 (10 min) | same | `stuck=0` on all three (logs `reg6_bot_*.log`) |
| Rendered match with aiming | `-game -ArenaUIDemo -Seed=7 -Minutes=2.5` | `UI_07_Aim`: the ultimate's area on the ground, its name and the controls under the crosshair; 121 FPS average, 77 FPS 1 % low |

What the mechanics lab measures:

| Check | Measured |
|---|---|
| indicator of every ability of every hero (20) matches the data | cones = range, rings = range, discs = radius, lanes ≤ range and stopped by walls, dash paths ≤ distance |
| a ground area lands where its indicator showed | 0 cm off (Rift, Black Hole, Annihilation, Rain of Arrows, Wrath of Heaven) |
| a dash ends where its indicator showed | 45–54 cm off (all six dashes, including Sparrow's backwards leap) |
| a swing through a wall | does not land (target 470/470) |
| the same swing without the wall | lands (target 420/470) |
| a 9 m dash at a wall 4 m ahead | stops with the body front at the wall face, no overlap |
| a melee bot against a standing target | 5 swings at 82–120 cm centre distance; the bodies touch at 76 cm |

## Defects found and fixed in this pass

| Defect (how it was seen) | Fix |
|---|---|
| Holes: the terrain boxes were invisible, so under every ramp, beside the pit ramps and at the ridge ends one looked through into the void | terrain boxes rendered with a world-aligned rock (M_ArenaCliff); ramps are thick earthworks; ramps have sloped stone parapets; only the tall border and the orb altar stay invisible |
| Flicker where a wall mesh lay exactly on a terrain face (base front walls, pit edges) | walls moved off the faces; rails that sat inside the new parapets removed |
| Abilities fired with no preview | `AArenaIndicator`: range ring, area disc, cone, lane and dash path as ground decals, in the ability's colour (red while it cannot be cast); 1–4 aims, LMB or the same key casts, RMB / Esc cancels; ground AoE telegraphs are decals too |
| Hits went through walls | line of sight (walls, rocks, trees) for every area, cone and blast |
| Hits used the target's centre and 3D distance (a unit on the ridge above was hit by a swing in the pit; a unit pressed to your side was outside the swing) | the target's capsule edge counts, cone widened by its width, a vertical window of one level, the cone starts at the attacker |
| Dashes slid the capsule in a straight line (stopped by any ramp, flew off ledges) | the dash is a root-motion move-to: full collision, follows ramps; its end is found along the navmesh (walls and ledges end it) and the indicator shows exactly that point |
| A dash began and ended in the same frame (the new root-motion source waits one move) | the dash ends by its clock |
| Melee bots swung from 2.5 m and hovered 1 m away; after knocking a target 2 m away they stood idle (the move counted as reached) | melee bots close to body contact (115 cm centre), swing from ≤ 150 cm, circle the target; contact moves use a small acceptance without the capsule-overlap bonus |
| Enemies dodged each other through crowd avoidance, so a fight never reached contact | avoidance only between allies |
| Every hero had the same 40 cm capsule | per-hero widths (Greystone 44, Kwang 42, Gideon 38, Countess 36, Sparrow 34); melee reaches set to the weapons (2.3–2.6 m) |
| A hero was pinned behind a crate or pushed it depending on the frame rate | a unit shoves a loose prop along the ground at its walking pace (no lift, frame-rate independent) |
| Stuck metric counted bots that stood still on purpose and then got a new goal | the clock runs only while the bot is trying to walk |

## Verdict: **Pass (T1 + T2)**

Measured checks (T1: 22/22 specs, 35/35 mechanics lab, 28/28 animation lab, 0 stuck in six matches); map, indicators
and aiming verified from screenshots (T2). How the targeting feels and whether the map reads well are the
operator's call (T3).

Shipping package: `RunUAT BuildCookRun ... -clientconfig=Shipping -iostore -compressed -IgnoreCookErrors` → `BUILD SUCCESSFUL`, exit 0,
`Build/Windows/ParagonArena.exe`. The packaged exe ran `-ArenaMechLab` (7 `MECH_*` screenshots: the indicators, the walls,
the dash stop, the melee contact) and `-ArenaUIDemo` (`UI_07_Aim`: aiming the ultimate while it recharges — red area,
red hint), both exit 0; screenshots in `%LOCALAPPDATA%ParagonArenaSavedScreenshotsWindows`.
