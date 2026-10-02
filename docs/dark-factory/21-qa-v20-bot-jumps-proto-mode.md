# 21 — QA: v20 bots that jump, and the new game's prototype mode

Scope (operator 2026-09-29): "the bots don't use the jump, fix that, and start work on a separate mode with a separate
menu, launched from the current one, for a new game: a map of simple grey UE objects with higher and lower objects,
buildings and alleys you can jump onto — the block of a small town with three lanes like a classic MOBA, third-person
view, a simple grey character model, new full movement like a JRPG (running, jumping, sneaking) and full combat with
different attacks, and the same kind of opponent bot to test the mechanics and the style of the new game; no art yet,
as is normally done in game dev. Build on the assets, mechanics and scripts we have and add only what is missing."

## Bots jump (Tartarus Arena)

`AArenaBotController::BotJump` (one jump per 2.5–4.5 s, only on the ground, never while casting, stunned or dashing):

| When | Why |
|---|---|
| a projectile flies at it (one bot in three of those that dodge) | jumping over a skillshot is the dodge a player uses |
| a ranged bot kiting a hero within 6 m (35 % of the steps) | a hop breaks the enemy's aim while it backs off |
| a melee bot whose target stands 0.7–2.6 m higher within 4.5 m | the step, rock or ledge the target stands on |
| the first unstick try | a jump frees it from a lip the path cannot see |

Evidence: `ARENA_BOTS jumps=` in the log of every bot match. Conquest seeds 11 / 12, 12 min: **188 / 249 jumps** (v19: 0),
`stuck=0`, all 10 heroes cast all four abilities; deaths on both matches: tower dives 3, outnumbered 18, fair 24.
Unit / spec tests 51/51.

## The prototype mode (the new game)

Launched from the main menu (**NEW GAME · PROTOTYPE**, the 4th button) → `/Game/Maps/Proto` with its own game mode,
player controller, HUD and menus (`Source/ParagonArena/Proto/`). Tartarus Arena is untouched by it except the button and
`AArenaHUD`'s drawing primitives becoming `protected` (the prototype's HUD reuses the same fonts, panels and buttons).

### What was reused, what was added

| Reused (already in the project) | Added |
|---|---|
| UE template mannequins SKM_Manny_Simple / SKM_Quinn_Simple (the grey model; Quinn is the bot) and `ABP_Manny_Combat` | `AProtoCharacter`: the moves and the combat |
| Template animations: Unarmed attacks 1–3, charged attack, dash, jump, fall, land, wall jump, hit reacts, deaths | `AProtoBotController`: the test bot |
| `/Engine/BasicShapes` cube and cylinder, `M_ArenaTraining` (the metre-grid grey of the training map) | `Tools/build_proto.py`: the town greybox, 12 grey tones by height |
| `build_training.py` helpers, the arena's HUD primitives, fonts, settings (sensitivity, invert Y) | `AProtoGameMode`, `AProtoPlayerController`, `AProtoHUD` |
| The runtime Enhanced Input pattern of the arena controller | the ProtoLab (`-ProtoLab`), PASS / FAIL lines |

### The map: a grey town with three lanes (200 × 120 m)

Point-symmetric (every piece authored for the west half is also placed at (-x, -y)), 256 actors. Base plazas at x ±7600
(a core, lane gates), the mid lane as the main street (14 m), the top and bottom lanes along the edges, a "river"
street across the middle (with a fountain and a round centre plaza with a pillar), towers along every lane. Between
the lanes four quarters of city blocks with alleys (3.5–4 m) and:

- height steps that read at a glance (lighter = lower): crate 1.2 m, **2.5 m (a jump + climb)**, **4 m (a double jump
  + climb)**, 6.5 m and 9.5 m roofs (reached roof to roof), 14 m landmarks (the clock tower, the edge buildings);
- staircases of roofs (crate → 2.5 → 4 → 6.5 → 9.5 m), ramps from the alleys to 4 m landings, bridges over the alleys,
  **two overpasses over the mid lane** at 4 m, covered passages (a 3 m tunnel, a 5 m market hall), a walkway over an
  alley at 2.5 m (jump up to it, walk under it), porches along the top and bottom lanes, a walled courtyard with cover
  for sneaking, stairs up a corner house, lamp posts;
- 28 patrol points, an overview camera for the menu, a runtime navmesh (the project's navmesh is dynamic).

### Movement (JRPG / action-adventure, code-driven)

Tuning in `FProtoTuning` (one struct, measured by the lab): run 5.2 m/s, sprint 8.4 m/s (drains stamina), sneak
2.3 m/s (the body low, silent), jump 1.65 m, **double jump** +1.65 m, **climb (mantle)** a ledge up to 2 m over the feet
(in the air with the stick forward or the jump key), **wall jump** (after the double jump, the jump key against a wall),
**dodge** (a tap of Shift: 5.3 m in 0.3 s, 0.28 s invulnerable, also in the air, cancels a light swing), a free camera
the body turns to (orient to movement), a lock-on (Tab) that turns the camera to the target. All movement comes from
code: the animations' own root motion is off (the dash animation carried the dodge 40 % further).

### Combat

- **Light combo** of three (8 / 10 / 16, the third knocks back), a press during a swing chains the next;
- **heavy**: hold to charge (1.2 s), release: 20–46 damage, a full charge **breaks the guard**;
- **running strike** out of a sprint (a lunge, 14), **plunge** out of the air (a slam, 18 in 3.2 m);
- **block** (−75 %, drains stamina, a grey shield), an **early block parries** (the attacker stunned 1.2 s);
- **sneak attack**: a blow from behind on an unaware enemy ×2.5;
- stamina for everything, hit-stop, hit reactions, staggers, knockback, KO and respawn (3.5 s), damage numbers.

### The test bot

The other mannequin with the same moves. Sight: 28 m in front, 9 m behind (heard); a sneaking player is heard within
3.5 m and seen in front within 12 m, never through walls. Unaware: in the fight modes it heads for where the player
roughly is (a guess within 12 m, re-planned every 5–8 s; `?` over its head), goes to where it last saw the player, in
the sandbox it patrols; hunting (`!`): sprints from far, running strike, jumps (and climbs) to a target above, plunges
on a target below, blocks light swings (a parry when quick), steps out of a charged heavy, retreats on low health or
stamina, mixes combos and heavies. Difficulty changes reactions (0.45 / 0.28 / 0.18 s) and how often it reads the
player right (35 / 60 / 85 %), not its aim.

### Menus and HUD

Own main menu (duel 1v1, two bots 1v2, sandbox, bot difficulty, controls, back to Tartarus Arena), pause (resume,
restart, controls, prototype menu, Tartarus Arena), controls screen and F1 overlay. HUD: health and stamina, the state
(sprint, sneaking, block, dodge, climb, in the air, charging %, stunned), the combo counter, the heavy's charge bar, the
lock-on diamond, the bots' bars with their stamina, awareness (`!` / `?` / `·`), block, stun and charge, damage numbers
(block, sneak attack, parry, dodged), the score, the respawn countdown.

## Evidence

| Check | Result |
|---|---|
| Unit / spec tests (`Automation RunTests Arena`) | 51 / 51 |
| Conquest bot matches, seeds 11 / 12, 12 min | jumps 188 / 249, stuck 0 |
| ProtoLab headless (`Proto -game -nullrhi -ProtoLab`) | **21 / 21 PASS** |
| Duel headless (`-ProtoDuel`, difficulty 0 and 2) | the bot crosses the town and spots the player at 30 s (26–27 m), fights, KOs and respawns work, no game errors (only the engine's Python toolset plugins' import noise) |
| Arena → Proto (`-ArenaStartMap=Proto`) | the map loads with `ProtoGameMode` |
| **Packaged exe** (Shipping, `Build/Windows`), headless: `ParagonArena.exe -nullrhi -ArenaStartMap=Proto -ProtoLab` | **21 / 21 PASS** in `Saved/ProtoLab.txt` (the Shipping exe writes no log: the lab's lines also go to that file), 59 s |
| Packaged exe, Arena bot match seed 2, 3 min, headless | finished, `hitches=2`, no crash reports; `GameUserSettings.ini` restored byte for byte |

The ProtoLab lines (tuning in brackets): run 520, sprint 840 (stamina drains), sneak 230; a jump rises 165 cm, a double
jump 331; the 2.6 m ledge is climbed (feet at 263); a wall jump off an 8 m wall; a dodge covers 530 cm and a blow during
it deals 0; a three-hit combo 34 (34); a full heavy 46 (46); a blocked blow 2.0 (2.0); a parry stuns the attacker and
nothing lands; a full heavy breaks the guard; a sneak attack from behind 20 (20); a plunge's blast 18; the bot reaches
the player and lands hits, mixes its moves and defends against the player's swings (dodges, blocks); a sneaking player
9 m behind the bot is not seen, 9 m in front is seen.

Found and fixed by the lab: the climb looked for the wall at the capsule's centre (over a low ledge at the jump's top);
the wall jump's reach (0.95 m) was shorter than the gap to the wall; the dodge ran 886 cm (the dash animation's root
motion on top of the code's force); the climb test measured a 1.8 m ledge that the jump itself lands on (now 2.6 m, and
the climb's reach 2 m so that 2.5 m is a jump and 4 m a double jump); in the duel the bot walked the random patrol and
found the player after 23 minutes (now it hunts: 30 s).

Packaging: the cook took D: from 29.2 to 3.6 GB free (the v19 build and the Shipping intermediates were deleted first; the
mannequins and the Proto map are cooked: `DirectoriesToAlwaysCook` /Game/Characters/Mannequins, `MapsToCook` Proto).

Not verified: the rendered runs (the lab's screenshots, the menu clicked with the mouse, the look of the map) — a window
on the operator's desktop needs the operator's go-ahead.
