# 15 — QA: v10 front end and training, v11 casting, team markings, minimap, Paragon minions, lighting

Scope (operator requests of 2026-09-27/28):
- v10: a MOBA front end (menu, modes, settings), a hero browser with ability previews, a cheap grey training map;
- v11: first of all fix what is faulty: the team markings (yours / allies / enemies) are wrong, using an ability is
  sometimes problematic ("its description opens and blocks the use"), the minimap broke visually; integrate the
  Paragon: Minions pack (now added to the project); light, shadows and shaders of the normal (arena) map to the full
  potential of the art, while optimizing everything; lean on real games of the genre.

## v10 findings and fixes

| # | Finding | Fix |
|---|---|---|
| a | The training room rendered white: the build scripts kept every Brush-derived actor, so post-process and nav volumes (and an old manual exposure) piled up on each rebuild | clearing keeps only the world settings and the builder brush |
| b | Hero browser: a new preview booth (with its lights) per hero, the lights stacked | one booth, reused |
| c | Settings: rows overflowed at 900 p, a mixed scalability (-1) showed as Epic, the menu gradient banded (12 strips) | compact rows, none selected for mixed levels, 48 strips |

Evidence (v10): unit/spec tests 28/28; `-ArenaTrainingDemo` fails=0 (DPS 19–130 by hero); `-ArenaUIDemo` shows the main
menu, play screen, hero browser with the ultimate previewed live, settings; 112.4 fps average.

## v11 findings and fixes

| # | Finding (how it was found) | Fix |
|---|---|---|
| 1 | **An ability key on cooldown (or without mana) still entered the aim** and drew the full description panel; LMB then tried that ability instead of attacking, so the hero seemed stuck (code review of `AArenaPlayerController::OnNumber / CastAimed`: a failed cast kept the aim on purpose) | a key that cannot go off never aims: the slot flashes red and the reason is said; three cast modes like LoL / Smite (quick with a preview = default: hold shows, release casts; instant; with confirmation); an input buffer of 0.6 s for a cast pressed while the hero cannot act for a moment (also when GAS refuses the activation that frame); the full description only on Alt |
| 2 | **Teams hard to tell apart**: both teams field the same five heroes, the body tint was 25 % team colour; bars and names in absolute team colours | a ring on the ground under every hero relative to you (green you, blue ally, red enemy, `UpdateTeamRing`); all HUD colours relative (`TeamColor` uses `LocalTeam`); hero bars with 200-HP ticks, a mana strip, a light name; bars that would overlap stack (nearest first, at most two steps), far heroes get a compact bar |
| 3 | **Minimap washed white with blue blotches** (screenshots): the capture used a fixed +3 EV that burned out once the sun got brighter, and the hidden trees still cast their shadows onto it | no dynamic shadows / contact shadows / AO in the capture; exposure calibrated from a read-back of the capture (mean ≈ 0.44 in two passes); hero portraits framed by relation, yours gold; minions; the bases; the camera's view cone; a warm tint |
| 4 | **Minions**: UE mannequins; Paragon: Minions ships no anim blueprint | the Dawn army (team 0) and the Dusk army (team 1), melee and ranged, driven by `UArenaMinionAnimInstance` (native: idle / jog by speed with the play rate following the speed, and a "DefaultSlot" registered by the proxy so attacks, reactions, stuns, knock-ups and deaths play as montages); fade copies of their materials; hit reactions, stun and knock-up for minions too; the ranged shot uses Iggy's turret bolt and the minion muzzle / impact FX |
| 5 | **Minions preferred heroes (×2)**: the player was always their target (review of `ArenaBot::Score`) | LoL's rule: minions fight minions; a hero draws them only after hitting one of their heroes (2.5 s "call for help") |
| 6 | **Lumen never ran**: the renderer settings asked for it, but the GPU profile showed DFAO sky occlusion; UE 5.8 allows Lumen on D3D only with SM6 (`DoesPlatformSupportLumenGI`) and the project targeted SM5 | DX12 SM6 targeted; software Lumen GI + reflections from mesh distance fields |
| 7 | The arena map carried 26 stacked post-process volumes and 27 nav bounds (kept on every rebuild: volumes are brushes) | rebuilt with the fixed clearing: 1 + 1 |
| 8 | Arena look: bright stone burned white, flat shade, a low sun striping the lanes, the arch with yellow blotches (over-exposure) | histogram exposure within EV100 1–3.5, local exposure, a slight colour / contrast lift, the sun at -46°, contact shadows, distance-field shadows past the cascades, a light volumetric haze, the sky light at 1.2 (Lumen bounces the sun) |
| 9 | Optimization | small scatter meshes out of the distance fields and Lumen's scene, their wind off past 30 m (trees past 60 m); a render-scale setting (TSR 50–100 %); `-ArenaCmdAt` for GPU profiles of a running match |
| 10 | Dead-code clash found by the build: two lab helpers named `Place` in one unity file once a new source file changed the grouping | the FX lab helper renamed |
| 11 | **Two abilities had no effect since v9** (the manifest rebuild listed them missing): Countess's Blade Siphon buff and Kwang's Light Strike burst named paths without their `FX/` folder | paths fixed; a new spec loads every asset `heroes.json` names (242), so a wrong path fails the tests |
| 12 | The manifest tool treated every path ending in `_C` as a blueprint class: two minion clips named `Attack_C` / `Death_C` would not have been packaged | an object first, a class only if that fails (the spec does the same) |
| 13 | **Lumen's price** (csvprofile, same run, 1600×900, RTX 3070): Epic with Lumen 17.3 ms a frame, without 12.6 ms; render-thread and GPU bound; the numbers drift with the operator's other apps (the ChatGPT app held ~22 % of the GPU) | `Config/DefaultScalability.ini`: **Epic = Lumen** GI + reflections, trimmed (no translucency volume, off-screen shadowing from the global SDF, half-res short-range AO); **High = no Lumen** (DFAO sky occlusion, SSR, all the other light / post work); the settings button reads "Epic · Lumen"; the packaged game moves a player on Epic to High once (v11 migration), Epic stays one click away |
| 14 | Ability feedback short of LoL / Smite | an icon flashes white when its ability comes off cooldown (a soft chime for the ultimate); a low "no" buzz with the red flash of a key that cannot go off; "+20 g" gold rises from a unit the player finished |
| 15 | A walking hero sometimes flung the lab crate (800–1100 cm/s against a 900 limit): the walk push was added on top of the crate's speed from a blast | the push sets the whole velocity (walking pace ahead, half the sideways drift, never lifted); a blast caps the resulting speed, not only its own push |
| 16 | **SM6 alone cost ~30 % of the frame rate** (High, no Lumen: 66 vs 97 fps) | both shader models cooked; the game starts on SM5 (`DefaultGameUserSettings.ini` `[D3DRHIPreference] PreferredFeatureLevel=sm5`) and the settings switch to SM6 only for "Epic · Lumen", from the next start (a note in the settings says so) |
| 17 | **The new render-scale setting read the engine's "auto" (0) as 50 %** and wrote it back when the settings were saved (found by the exe check: 164.8 fps was a 2560×1440 desktop rendered at 1280×720); the engine also resets the scale with every quality level (High = 87 %) | the render scale is our own key (`[ArenaSettings] RenderScale`, 0 = auto by default), set after the quality level; the stepper goes auto → 100 → … → 50; the operator's settings file restored to auto |
| 18 | **Empty hero portraits on the first start on a new shader model** (exe, Epic/SM6: blank blue squares in the top bar and the player panel): the portraits are shot once at 0.9 s, and until a mesh's pipeline states are compiled the engine does not draw it at all | the icon studio waits for the booths' and the level's PSO precaching (`IsPSOPrecaching`, `PipelineStateCache::NumActivePrecacheRequests`); a first picture at 10 s at the latest, the final one when the compiling is over (40 s cap); `evt=icons_shoot waited= pending= final=` in the log |

## Evidence (v11 regression, 2026-09-28)

| Check | Result |
|---|---|
| Unit/spec tests | 30/30 (new: minion aggro "call for help", every asset heroes.json names loads — 242 paths) |
| `-ArenaSkillLab` | 42/42 (8 new: quick cast aims on press and casts on release; a key on cooldown neither aims nor opens anything; instant cast; cast with confirmation; the input buffer holds a cast through a busy moment and fires it) |
| `-ArenaAnimLab` | 40/40 with the Paragon minions (the crate check failed once at 1138 cm/s — see finding 15 — then passed) |
| `-ArenaMechLab` / `-ArenaAimLab` / `-ArenaBaseLab` | 35/35, 14/14, 13/13 |
| `-ArenaFxLab` | 47/47; Kwang's Light Strike now shows its lightning burst (finding 11) |
| `-ArenaTrainingDemo` | fails=0 |
| `-ArenaFoliageCheck` | floating=0 buried=0 (12 664 checked) |
| Bot matches, headless 5 min | seed 2: `all4=9 stuck=0 yaw_snaps=0`; seed 7: `all4=10 stuck=0 yaw_snaps=0` |
| Arena map volumes | 1 post-process, 1 nav bounds (were 26 / 27) |
| Shader model A/B (High, same bot match, csvprofile) | **SM5 97.2 fps avg, 71.6 1 % low, 10.2 ms; SM6 66.3 / 33.8 / 13.1 ms** — hence finding 16 |
| Epic + Lumen (SM6) | Lumen passes present (`LumenScreenProbeGather`), 20.9 ms a frame in the editor at 1600×900 |

## The packaged game (Shipping, `Build/Windows/ParagonArena.exe`, 4.7 GB, 2026-09-28)

| Run | Result |
|---|---|
| Headless bot match (seed 4, 3 min) | to the end, `all4=9`, preload 437 ms |
| UI demo, default settings (High, SM5, render scale auto), 1600×900 window | **122.6 fps average, 89.5 fps 1 % low**, `all4=10`; menu, play screen, hero browser, settings, match, shop, scoreboard, death, end screens as expected |
| Bot match with the map tour, Epic · Lumen (SM6), first start on SM6 (no PSO cache) | 63.9 fps average, 41.9 1 % low; Lumen on (`ParagonArena_PCD3D_SM6.upipelinecache`), hero portraits present (finding 18) |
| The operator's settings file | migrated once to High (`GfxVersion=11`); the render scale restored to auto after finding 17; the test changes for Epic undone from a copy |

## Verdict: **Pass (T1 + T2)** — T3 (how it plays and looks) is the operator's
