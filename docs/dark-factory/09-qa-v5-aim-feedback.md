# 09 — QA: v5 aim assistance and hit confirmation

Scope (operator request of 2026-09-27): attacks are still unintuitive, it is hard to attack, to land hits and to
know when a hit actually landed; keep improving and ship an update with a new exe.
Every line is reproducible from the repository root; the logs were kept outside the repository.

## What changed for the player

| Before | Now |
|---|---|
| A shot flew exactly along the crosshair: a strafing hero 12 m away was hit 5–25 % of the time even with the crosshair on its body | the crosshair picks the enemy (red crosshair, brackets, enlarged bar with its health); the shot leads its movement: 80–90 % |
| A shot at a picked target was lobbed (the gravity branch took it) | only gravity shots lob |
| A swing went where the camera looked; an enemy just past the blade was missed | the swing turns the body to the picked enemy, sticks to it through the wind-up, and steps up to 70 cm (stops dead) |
| The only sign of a hit was a small damage number | X on the crosshair (white / yellow crit / red kill), a white rim flash on the victim, a larger number beside it, a tick sound, a flash of the bar frame; a whiffed swing leaves a grey arc on the ground |
| The shot lane showed the crosshair direction | it shows where the shot will actually go (the lead) |

## Evidence

| Check | Command | Result |
|---|---|---|
| Compile | `Build.bat ParagonArenaEditor Win64 Development ... -NoHotReloadFromIDE` | Succeeded, 0 errors, 0 warnings in `Source/` |
| Materials | `-run=pythonscript -script=Tools/make_materials.py` | `MATERIALS OK` (+ M_ArenaHitFlash) |
| Aim lab, seeds 12 / 21 | `UnrealEditor <uproject> -game -ArenaAimLab -Seed=N` | 14/14 PASS, `LAB_SUMMARY fails=0` (`aimlab3.log`, `aimlab4.log`); screenshots `AIM_Track`, `AIM_Hit`, `AIM_Melee`, `AIM_Runner`; seed 7 shot numbers from `aimlab2.log` (same shot code, before the lab's runner step was fixed) |
| Unit/spec tests | `-ExecCmds="Automation RunTests Arena;Quit" -nullrhi` | 22/22 Success |
| Mechanics lab | `-game -ArenaMechLab` | 35/35 (`MechLab_v5b.log`); one earlier run had a dash settle 102 cm off (threshold 90) while a headless match ran beside it, rerun alone 45 cm |
| Animation lab | `-game -ArenaAnimLab` | 28/28 |
| Headless 5v5, 5 min, seeds 7 / 21 | `-game -nullrhi -benchmark -fps=60 -ArenaBotMatch -Seed=N -Minutes=5` | `stuck=0`, `all4=10`, no cast with `assist=1` (bots are never assisted) |
| Rendered match | `-game -ArenaUIDemo -ArenaBotMatch -Seed=3 -Minutes=3` | 133 FPS average, 87 FPS 1 % low (v4: 121 / 77) |

What the aim lab measures (the real player controller possesses the hero and drives the camera):

| Check | Measured (seed 7 / 12 / 21) |
|---|---|
| 20 basic shots at a hero strafing 12 m away, crosshair on its body (no lead, ±1° hand error) | without assistance 25 % / 10 % / 5 %, with it 80 % / 80 % / 85 % |
| the target is picked while the crosshair is on it | 3303 of 3304 frames; 0 frames with `arena.AimAssist 0` |
| the shot lane points where the shot goes | worst 0.6–0.7° |
| one hit marker per landed player hit | 16 = 16, 17 = 17; a bystander's hit: 1 hit, 0 markers |
| the hit body flashes and stops | overlay on the hit frame; none 0.6 s later |
| a swing at an enemy 55° off the crosshair | picked, body faces it (0.0° off during the swing), lands |
| an enemy 35 cm past the blade | without assistance: miss; with it: a 55 cm step, lands, bodies 196 cm apart |
| a swing at an enemy running across 1.4 m ahead | lands |

## Defects found and fixed in this pass

| Defect (how it was seen) | Fix |
|---|---|
| A shot at a picked target flew with the +0.45 lob of gravity shots (the `else` branch) | only gravity shots lob |
| The lead was taken at the key press, the shot leaves 0.1–0.2 s later | the lead is taken again when the shot leaves |
| The body turned to the target for one frame only (`bUseControllerRotationYaw` put it back) | `HoldFacing` overrides `FaceRotation` through the wind-up |
| The step into a swing slid on 55 cm after its end (the dash's 250 cm/s exit speed) | the step ends with zero speed |
| Two health bars on the picked enemy | its own bar grows instead |
| The overlay flash on every hit in a 10-hero fight cost 20 FPS (render state recreated per hit) | the overlay only for the player's hits and the player's hero; other hits keep the tint flash |
| Damage numbers covered the hit marker | the player's numbers pop beside the target |

## Verdict: **Pass (T1 + T2)**

Measured checks (T1): aim lab 14/14 on two seeds, specs 22/22, mechanics lab 35/35, animation lab 28/28, 0 stuck,
FPS up. The HUD feedback is verified from screenshots (T2). Whether aiming now feels right is the operator's call (T3).

Shipping package: `RunUAT BuildCookRun ... -clientconfig=Shipping -iostore -compressed -IgnoreCookErrors` → `BUILD SUCCESSFUL`, exit 0,
`Build/Windows/ParagonArena.exe` (2026-09-27 11:47). The packaged exe ran `-ArenaAimLab -Seed=7` to the end (all four `AIM_*`
screenshots: red crosshair, brackets, the X marker, the enlarged bar, the number beside the target, the swing arc) and
`-ArenaUIDemo -ArenaBotMatch -Seed=7 -Minutes=2.5` (all seven `UI_*` screenshots), both exit 0; Shipping writes no log, the
screenshots are in `%LOCALAPPDATA%\ParagonArena\Saved\Screenshots\Windows`.
