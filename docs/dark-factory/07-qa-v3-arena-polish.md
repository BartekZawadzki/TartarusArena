# 07 — QA: v3 map, HUD, shop, score and time, reaction animations, physics

Scope (operator requests of 2026-09-27): a varied map from the library packs; a HUD with icons instead of text
boxes; locomotion animation for every unit; an item shop; a paid revive with a cooldown; team points for kills and a
match clock (5 / 10 / 15 min); a performance pass; then every missing reaction animation and effect from the packs,
and no unit or object clipping into another, riding on another or being flung into the air.
Every line is reproducible from the repository root; the logs were kept outside the repository.

## Evidence

| Check | Command | Result |
|---|---|---|
| Compile, project code | `Build.bat ParagonArenaEditor Win64 Development -Project=... -NoHotReloadFromIDE` | Succeeded; 0 errors, 0 warnings in `Source/` |
| Map | `-run=pythonscript -script=Tools/build_arena.py` | `ARENA_MAP OK actors=347`, 6943 scatter instances, 100 solid footprints kept clear of trees and rocks |
| Hit reactions (additive → regular copies) | `-run=pythonscript -script=Tools/make_hitreacts.py` | `HITREACT OK made=20` |
| Asset manifest | `-run=pythonscript -script=Tools/build_manifest.py` | `MANIFEST OK assets=211 missing=0` |
| Unit/spec tests | `-ExecCmds="Automation RunTests Arena;Quit" -nullrhi` | 21/21 Success (score, gold, shop, revive, data limits incl. knock caps, bot retreat spots) |
| Animation and physics lab, rendered | `UnrealEditor <uproject> -game -ArenaAnimLab` | `LAB_SUMMARY fails=0`, 28 PASS, 12 screenshots `LAB_*.png` (log `animlab_final.log`) |
| Headless 5v5, 5 min, seeds 7 / 12 / 21 | `-game -nullrhi -benchmark -fps=60 -ArenaBotMatch -Seed=N -Minutes=5` | `stuck=0` on all three; 41–45 deaths, 31–34 purchases, 6–9 revives, `all4=10` (logs `reg4_bot_*.log`) |
| Headless 5v5, 10 min, seed 33 | same, `-Minutes=10` | `stuck=0`, 245:232, 42 purchases, 13 revives, 17 minions scored in a base |
| Rendered match, 1600×900, editor `-game` | `-ArenaUIDemo -Seed=7 -Minutes=2.5` | avg 124 FPS, 1 % low 77 FPS (`ARENA_PERF`), all six UI screenshots |
| Shipping package | `RunUAT BuildCookRun ... -clientconfig=Shipping -iostore -compressed -IgnoreCookErrors` | `BUILD SUCCESSFUL`, exit 0; `Build/Windows/ParagonArena.exe`, 3.9 GB of paks |
| Packaged exe, lab and UI demo | `ParagonArena.exe -windowed -ArenaAnimLab`, then `-ArenaUIDemo -Seed=7 -Minutes=2.5` | both exit 0; 12 `LAB_*` + 6 `UI_*` screenshots in `%LOCALAPPDATA%\ParagonArena\Saved\Screenshots\Windows` show the stun, death, drop-in and victory animations and the end screen (win on time, 71:65). Shipping has no log output, so the measured checks are the editor runs above |

What the lab measures (each line is `LAB PASS/FAIL` in the log):

| Check | Measured |
|---|---|
| hit reaction plays for every hero (upper-body slot of the pack's anim blueprint) | 5/5 |
| a minion's punch keeps it in place (the mannequin attack carries 1.5 m root motion) | moved 0 cm |
| stun start + loop plays for every hero | 5/5 |
| strongest knock-up in the data (4.5 m/s) | rise 98–100 cm, all 7 units back on the ground |
| death animation, body on the floor | pelvis 7–34 cm above the floor for 6 units; Countess rises and vanishes in her shadow burst |
| after the drop-in every unit is back on its anim blueprint | 7/7 |
| only the new line-up is visible after a respawn | 7 of 7 |
| unit dropped dead-centre on another's head | slides off to the ground, 179–193 cm apart, upward speed 0 |
| ultimate blast next to a 40 kg crate | crate lifts 16 cm, 650 cm/s along the ground |
| hero walks straight into the crate | shoves it (≈60 cm/s) and walks on, crate lifts 5 cm |
| unit on the rim of the orb altar sent to a far goal | walks off, 1888 cm |

## Defects found and fixed in this pass

| Defect (how it was seen) | Fix |
|---|---|
| Dead bodies flung ~90 m/s into the air (ragdoll impulse 9000 + 5000 with velocity change) | pack death animations played on the mesh and held on the last frame, no ragdoll; the body sinks and hides; fall direction checked against walls |
| Bots hopping 3.2 m/s straight up when stuck; units launched off heads and props | unstick side-steps on the navmesh (no launch); `UArenaMovementComponent` never takes a pawn or a loose prop as floor, a unit slides off sideways |
| Blasts threw props 15–30 m/s (radial impulse as velocity change 1200–3000) | shove capped at 650 cm/s, mostly horizontal; props damped, spin capped, depenetration 60 cm/s |
| Walking into a crate flung it (engine push 750000 N) | push tuned in the lab: a walking hero shoves it aside, it lifts ~5 cm |
| Knock-ups of 6–8 m/s (3–3.3 m) | data limit knockUp ≤ 4.5 m/s, knockback ≤ 5 m/s, enforced by the data validator |
| Minions shoved into their targets by the punch's root motion | root-motion translation scale 0 on minions |
| Minions and bots catapulted by jump pads they walked over | pads launch heroes only; a nav obstacle area keeps bot paths off them |
| Heroes walking through rails, arches, gates, braziers, rubble, wall faces | the pieces keep their own simple collision (all have one), trees and rocks never scatter into them |
| Hero meshes overlapping (34 cm capsule on broad Paragon bodies) | 40 cm hero capsule, navmesh agent 42 cm (`SupportedAgents`) |
| Units spawned inside or on top of each other at the base | spawns take the first free spot on rings of 1.5 m and 3 m |
| Hit-stop from every hit, minions included, kept heroes in slow motion under fire (a 3 s "stuck" at 5 % time) | hit-stop only for heavy blows between heroes, once per 0.4 s per unit |
| Retreating heroes piled on one point at the fountain and body-blocked | each hero has its own spot on a 3.2 m ring around the base |
| A brazier given collision narrowed the pass beside the pit ramp (bots wedged) | moved clear of the corridor |
| Camera inside tall bushes (end-screen shot of the UI demo) | bushes scaled to 0.7–1.1, below the follow camera (not re-shot from the same spot) |
| A hit flash stayed on dead bodies (white glow) | flash cleared at death |
| Lab kills triggered real respawns (extra heroes in the line-up) | lab deaths skip score and respawn |

Animations and effects added from the packs: intro (`LevelStart_Montage`) during the countdown; hit reaction by
side (front / back / left / right); stun start and loop; airborne knock pose until landing; death (1–2 variants per
hero, mannequin deaths for minions) with Gideon's and Sparrow's death effects and Countess's shadow-burst exit;
drop-in `Respawn` landing with a light column (`P_Core_CharacterRecall`) and each hero's spawn effect; victory emote
for the winning team. Ability sequences now play through the anim blueprint's own slot (`UpperBody` on every Paragon
hero — the old `DefaultSlot` does not exist there).

## Verdict: **Pass (T1 + T2)**

Rules, bots and physics verified with measured checks (T1: 21/21 specs, 28/28 lab checks, 0 stuck in four seeded
matches); animations, effects and HUD verified from screenshots of the editor and the packaged exe (T2). The feel of
the fights, the readability and the look of the map are the operator's call (T3).

Known limits: hit reactions come from the packs' additive clips turned into regular upper-body clips, so a hero
running while hit keeps running legs (by design); Countess's pack death rises instead of falling, so she vanishes in
her shadow burst; minions have no hit reaction (the mannequin's are rifle poses).
