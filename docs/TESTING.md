# Testing and verification

Tartarus Arena is verified the Dark Factory way: no feature counts as done on a claim, only on evidence the game
produces by itself. This page lists every way to get that evidence: the automation specs, the in-game labs, the
seeded bot matches, the demos and screenshot tours, and the command-line switches behind them. The per-version results
are in the QA reports in [dark-factory/](dark-factory/).

## How the switches work

- **Flags** are passed as `-Name` and **values** as `-Name=value` (quote a value with spaces). UE matches a value's
  name anywhere in the command line, so `Seed=` also matches inside `-DuelSeed=N`: give `-Seed` before `-DuelSeed`, or
  leave it out.
- **Maps:** Arena (`/Game/Maps/Arena`) is the default; the others are Conquest, Training and Proto. In the editor build
  pass the map before `-game`. A packaged Shipping build ignores a map on its command line: use `-ArenaStartMap=<Map>`.
- **Output:** screenshots go to `Saved/Screenshots/<platform>/`. A Shipping build writes no log, so modes that matter
  for the packaged game also write `.txt` files to `Saved/` (`%LOCALAPPDATA%\ParagonArena\Saved` in a packaged build).
- **Labs** are mutually exclusive (the first one on the command line, in the order of the table below, wins). Each
  prints `LAB PASS <check>` / `LAB FAIL <check>` lines and a final `LAB_SUMMARY fails=N`, then quits.
- **Headless:** most checks run with `-nullrhi` (no window, no GPU). With `-benchmark -fps=60` the game steps a fixed
  1/60 s per frame, so a bot match of several game minutes takes less than a minute.

Typical editor-build run:
`UnrealEditor-Cmd.exe <uproject> /Game/Maps/Arena -game -nullrhi -benchmark -fps=60 -unattended -ArenaSkillLab`

## Automation tests

`UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests Arena;Quit" -unattended -nullrhi -nosplash -NoSound -ReportExportPath=<dir>`

Six specs, 50 tests. A run of the `Arena` filter reports 51, because it also picks up one engine test whose name
contains the word (`RigLogic.RigLogicLib.ArenaMemoryResourceTest`). They run in the editor build (`EditorContext`),
not in the packaged game.

| Spec | Covers |
|---|---|
| `Arena.Core` | the pure rules: damage, armour, shields and crits (VR-01), friendly fire (VR-04), the hit registry (VR-03), team score, time and ties (VR-05), items, shop, recipes and revive (VR-12), XP, ranks and deterministic crits (VR-20), shutdown gold (VR-23), respawn time and the cast gate (VR-06, VR-02) |
| `Arena.Data` | `heroes.json` parses, every asset it names exists, a negative number is rejected (a decoy that must fail) |
| `Arena.Bot` | the bot brain's decisions: retreating, not chasing healthy heroes, saving stuns, no tower dives, falling back when outnumbered, ranges, ultimates, the orb, melee contact, minion targets and marching |
| `Arena.BotSmarts` | leading shots by difficulty, placing area ultimates, focusing a weak hero |
| `Arena.Conquest` | the protection chain, tower targeting and ramp-up, damage to structures, lane polylines, the timeout winner, the Conquest data |
| `Arena.Balance` | time-to-kill per role band at levels 5, 10 and 15 (VR-07); melee heroes catch ranged ones |

## In-game labs and checks

| Switch | Map | What it checks / outputs |
|---|---|---|
| `-ArenaAnimLab` | Arena | every hero and both minion types lined up: intro, hit, stun, knock, death, drop-in, victory; nobody stands on a unit's head, props are not flung. `LAB_00_Intro` … `LAB_11_Victory.png` (screenshots need a rendered run) |
| `-ArenaMechLab` | Arena | each hero's ability indicators (1–4) against the data, then each cast lands where its indicator showed; swings and dashes stop at walls; melee bots reach body contact without overlapping. `MECH_*.png` |
| `-ArenaFoliageCheck` | Arena / Conquest | traces every scattered plant, stone and tree against the ground: `FOLIAGE_SUMMARY floating=N buried=M checked=K` |
| `-ArenaConquestLab` | Conquest | Conquest end to end: structures on the navmesh, the protection chain, structure damage, tower targeting and ramp, a tower falling, a camp (aggro, leash, buff), the boss, the core ending the match. `CQ_*.png` |
| `-ArenaDuelLab` | Arena | balance measurement: every ranged hero against every melee hero 1v1 (hard bots, level 9, sides swapped). `DUEL`, `DUEL_HERO`, `DUEL_SUMMARY` (melee win rate). Options below |
| `-ArenaLocoLab` | Conquest | each hero (or `-LocoHero=Id`) plays idle, forward, strafe, back, stop, relax, then a bot path: `LOCO_STATE`, `LOCO_VARS`, `LOCO_<hero>_<phase>.png` |
| `-ArenaSkinDeathLab` | Arena | every hero in every skin dies, is destroyed, then garbage collection runs: `SKIN_DEATH_SUMMARY bodies=N crashes=0` |
| `-ArenaFxLab` | Arena | every ability of every hero cast at a dummy before a side camera: `FX_<hero>_<slot>.png` at impact, `FXLAB` damage and knockback lines; a ramp shot up and down; a knockback into a wall |
| `-ArenaBaseLab` | Arena | sustain (VR-23): recall and what interrupts it, the fountain (heals allies, burns enemies), potions. `BASE_Recall.png`, `BASE_CamWall.png` |
| `-ArenaSkillLab` | Arena | live units against `heroes.json`: ranks 1 vs 5, the ultimate's telegraph and knock-up, the XP curve, item passives, shot width, the tooltip, the ability bar and the recipe shop. `SKILL_*.png` |
| `-ArenaAimLab` | Arena | aim assistance through the real player controller: 20 shots without and 20 with assist at a strafing hero, hit markers, the hit flash, melee turn and step-in. `AIM_*.png` |
| `-ArenaTrainingDemo` | Training (`?Hero=N`) | the training room: casts with cooldowns off, the DPS meter, level 20, dummies mending. `TRAIN_*.png` |
| `-ArenaNetHost` / `-ArenaNetGuest` | Arena, two instances | LAN: the host reopens Arena as a listen server and waits for a guest; the guest joins 127.0.0.1, picks a hero, fights; both run replication checks. Run alone (two instances need the memory) |
| `-ArenaNavCheck` | Arena / Conquest | at the start of a match (add it to `-ArenaBotMatch`; alone, the game waits in its menu): paths from base to base, the orb and every lane point: `ARENA evt=navcheck points=N unreachable=M` |
| `-ProtoLab` | Proto | prototype 1: run, sprint, sneak, jumps, mantle, wall jump, dodge, light combo, heavy and launch with air slashes, perfect dodge and counter, camera-aimed swings, hit reactions, air slam, lock-on strafe, the bot's fight and sight. Also appended to `Saved/ProtoLab.txt` |
| `-ProtoLab -ProtoHades` | Proto | prototype 2 (Hades controls): the fixed camera, WASD on the screen, the dash, cursor attacks, the cast, the smash, the call, the camera cutaway, the bot |

## Bot matches, demos and screenshot tours

| Switch | Map | What it does / outputs |
|---|---|---|
| `-ArenaBotMatch` | Arena / Conquest | ten bots play a match; at the end `ARENA_SUMMARY` (stuck bots, casts, turn snaps), `ARENA_HERO`, `ARENA_PERF`, `DEATH_AUDIT` (deaths by cause), `ARENA_BOTS` (jumps), `CONQUEST_SUMMARY`, and `PASS/FAIL VR-09` (all four abilities cast), `VR-08` (stuck over 3 s), `GS-08` (ten distinct heroes) |
| `-Minutes=<n>` · `-Seed=<n>` | bot matches | match length (default 3) · the seed for the draft, the heroes, the skins and the bots |
| `-Difficulty=<0-2>` · `-DiffA=<n>` · `-DiffB=<n>` | Arena modes | bot difficulty, or per team in a bot match |
| `-ArenaConquest` | Arena | Conquest rules (on the Conquest map they are always on) |
| `-ArenaStartMap=<Map>` | default map | travels to `/Game/Maps/<Map>` (how a Shipping build reaches the other maps) |
| `-ArenaUIDemo` | Arena | tours the front end and a match: `UI_00_Menu`, `UI_08_Play`, `UI_09_Heroes`, `UI_10_Settings`, `UI_01_HeroSelect`, `UI_02_Match`, `UI_03_Shop`, `UI_04_Scoreboard`, `UI_07_Aim`, `UI_05_Death`, `UI_06_End.png`, plus a `UI-PAUSE` check of the pause menu's clicks |
| `-ArenaShots` (+ `-ArenaTour`) | a match | a screenshot every 15 s (cycling the map's `Tour` cameras) |
| `-ArenaCamShots=<file>` | any | one `CAM_<name>.png` per line `name x y z pitch yaw [fov]` of the file |
| `-ArenaIconExport` | any | exports the icon studio's portraits, ability icons and minimap to `Saved/Icons/` (needs rendering) |
| `-ProtoDuel` · `-ProtoDiff=<0-2>` · `-ProtoUIShots` | Proto | a duel at once · the prototype bot's difficulty · a tour of the prototype's menus |

Every match that ends also appends a line to `Saved/ArenaPerf.txt`: seed, duration, frames, average and 1 % low FPS,
95th percentile and worst frame time, hitches, the winner and the score.

## The teaser director

`-ArenaTeaser=<scene>` turns the running game into a film crew: a camera that finds the action by itself, the HUD
hidden, a fixed 1/30 s step, `MovieFrameNNNNN.png` frames, and `Saved/Teaser.txt` (`REC_START` / `REC_END`). Scenes:
`aerial`, `fight`, `follow`, `hero`, `tower` (Conquest), and `duel` / `hades` on Proto. Options: `-TeaserSeconds`,
`-TeaserPreroll`, `-TeaserWait`, `-TeaserOrbit=cm` / `-TeaserHeight=cm` (the fight orbit), `-TeaserSlomo=X` (a smooth
slow motion) and `-TeaserBots` (Proto: two bots). The pipeline from frames to a finished teaser is in
[Tools/Teaser/](../Tools/Teaser/) and in QA report 24.

## Debugging and A/B switches

| Switch | What it does |
|---|---|
| `-EvidenceJournal=<id>` | writes every game log line as JSON with `requestId=<id>` to `Saved/Evidence/<id>.jsonl`, also in the Shipping build, which has no log ([ARGUS.md](ARGUS.md)) |
| `-ArenaCmdAt=T:cmd\|T:cmd` | runs console commands at world times, e.g. `60:profilegpu\|90:stat unit` |
| `-ArenaNoHorizon` | hides the far cliff ring (a rendering-cost A/B) |
| `-ArenaNoFadeMats` | heroes keep the packs' materials, without the fade copies |
| `-ArenaSnapTurn` | bots snap-turn (compare `ARENA_SUMMARY yaw_snaps`) |
| `-ArenaAnimDebug` | each character logs its animation state every 4 s |
| `-ArenaDumpAnimBP=/Game/...` | with `-ArenaLocoLab` in the editor: dumps an animation blueprint's graphs, nodes and links |
| `-DuelOnly=<Ranged>-<Melee>` · `-DuelRepeat` · `-DuelSeed` · `-DuelStart` · `-DuelCount` · `-DuelTrace` | the duel lab: one matchup, repeats, seed, range, a per-second trace |

## The game as a system under test

[`Tools/ArgusSUT/server.mjs`](../Tools/ArgusSUT/server.mjs) starts the packaged game's headless checks over HTTP and
reports what the game's evidence journal says, for an external tester such as Argus. `selftest.mjs` checks its
contract and `sweep.mjs` runs every check once. The contract, the safety rules and the Argus onboarding are in
[ARGUS.md](ARGUS.md).

## The packaged game

Each release is checked in the packaged Shipping build too, not only in the editor: the prototype labs headless
(`ParagonArena.exe -nullrhi -ArenaStartMap=Proto -ProtoLab`, results in `Saved/ProtoLab.txt`), a headless Conquest
bot match, and a rendered `-ArenaUIDemo` whose screenshots are inspected. Rendered runs open a window on the
operator's desktop, so they wait for the operator's go-ahead, and the operator's `GameUserSettings.ini` is backed up
and restored byte for byte around every run.
