# 23 — QA: v22 prototype 2 — the same new game with Hades' controls

Scope (operator 2026-09-29): "create another prototype of the new game for testing the mechanics: everything the same —
the same world, characters and assumptions — but this time all the controls the same as in the game Hades, for the bot
and the player, adding only a jump on Space."

## What it is

Main menu → **NEW GAME 2 · HADES CONTROLS** → the same map (`/Game/Maps/Proto`, the URL option `?Hades`), the same
mannequins, moves, combat numbers, bot, menus (duel / two bots / sandbox, difficulty) and the controls card at the start
of each match. What changes is the control scheme and the camera, as in Hades:

| Hades (PC default) | Prototype 2 |
|---|---|
| a fixed camera above the room | a fixed camera 19 m away at 55° down, turned 45° (the town's streets run across the screen); a building between the camera and the hero is cut away (hidden, its collision kept) while it is in the way |
| WASD — move on the screen | the same: W runs up the screen, D to its right |
| the cursor aims | the attack, the cast and the special go at the cursor on the ground (a ring under it, a line from the hero); a narrow homing (4.2 m, 32°) |
| LMB — attack | the three-hit combo (held: it keeps going); in the air: slashes |
| Space — dash | **Shift** (Space took the jump); the dash, its invulnerability, the perfect dodge and the counter as in prototype 1 |
| LMB during / right after a dash — dash-strike | the dash-strike (12, a lunge) |
| RMB — cast | a stone thrown at the cursor: 14 m, 10 damage, a 35 % slow for 2 s; it comes back after 3 s |
| Q — special | tapped: a smash around the hero (16 within 2.4 m, both sides); held: the charge, a full one launches (as prototype 1's heavy); in the air: the slam |
| F — call (god's aid, the wrath gauge) | the wrath gauge fills with blows dealt (×0.8) and taken (×1.2); full: F — a blast of 28 within 5.2 m that throws every enemy up, 0.6 s invulnerable, the time slows a moment |
| E interact, R reload, C codex… | nothing to interact with in the prototype |
| — | **Space — the jump** (the only addition): the jump, the double jump, the ledge climb, the wall jump |

The bot uses the same verbs: it throws its stone at range (leading the target on hard), closes with a dash and a
dash-strike, attacks, smashes around, charges and launches, juggles in the air, slams from above, calls the wrath when
its gauge is full and the player near, reads the player's swings and dashes out of them; no sprint and no lock-on (Hades
has neither). The player's HUD adds the stone (ready / back in n s) and the wrath gauge.

## Evidence

| Check | Result |
|---|---|
| Prototype 2 lab (`Proto -game -nullrhi -ProtoLab -ProtoHades`), 16 checks | **16 / 16 PASS** |
| Prototype 1 lab (unchanged, the shared character) | 26 / 26 PASS |
| Duel headless, hard bot, Hades controls (`-ProtoDuel -ProtoHades -ProtoDiff=2`) | spots the player at 32 s; its blows by kind: light 553, air slash 484, stone 223, dash-strike 221, finisher 156, wrath 134, launch 84, smash around 71, slam 49, charged 11; no game errors |

The lab lines: the camera is fixed above (pitch −55, yaw 45, 19 m); WASD on the screen (W at yaw 45 = up the screen, D at
135, 520 cm/s); Space jumps 165 cm; the dash covers 530 cm and a blow in it deals 0; LMB attacks at the cursor (aimed
away: 0, on the dummy: a hit); the dash-strike 12 (12); the cast hits 9 m away (10, slowed, ammo 0 after the throw) and
the stone comes back after 3 s; Q tapped smashes around (the dummy in front and the one behind: 16 each); Q held to the
full launches (46, 2.9 m up); the wrath gauge fills with blows (88 of 100 by then); F calls the wrath (28 and 28, both
thrown up); a perfect dodge and the counter (12); a building in the camera's way is cut away and comes back when the
hero walks off; the bot lands hits and uses the Hades moves (3 casts, 3 hits, a dash-strike, dodges).

Found and fixed by the lab: the call's own blast refilled the wrath gauge (a call now never feeds itself); the bot threw
its stone too rarely while closing in (it throws at 4.5–13 m in sight most of the time now).

Not verified: the rendered look (a window on the operator's desktop needs the operator's go-ahead).

## The packaged exe (Build/Windows, Shipping)

Verified headless (no window on the operator's desktop): `ParagonArena.exe -nullrhi -ArenaStartMap=Proto -ProtoHades
-ProtoLab` — **16 / 16** (the bot: 3 casts, 3 hits, 3 dash-strikes, 5 dashes); `-ArenaStartMap=Proto -ProtoLab` (prototype
1) — **26 / 26**; an Arena bot match (seed 2, 3 min) finished; no crash reports; `GameUserSettings.ini` restored byte for
byte. `/Engine/BasicShapes` is cooked explicitly (the cast stone's sphere is referenced by code only). To make room for
the staging on C: (the Windows pagefile had grown to 28 GB) the v17 backup was deleted with the operator's OK.

## Rendered (the operator's go-ahead, a 1600x900 window, the packaged exe)

- Both labs in the window: prototype 1 **26 / 26**; prototype 2 14 / 16 at first — in a window the real cursor overrode
  the lab's aim (the Hades controller aims at the mouse every frame); the lab now aims by itself: **16 / 16** rendered.
- New `-ProtoUIShots` (with or without `-ProtoHades`): the prototype's menu, the controls, the card at the start of a
  match, a fight in an alley of the town, the pause — each a screenshot, then quit (on real time: the card and the pause
  stop the game's clock; each shot is taken before the next screen opens — a request renders on the next frame).
- Looked at and fixed: the town's signs (Polish then) drew a box for some Polish letters (the engine's text-render font
  is Latin-1) — the prototype turned them into their base letters at start (since 2026-10-02 the maps' signs are English: `Tools/translate_map_signs.py`); the pause's title sat on the score
  panel (no play HUD under a menu now); the Hades card's mouse drew a wheel and thumb buttons Hades does not use (hidden
  there); the second column of keys stood far from its keys; the pause said "prototype" in prototype 2.
- Seen working: the third-person town and the combo, the top-down view with the aim ring and line, the thrown stone,
  the stone and wrath panel, a building cut away over the hero in an alley, both menus with their mouse panels, both
  controls cards, the pause. The final exe: both labs headless again 26 / 26 and 16 / 16, no crash reports,
  `GameUserSettings.ini` restored byte for byte.
