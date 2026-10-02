# 13 — QA: v8 animation smoothness, bodies that lie and fade, turning, camera, Blender environment pilot

Scope (operator request of 2026-09-27): smoother animations everywhere; dead bodies, minions first, lying naturally
and fading away; the camera fully reworked including while moving; the surroundings improved in Blender where it
makes sense, without extra cost; report the state of everything.

## What changed for the player

| Area | Before | Now |
|---|---|---|
| Death, hero | the pack fall cut in on one frame (a pose pop), the body sank 1.3 m into the floor | the fall blends in from the pose of the moment (0.18 s), the body turns to the killer over a few frames, lies 2.5 s, then fades away in 1.5 s (a fine dither, eased at both ends) |
| Death, minion | the mannequin "death" clips end on their feet (the template ragdolls after them): minions stayed bent over, then sank | a 0.35 s stagger (a montage blended by the engine), then the body goes limp (ragdoll: ground and walls only, never pushes a unit or a prop, slow depenetration), lies, fades in 1 s |
| Drop-in, victory | pose pop at the start | the same blend-in (UArenaPoseBlendInstance) |
| Bots and minions | faced every new target in one frame (yaw snaps) | turn at 720 °/s (heroes) / 540 °/s (minions) toward their focus |
| Camera | trailed by speed only (a dash left it behind), per-frame random shake, snapped back after passing a pillar | trails softly but never more than 1.3 m, sub-stepped (same at 40 and 140 fps), Perlin shake, +6° FOV during a dash, in at once at a wall and gliding back out after it |
| Environment | the tall ramp parapets met the ground in a ruler-straight seam | piles of broken rock at their outer feet (own procedural meshes from Blender, world-aligned rock material) |
| First wave | a 1.1 s stall while the minion meshes and anim blueprints loaded | preloaded with everything heroes.json names (meshes, portraits, anim classes, fade materials) |

## Findings on the way (bugs found and fixed)

1. **Minions never lay down.** The earlier check `pelvis_above_floor=25` passed only because the body was already
   sinking at the moment of the check. Sampling the six `MM_Death_*` clips: the pelvis ends at 83–88 cm — they are
   stagger clips made to be followed by a ragdoll. Fixed with the stagger → ragdoll; now 12 cm.
2. **Paragon's own `FadeOut` parameter** (every hero material has it, with a `FadeMask`) turns the body into a black
   silhouette in a masked copy — not a fade. Replaced by our own dither parameter `ArenaFade`.
3. **Localized pin names**: in the Polish editor the Python material API sees `Atrybut`, not `MaterialAttributes`,
   and an empty output name picks a function's first output (not `Result`): the first fade copies drew heroes white.
   The tool now takes the used output name from the API, checks every link, and runs with `-culture=en`.
4. **Translucent eye layers** (eyes, tear lines, eye occlusion; 21 materials) cannot be masked: they are hidden when
   the fade starts.
5. **Plateau faces are not bare**: the bases' walls stand in front of them; the first rock-skirt placement there was
   invisible. Moved to the ramp parapets (the real bare seams).
6. **Smooth displaced rock read as a row of eggs** in the engine; the pile is now convex-hull broken stones.
7. **UE mirrors Blender's Y** on import (left-handed): the import gate measures the bounds and the placement uses the
   measured convention.

## Evidence

| Check | Result |
|---|---|
| Unit/spec tests | 28/28 (27 Arena specs + RigLogic) |
| `-ArenaAnimLab` | 40/40 (new: bodies lie fully drawn 2.7 s after the fall, fade materials present, minions gone and heroes fading 6 s after death, every unit back on its anim blueprint after the drop-in) |
| Death pelvis heights (2.7 s after death) | heroes 21–37 cm (Countess leaves in bats), minions 12 cm (was 83–88 cm at the end of their clip) |
| Fade | every hero lies fully drawn 2.7 s after its fall (fade 0.00), 6 s after: minions gone, heroes fading or gone; `LAB_07_Fading.png` |
| Turning A/B, 3-min bot match seed 3 | smooth: `yaw_snaps=0/174675`; old snap (`-ArenaSnapTurn`): `513/222022`; both `all4=10 stuck=0` |
| Camera `-ArenaBaseLab` | 13/13 (new 2): back 1.5 m from the east base wall the arm is 147 of 420 cm and the eye-to-camera line is clear (no view through the wall); on open ground it glides out 147 → 166 cm after 0.12 s → 435 cm after 1.8 s (full, shoulder offset included). `BASE_CamWall.png` |
| Other labs | MechLab 35/35, AimLab 14/14 (assisted 85 % vs 15 % unassisted hits on a strafing hero at 12 m, unchanged with the new camera), SkillLab 34/34 |
| Foliage after the map change | `FOLIAGE_SUMMARY floating=0 buried=0 checked=12664` |
| Rock meshes | Blender validator `ASSET PASS` ×3 (1184 / 1936 / 856 tris), UE import gate `ENV OK n=3`, 32 pieces on the parapets, camera shots `CAM_v8b_*` |
| Bot matches (5 min, headless) | seeds 1 / 2 / 5: `all4=10 stuck=0`, `yaw_snaps=0/334915`, `0/403325`, `0/287669`; rendered UI demo seed 3: `yaw_snaps=0/362689` |
| Performance (UI demo, seed 3, 3 min, editor -game 1600×900) | with the fade materials 128.4 fps average, 91.2 fps 1 % low, worst frame 217 ms; A/B without them (`-ArenaNoFadeMats`) 121.4 / 80.4 / 232 ms: the masked copies cost nothing measurable. The first-wave stall (1152 ms in v8a) is gone: the remaining >100 ms frames are the demo's screenshots. One earlier run read 109.6 fps: run-to-run variance (see the A/B) |

## Blender: sense and cost (the operator's question)

- **Done here, zero cost**: procedural environment meshes (a seeded script, no downloads, no paid generators,
  ~2 s per piece in headless Blender 5.2), validated like any asset and imported by a script. Right tool for simple
  natural shapes that must fit the map's scale and the world-aligned rock material.
- **Not done, on purpose**: editing the Paragon heroes (a round trip through Blender risks rigs and materials, Epic
  content cannot go into the repo, little to gain); AI 3D generators (paid).
- **Next candidates**: rock piles for the pit ramp walls and the ridge ends, cliff-edge caps for the tops of the
  parapets. Library content (KiteDemo rocks, Paragon props) stays first choice where a fitting piece exists.

## Open / known

- The Paragon anim blueprints log one "divide by zero" on their first update after a (re)init (pack issue, harmless).
- In the editor the first run after new materials shows "Preparing shaders" and grey bodies for a while; the
  Shipping build ships compiled shaders.
- A timing check of the AnimLab (crate shove) can fail during that first shader-compile run; it passes on a warm run.
- Team A won 7 of 9 seeds in v7b and seeds 1, 2, 5 here (seed 3 went to team B twice); the teams and the map are mirrored: to investigate (T3).

## Verdict: **Pass (T1 + T2)**

Shipping package: `RunUAT BuildCookRun ... -clientconfig=Shipping -iostore -compressed -IgnoreCookErrors` → `BUILD SUCCESSFUL`, exit 0,
`Build/Windows/ParagonArena.exe` (2026-09-27 19:56; the fade copies and the rock meshes cooked from `/Game/Arena`). The
packaged exe ran `-ArenaBaseLab` (`BASE_CamWall`), `-ArenaAnimLab` (`LAB_06_Dead`: minions flat on the floor, fading;
`LAB_07_Fading`: Gideon mid-fade, Kwang still lying, the others gone) and `-ArenaUIDemo -ArenaBotMatch -Seed=3 -Minutes=3`:
`Saved/ArenaPerf.txt`: 122.4 fps average, 87.2 fps 1 % low, worst frame 200 ms, preload 238 ms, `all4=10`; all exit 0.

Next (v9, operator request of the same day): minions from the free Paragon: Minions pack, crisp HUD text (Slate fonts at
the drawn pixel size), HUD and portraits, abilities (look from the owned Paragon FX, behaviour, collision, physics,
indicators), a temporal dither for a smoother fade.
