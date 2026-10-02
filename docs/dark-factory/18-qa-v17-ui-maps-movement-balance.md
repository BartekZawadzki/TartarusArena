# 18 — QA: v17 fonts and menus, the big Conquest map, the camera, movement, balance, bots, resolutions

Scope (operator 2026-09-29): make the fonts and the whole UI consistent; compare with Paragon: The Overprime and fix
the graphical gaps (use the packs' full potential); make the Conquest map bigger with all its elements (towers,
inhibitors, the core, the jungle); stop the camera dropping near walls; fix the heroes' movement ("the animations
while moving must not be stiff"); balance the heroes (nerf and boost); then hunt bugs, resolutions, performance
and smoothness. Added during the work: "the bots are far too weak, even on hard" and "the long-range heroes' edge
over the melee ones is far too big"; and "what matters is what you see, not what you don't — performance and
smoothness".

## Fonts and menus

| # | Finding (how) | Fix |
|---|---|---|
| 1 | Roboto everywhere, one weight: the HUD and menus read as a debug overlay next to Overprime's condensed headings | **Barlow / Barlow Condensed** (SIL OFL 1.1, operator approved the download; `Content/Data/Fonts`, credits in CREDITS.md): headings in Condensed SemiBold/Bold with letter spacing, body in Barlow Medium/SemiBold |
| 2 | A font-import commandlet crashed (Slate assert) and a composite font asset drew nothing | runtime `UFont` objects built from the TTFs (runtime cache, a Regular/Bold typeface each, rooted) — no import step, the same files in the editor and the exe |
| 3 | Buttons were flat boxes of three different looks | one button: a vertical shade ramp, a lit top edge, gold corners when selected or hovered, the label shrinks to fit |
| 4 | Titles, the settings' gamepad hint and the main menu's subtitle overflowed at 720p | a title clasp, wrapped hints; the subtitle says what the game is: "MOBA with Paragon's heroes · 11 heroes · Conquest and Arena · bots and LAN play" |
| 5 | HUD: bars, pips and tags drawn over each other; the victory screen fixed to 1080p | stacked bars, mana tags, rank pips, announcements per team (`AnnounceFor`), a victory screen relative to the view |

Evidence: `-ArenaUIDemo` at 1600×900, 1280×720 and 1920×820 (21:9): the menu, play, heroes, settings, hero
select, match, shop, scoreboard, aim and end screens fit and scale at all three.

## The Conquest map (`Tools/build_conquest.py`, `/Game/Maps/Conquest`)

- 250 × 160 m (the Arena is 140 × 92): bases on plateaus at both ends, three lanes (mid and two side lanes with
  ramps up to the bases), per team 3 towers + an inhibitor per lane and the core (20 + 2 structures, all placed on
  the navmesh: `conquest_place on_nav=1` ×22), the jungle between the lanes with paths, walls with openings, two
  plazas (the boss pit north, a ruin south), camps (red and black buffs, two minion camps, the boss).
- Conquest is played on its own map: the menu travels there with the picked hero (`?Hero=N`), a LAN host travels there
  too; the minimap's extent comes from the map's navigation bounds.
- The structures stand at Paragon's own scale (the pack's GDC turrets: tower 0.95, inhibitor 0.9, core 1.15 with its
  Agora base) — at 0.6 they read as toys next to 13 m lanes; capsules, muzzles and the health bars (over the mesh's
  top) follow.
- The world past the forest showed a flat brown plain to the horizon from the menu, hero-select and overview
  cameras. Both maps are now closed by a ring of KiteDemo cliffs (one scale, no shadows, no collision, hazy in the
  fog): the Arena 40–60 m at 220 × 180 m, Conquest 48–72 m at 330 × 280 m. A first try with Agora's rock strips
  stretched to 140 m stood as a flat streaked wall over the lanes from the player's camera and was replaced; the
  Dusk spire (a flat black slab over the mid lane) became a Dusk barbican.
- Foliage check (`-ArenaFoliageCheck`): Arena floating=0 buried=0 (12 664 checked), Conquest floating=0 buried=0
  (9 358; 46 edge tufts under the mid medallion's rim were found and are no longer placed there).

## Camera

The camera dropped towards the ground when the spring arm hit a wall behind the hero (the arm shortened towards
the pivot at the hero's feet). The pivot is raised (`TargetOffset` 90 cm, `SocketOffset` 70 cm to the side): the
arm now shortens towards the shoulder. `-ArenaBaseLab` checks the camera's lift against a wall (>60 cm).

## Movement: "the animations while moving must not be stiff"

`-ArenaLocoLab` (new): each hero runs a player's input on the Conquest mid lane — idle, forward, a strafe, backwards,
a release to a stop, then a bot's own path — logging the anim blueprint's state machines and its variables once per
phase and taking side-camera screenshots. It showed Greystone strafing and backpedalling in the **forward jog**
(a moonwalk) with `Yaw = 0` and `Character = null`.
`-ArenaDumpAnimBP=<path>` (in the same lab) printed the blueprint's graphs: the Paragon anim blueprints play a
forward jog only (`Run` = `JogFwdSlopeLean` leaned by `YawDelta`); the directional blend space in the packs is
unused. Fix (ADR-14): heroes turn to where they run (720 °/s, the pack's lean into the turn), a cast turns the hero
to its aim for the cast (1080 °/s), the aim offset turns head and chest to the camera. After the fix the lab's
strafe phase ends at yaw −90° and the back phase at 180° with the jog matching the motion (LOCO_Greystone_back,
LOCO_Sparrow_strafe).

## Balance (the operator: ranged far too strong)

### The duel lab and its own bug
`-ArenaDuelLab`: every melee hero against every ranged one (6 × 5 × 2 sides = 60 duels), hard bots without their
stat edge, level 9 with ranks, 15 m apart. Rounds r2–r12 were skewed: the duel ground was inside base B, so the
ranged bot (team A) believed it stood at the enemy fountain and only backed off, and the melee bot counted as "safe"
in its base and was never shot (Morigesh walked 5 m towards Kwang without a shot). Both bases are moved far away once
the navmesh is up (NavReady looks for the navmesh at the bases — moving them before it hung the lab at 2 fps).

### Bot fixes the traces found
| # | Finding (duel trace) | Fix |
|---|---|---|
| 1 | Countess opened every duel with her leaping execution from 15 m (the hold rule was in one branch only) | the leap ultimate waits for a target under 60 % (or the bot under 35 %) in the slot loop too |
| 2 | An assassin hovered 2.5–2.9 m from a target backing off at the same pace, just past its 2.3 m blade | a bot's swing starts up to 55 cm past the blade (the swing's lunge covers up to 70 cm) |
| 3 | A retreating bot (under 25 %) never shot back: Revenant stood next to Kwang for 4 s without a shot | a retreating hero still fires its basic or a ready ability at the nearest enemy hero in reach (MOBA players shoot while they flee); spec `Arena.Bot` "hits back … while it retreats" |
| 4 | Tank ultimates opened fights from 17–18 m with 1.2–1.6 s stuns (Greystone, Kwang) | melee reach (7 m + radius), shorter stuns |

### Rounds (melee win rate; 60 duels a round, 120 when two rounds are pooled)
| Round | Lab | Melee win rate | What changed before it |
|---|---|---|---|
| r2–r4 | skewed | 42 / 46 / 41 % | ranged basic ranges 22–30 m → 15–18 m; fire slow on the ranged hero; melee lifesteal; hard bots' stat edge |
| r5–r12 | skewed | 63–75 % | melee health, armour and speed up, assassin leaps, tank ultimates' reach (read through the skew: overshot) |
| r13 | **fixed** | 45 % | the lab's base skew removed: the true picture — Sparrow 12/12, the assassins 2/10 |
| r14 | fixed | 57 % | Sparrow and Gideon trimmed, the assassins' leaps slow, Morigesh and Iggy health |
| r15 | fixed | 62 % | the bot fixes (swing reach, execution hold) and heroes facing where they run |
| r16–r19 | fixed | 71 %, 62 % | ranged buffs; tank shields down |
| r20–r22 | fixed | 58 %, 57 % | Revenant and Iggy kite tools; the lab found deterministic (r21 = r20) |
| r24 | fixed, 3 seeds | 64 % (62 / 68 / 62) | ranged survivability; from here each result pools 180 duels (-DuelSeed) |
| **r25** | fixed, 3 seeds | **58 %** (56 / 60 / 57) | Crunch and Sevarog trimmed; Morigesh, Revenant, Gideon up — every ranged hero 36–64 %, every melee 33–73 % |
| r26 | fixed, 3 seeds | 57 % (48 / 62 / 60) | Sevarog health, Greystone's and Khaimera's basic |
| r27 | fixed, 3 seeds | 57 % | Sparrow's basic and rain trimmed; Revenant and Morigesh health, Morigesh's swarm slows more |
| **r28 (final)** | fixed, 3 seeds | **58 %** (63 / 58 / —) | Sparrow's attack-speed buff 0.5→0.4; Revenant, Iggy and Morigesh fire a little faster |

Final per hero (r28, 180 duels): ranged — Sparrow 56 %, Iggy 47 %, Gideon 42 %, Morigesh 39 %, Revenant 28 %;
melee — Crunch 70 %, Sevarog 67 %, Khaimera 57 %, Greystone 57 %, Kwang 53 %, Countess 43 %. One hero's rate moves
by up to ±15 % between rounds even with 180 duels (each fight shifts the dice of the next ones), so the tuning
followed the averages of r24–r28, not a single round. Revenant stays the weakest 1v1 against melee (no escape or
peel in his kit; his damage is capped by VR-07); in the 5v5 matches he trades normally.

### Numbers changed (heroes.json, against the v16 commit)
- **Greystone**: maxHealth 620→640; moveSpeed 6→6.2; Sword Strike: damage 38→35; Annihilation: range 12→7; Annihilation: radius 6→4.5; Annihilation: stunSeconds 1.2→1
- **Countess**: maxHealth 520→645; armor 21→32; moveSpeed 6.6→6.7; Blood Blade: range 2.3→2.5; Blood Blade: damage 30→38; Dark Leap: damage 75→90; Dark Leap: slowSeconds —→1.6; Dark Leap: slowPct —→0.35; Dark Leap: cooldown 8→7; Shadow Daggers: damage 55→52; Bloody Execution: endDamage 220→235; Bloody Execution: stunSeconds 0.5→0.6
- **Gideon**: maxHealth 430→450; Cosmic Bolt: range 25→16; Black Hole: stunSeconds 1.5→1.2
- **Sparrow**: maxHealth 450→430; Arrow: range 30→16.5; Arrow: damage 25→21; Arrow: cooldown 0.55→0.62; Explosive Arrow: knockback 5→4; Backstep: cooldown 9→11; Inner Fire: attackSpeedBuffPct 0.5→0.4; Rain of Arrows: damage 240→220
- **Kwang**: maxHealth 600→605; moveSpeed 6.1→6.25; Jade Sword: damage 36→38; Guardian's Shield: shield 190→150; Wrath of Heaven: range 10→7; Wrath of Heaven: radius 7→5; Wrath of Heaven: stunSeconds 1.6→1.2
- **Crunch**: maxHealth 630→650; armor 28→34; moveSpeed 6.2→6.3; Rocket Fists: damage 31→32; Hak: stunSeconds 0.7→0.55
- **IggyScorch**: maxHealth 440→535; armor 15→20; Fire Bolt: speed 34→52; Fire Bolt: range 24→16; Fire Bolt: damage 36→40; Fire Bolt: cooldown 1.05→0.98; Molotov Cocktail: damage 115→125; Molotov Cocktail: slowPct 0.3→0.4; Oil Slick: speedBuffPct 0.45→0.6; Fire Turret: range 10→14; Fire Breath: speed 24→34
- **Khaimera**: maxHealth 540→675; armor 22→30; moveSpeed 6.6→6.7; Claws: range 2.3→2.5; Claws: damage 29→41; Rending Leap: damage 40→72; Rending Leap: slowSeconds —→1.6; Rending Leap: slowPct —→0.35; Unleashed Fury: endDamage 160→138
- **Morigesh**: maxHealth 420→515; Daggers: speed 34→40; Daggers: range 22→15; Daggers: damage 34→38; Daggers: cooldown 1.1→1.02; Doll's Curse: damage 105→115; Swarm: slowPct 0.35→0.45; Life Drain: heal 90→120
- **Revenant**: maxHealth 450→570; armor 16→24; Revolver: range 28→17; Revolver: damage 24→32; Revolver: cooldown 0.7→0.68; Mark: stunSeconds 0.6→1; Obliteration: damage 95→100; Obliteration: knockback 3→5; Reload: speedBuffPct 0.1→0.3
- **Sevarog**: moveSpeed 6→6.25; Soul Hammer: damage 36→34; Subjugate: range 8→6; Subjugate: stunSeconds 0.8→0.55; Zryw: shield 110→90
- **tower**: scale 0.62→0.95; capsuleRadius 190→240; capsuleHalfHeight 360→470; meshZ -360→-470; muzzleZ 400→693
- **inhibitor**: scale 0.6→0.9; capsuleRadius 190→240; capsuleHalfHeight 390→500; meshZ -390→-500; muzzleZ 420→715
- **core**: scale 0.85→1.15; capsuleRadius 200→250; capsuleHalfHeight 400→500; meshZ -400→-500; muzzleZ 430→620; staticScale 0.75→1

`Arena.Balance` (VR-07 time to kill by role at levels 5 / 10 / 15, and the kite model) passes after the round.

## Bots ("far too weak even on hard")

Hard: +15 % damage and +12 % health (the stat edge; allied bots stay normal), aim error 25 cm, reaction 0.12 s,
always side-steps shots and telegraphed areas; normal 110 cm / 0.3 s, dodges half the time. Ranged bots kite a
melee hero within 5.5 m, melee bots stay at contact and circle; plus the fixes above (the swing reach, the
execution hold, shooting while retreating). Conquest: sieges when the team outnumbers the defenders, the open core is
attacked when at most one defender is alive.

Conquest bot matches on the new map (12 min, final numbers):

| Matches | Result | Melee heroes (k / d / damage) | Ranged heroes (k / d / damage) |
|---|---|---|---|
| start of v17 (Arena map, first changes) | — | 2.2 / 4.3 | 4.6 / 2.6 |
| 3 × equal bots (seeds 2, 5, 9) | 0 stuck; points limit or timeout | 3.6 / **5.3** / 11.4k | 7.2 / **5.7** / 16.0k |
| 2 × hard (A) vs normal (B) (seeds 3, 7) | **hard won both** (360:297, 360:202); 1 stuck episode | 4.8 / 6.7 / 12.1k | 7.0 / 5.4 / 15.3k |

The melee heroes now die less often than the ranged ones (they died 1.7× as often at the start); the ranged heroes
still take more kills and deal more damage from the back line while the melee ones soak (taken damage 16–24k) —
the roles of a MOBA team. The regression's 20-minute Conquest match ended by the core at 15:25 (boss taken, 10 of 20
structures down, 0 stuck).

## Resolutions, performance and smoothness

- The UI at 1600×900, 1280×720 and 1920×820 (21:9): every screen fits (the demo's screenshots at all three).
- The heroes' pace: 0 stuck in the Arena and 3 equal Conquest matches, 1 episode in 2 hard-vs-normal ones; yaw snaps 0
  in every headless match (a cast's turn at 1080 °/s is counted as a snap only past the frame's own reach).
- The horizon cliffs and the landmarks cast no shadows and have no collision; the structures stay 22 actors.
- Editor frame hitches of 1.7–2.8 s in the rendered tour are first-use shader and Niagara compiles ("Preparing
  Shaders") in the uncooked editor; the packaged game's numbers are below.
- Regression (`regress_v17.sh`): tests 45/45; Arena bot matches seeds 2 and 7 stuck 0; Conquest 20 min stuck 0;
  foliage 0/0 on both maps; AnimLab 71, BaseLab 14, MechLab 67, AimLab 14, SkillLab 42, FxLab 102, ConquestLab 22,
  UIDemo, training 4 — every check passed.

## Open

- The duel lab's editor process can crash at shutdown inside the Python plugin (after all duels are logged); the
  packaged game has no Python plugin.
- The duel lab can still crash inside GameplayAbilities when it spawns the next pair right after a Gideon–Crunch duel
  deep into a run (r23: the 8th spawn of a chunk; r5: at a GC after those duels). Six Gideon–Crunch duels in a row
  alone do not crash, and no bot match (Arena or Conquest, 12–20 min, dozens of deaths and respawns) has; the engine's
  plugin symbols are not installed, so the frame is unnamed. The lab runs in two chunks; one duel of 180 was lost.
- The side that won the three equal Conquest matches was team B each time (the regression's match: team A) — a sample
  to watch for a side bias on the mirrored map.
- Revenant remains the weakest hero one on one against melee (28 % in r28).

## Packaged game (Shipping, 2026-09-29)

| Finding (how) | Fix |
|---|---|
| The cook's 8 shader workers left the CPU at 71 % (16 threads): ~5 packages a minute in the shader-bound tail | restarted with 12 workers (`NumUnusedShaderCompilingThreads=4`): 93 % CPU, ~20–30 packages a minute. The legacy iterative cook (no Zen store) wiped the cooked folder, but everything computed was in the DDC: 10 233 packages in 57 min, 0 storage errors (only the known Paragon `*PlayerCharacter` blueprints) |
| D: had 4.6 GB after the cook, the stage needs 13 GB | staged to C: (`-stagingdirectory`), the cooked folder deleted, the build moved to `Build/Windows` (robocopy); the DDC kept (the next cook reuses it) |
| **The Conquest map never loaded in the exe**: the tour on `Conquest` showed the Arena. A Shipping build ignores a map on its command line (`UE_ALLOW_MAP_OVERRIDE_IN_SHIPPING` is off), and the game's own travels used short map names (`Conquest`, `Training`, `Arena`), which the packaged IoStore game does not resolve reliably | every travel goes by the full path (`/Game/Maps/<name>`); `-ArenaStartMap=<Map>` travels in game with the menu's own `OpenLevel`, for the exe's tests. Code only: the Shipping exe rebuilt and swapped into `Build/Windows` — no new cook (the quick patch path) |

Results (1600×900, High, GameUserSettings.ini backed up and restored byte for byte):

| Run | avg fps | 1 % low | worst frame | hitches |
|---|---|---|---|---|
| UIDemo, Arena (seed 3) | **107.1** | 89.4 | 40.6 ms | 0 |
| Conquest bot match, hero camera (seed 5) | **89.4** | 51.1 | 23.3 ms | 0 |
| Conquest with the whole-map tour cameras (seed 4) | 67.4 | 42.0 | 31.3 ms | 0 |

The v16 exe's UIDemo gave 137.8 fps with a 132 ms hitch; its match had a third of v17's fights (90:27 against 90:84),
so the averages are not comparable, but no v17 run had a hitch. The hero select shows the portraits (the icon studio
finishes in ~1 s in the exe; in the editor it waits for its shaders); the floors, the Barlow fonts, the cliff ring and
the Paragon-scale towers look as in the editor. An A/B of the cliff ring in the editor (`-ArenaNoHorizon`) was
within the editor's run-to-run noise (44–64 fps for the same run).
