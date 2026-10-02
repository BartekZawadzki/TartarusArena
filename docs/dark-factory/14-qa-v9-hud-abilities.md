# 14 — QA: v9 crisp HUD, portraits, ability indicators, ability behaviour and effects

Scope (operator request of 2026-09-27): minions from free libraries with their animations; text is low-resolution,
everything must be sharp; HUD and hero portraits; most of all the abilities: their look (free libraries), their
behaviour, mechanics, code, collision, physics, intuitive; indicators better matched and more visible.

## Findings and fixes

| # | Finding (how it was found) | Fix |
|---|---|---|
| 1 | **Explosive shots did nothing to the enemy they hit** (`-ArenaFxLab`: Gideon's Burden and Sparrow's Explosive Arrow, 0 damage on a dummy in their aim). The projectile recorded the touched enemy as already hit, then exploded, and the blast skips the already hit | the shot bursts first; the enemy it touched is in the blast. Now 94 / 98 damage and a 1.35 / 2.25 m knockback |
| 2 | **The lobbed arrow (Sparrow 1) almost never landed**: fixed 24° launch at 22 m/s = a ~35 m arc, but its life was range / speed = 0.8 s, so it vanished in the air 4 m up after 16 m, over everything closer. The indicator showed the blast at the end of the lane | the arc is solved for the aimed ground point within range (horizontal speed = the data's speed, vertical speed from gravity and height); it bursts on the ground there or on the first enemy on the way; the indicator shows the path and the blast circle exactly there |
| 3 | HUD text blurry: the canvas drew the engine's legacy font at its own size and stretched the glyphs by the HUD scale (x1.7 for labels, x2 at 4K) | Roboto (runtime composite) rasterized at the drawn pixel size via `FSlateFontInfo`, bold from 15 px, a thin dark outline; minimum 12 px at 1080p; the same sizes, so the layout does not move |
| 4 | Ability icons muddy: 128 px without mips drawn at 40–120 px, and the hero's pose render behind the symbol | icons 256 px with mips; the symbol on a clean vertical wash of the ability's colour |
| 5 | Portraits 256 px on the empty sky (flat light blue) | 512 px with mips, a studio backdrop (soft glow of the hero's colour, dark edges; `M_ArenaBackdrop`), a third (fill) light |
| 6 | Indicators washed out on the bright stone of the bases (a pale fill lightening the floor, an 8 cm rim) | three masks: the fill darkens and tints the ground, a ~24 cm rim glows in the ability's colour, a dark outline separates it from any floor; a lane's far end as wide as its sides |
| 7 | Effects in the wrong place or size (`FX_*` gallery): Gideon's black hole opened under the caster and lingered seconds; Greystone's ultimate blast was scaled x3 (a white screen); a huge blood vortex under Countess for a dagger throw; buffs left no visible sign | the black hole is the telegraph at the target and ends with the blow; blast scale from the radius, capped (optional `fxScale` in the data); no cast vortex for the daggers; buff effects ride on the bearer for the buff's time, plus a glowing circle under it (`ShowAura`) |
| 8 | The lane of a shot on a ramp was measured level, the shot flies up or down the slope | the lane is measured along the shot's own 3D line |
| 9 | Dead bodies faded as a static dot pattern | the dither pattern shifts every frame: TSR averages it into a smooth transparency |
| 10 | A stale fade copy of a translucent eye layer (made by the tool's first run) was still picked up by name: a material that does not compile on Greystone's eyes (also in the v8 exe) | the tool clears its own output folder before each run |

## Evidence (filled from the regression run)

| Check | Result |
|---|---|
| `-ArenaFxLab` (new) | every ability of every hero cast at a dummy placed where it reaches (25 screenshots `FX_<hero>_<slot>`, FXLAB lines with damage and knockback); a shot up and a shot down the base ramp at a dummy on the slope land; a knockback towards a wall stops the body 32 cm before the wall face |
| `-ArenaFxLab` result | 47/47: 25 casts, 19 damaging abilities all hit, Gideon Burden 94 dmg / 1.35 m knockback, Sparrow Explosive Arrow 98 dmg / 2.25 m, ramp shots 31 dmg up (+2.0 m) and down (−2.6 m), the wall stops a 4 m knockback 32 cm before its face. (A first ramp-shot run missed: a physics block on the ramp, cover, rightly stopped the arrow — found with the new `evt=shot_blocked` verbose log) |
| Unit/spec tests | 28/28 |
| Labs | AnimLab 40/40 (x2, the first compiling the new fade shaders), BaseLab 13/13, MechLab 35/35 (indicator shapes vs data, casts land where shown), AimLab 14/14, SkillLab 34/34 |
| Bot matches (5 min, headless) | seeds 2 / 7: `all4=10 stuck=0`, `yaw_snaps=0/360391`, `0/338231`; winners team A / team B |
| UI demo (seed 3, editor) | 112.9 fps average, 76.3 fps 1 % low; frames over 100 ms only at the demo's screenshots; `all4=10` |

## Minions

The free **Paragon: Minions** pack (Epic Games, Fab standard licence, UE 5.0–5.8: minion models, animations, FX in the
Dawn / Dusk style of the map) is the chosen source. It has to be added to the project from the operator's Fab
library; until then the minions stay UE mannequins.

## Verdict: **Pass (T1 + T2)**

Shipping package: cooked and staged (`BUILD` stage complete, 4.3 GB). The archive step could not overwrite
`Build/Windows` while the operator was playing the v8 exe from it, so the v9 build was copied from
`Saved/StagedBuilds/Windows` to **`Build/Windows_v9/ParagonArena.exe`** (2026-09-27 21:55). It contains the new code
(the HUD font path, the portrait backdrop, the FX lab) and ran `-nullrhi -ArenaBotMatch -Seed=4 -Minutes=3` to the end:
`all4=10`, 0 hitches, preload 286 ms. The rendered check of the exe waits until the operator closes the game (the labs
are not run on the GPU while they play). The v8 exe carried stale fade copies on Greystone's eye layers (finding 10).

**Rendered check of the v9 exe (done after the operator closed the game):** `ParagonArena.exe -ArenaFxLab` exit 0 with
all 27 screenshots; `-ArenaUIDemo` (seed 3) 126.6 fps average, 97.5 fps 1 % low, worst frame 197 ms (a demo
screenshot), `all4=10`. The v9 exe then replaced v8 in `Build/Windows` (v8 kept in `Build/Windows_v8`).
