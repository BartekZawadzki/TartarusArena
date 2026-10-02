# 24 — QA: the English game and its first teaser

Scope (operator 2026-10-01): "change everything in the game from Polish to English and update the exe", then "carry on,
and once it's in English run the game and record the best scenes for a teaser you make with your motion-designer
skills"; during the recording: "record only from the Paragon game" (the main game, not the prototypes).

## 1. The English game

Every player-visible string is English: the HUD, menus, settings, tutorial, death recap, Conquest, Training, both
prototypes, the map signs (translated at run time: `UI/ArenaSigns.h`, both game modes) and `Content/Data/heroes.json`
(~280 strings; classes Warrior / Assassin / Mage / Archer / Marksman / Guardian). A glossary of the terms was kept as a working file outside the repository.
The exe got the change without a re-cook: a new Shipping exe, and the base pak's UFS part rebuilt with the English
`heroes.json`. **A patch pak did not work:** `ParagonArena-Windows_1_P.pak` (data only) was valid (`UnrealPak -List`)
but never read — with IoStore on, a `.pak` without a matching `.utoc` is not mounted unless `pak.AllowMissingUToc` is
set (`IPlatformFilePak.cpp`, `MountFailsOnMissingUToc`); the first headless labs passed because they do not show the
data's text. The rendered UI demo caught it (Polish hero descriptions in the hero browser). Fix: `UnrealPak -Extract`
the base `ParagonArena-Windows.pak` (1 792 files, mount `../../../`), swap `ParagonArena/Content/Data/heroes.json`,
repack with the same mount point (uncompressed: 35 MB instead of 12.8 MB); the original pak and the unused patch pak are
kept in `Build/Backup_20261001`.

| Check | Result |
|---|---|
| exe rendered UI demo (`-ArenaUIDemo`, 1920×1080 window): main menu, Play, Heroes browser, Settings, hero select, match HUD, shop, scoreboard, death recap, end screen | 11 screenshots, all English (e.g. Kwang "Guardian · Warrior", "Jade Sword", "Wrath of Heaven", shop items, "KILLED BY") |
| exe, prototype 1 lab (`-nullrhi -ArenaStartMap=Proto -ProtoLab`) with the rebuilt pak | 26 / 26 PASS |
| exe, prototype 2 lab (`... -ProtoHades`) | 16 / 16 PASS (before the pak rebuild) |
| exe rendered teaser runs: Arena and Conquest load from `-ArenaStartMap`, 13 takes, no crash | 13 / 13 exit 0 |
| editor + Shipping targets build | Succeeded |
| editor tests `Automation RunTests Arena` | 50 / 51 — the one failure is `Arena.Data` reading the DDC on drive D:, whose filesystem needs a repair (NTFS "Full Repair Needed"); not the code |
| GameUserSettings.ini after every exe run | restored, the same SHA-256 as before |

## 2. The teaser director (`Game/ArenaTeaser.h/.cpp`)

`-ArenaTeaser=<scene>` spawns a film camera that finds the action by itself and records it frame by frame
(`GIsDumpingMovie`, `Saved/Screenshots/Windows/MovieFrameNNNNN.png`), the HUD hidden; the game quits when the clip is
done. Options: `-TeaserSeconds=N` (seconds of film, 30 frames each), `-TeaserPreroll=N` (game seconds before it may
record), `-TeaserWait=N` (the latest start), `-TeaserSlomo=X` (the game slowed while filming: a smooth slow motion,
the camera at full speed). The film clock is fixed at 1/30 s per frame by the director itself (`FApp::SetUseFixedTimeStep`)
— `-benchmark -fps=30` alone did not hold in the packaged game (51 frames for a 12 s clip); the preroll renders at 30 %
screen percentage. A line per take goes to `Saved/Teaser.txt` (REC_START / REC_END ticks, real seconds, heat).

| Scene | The camera |
|---|---|
| `aerial` | a slow eased flight over the map's bounds (from the units and structures) |
| `fight` | an orbit (10 m, 3.4 m up) around the hottest fight — the hero with the most heroes of both sides within 14 m — locked on that fight for the take (its star, or the hero nearest when it falls) |
| `follow` | behind and beside the star of the fight, lagging |
| `hero` | low and close (4.8 m), the side with a clear line to the lens over the next 40° of the orbit, the face preferred; slow orbit |
| `tower` | Conquest: an orbit around the structure with the most enemies within 15 m |
| `duel`, `hades` | the prototypes (not used for this teaser) |

The lens stays out of walls: a trace from 1.5 m off the subject (5 m for a tower — its own mesh is not a wall) to the
camera pulls it in. Lessons from the takes: an orbit that re-picks the hottest spot every frame swung to another fight
mid-take (now locked); a low hero camera without the side check filmed walls; a tower orbit traced from the tower's
centre hugged its mesh.

Run (a window 1920×1080 on the desktop, the operator's GameUserSettings.ini backed up and restored exactly):
`Tools/Teaser/run_scene.ps1 -Name fight -Take fight_a -GameArgs "-ArenaBotMatch -Minutes=20 -Seed=7 -TeaserSeconds=12 -TeaserPreroll=30"`.

## 3. The teaser (`Tools/Teaser/`)

| Step | Tool | Output |
|---|---|---|
| 13 takes (Arena 5v5 bot matches and Conquest; seeds 3–33) | `run_scene.ps1`, `batch*.ps1` | 3 600+ frames |
| the best moments | `score.py` — per frame motion (YDIF) + effect colour (SATAVG), smoothed; then contact sheets by eye | in-points |
| the soundtrack | `music.py` — synthesised from scratch in numpy (no samples): D-minor trailer cue at 120 BPM, drone, braams, taiko, ostinato, string pad, risers, reverb, master EQ | 55 s WAV, −16 LUFS |
| the title cards | `titles.py` — Blender 5.2 Eevee: Cinzel in 3D gold, the font's own kerning (one text → mesh → one object per letter), letters flipping up in turn with a back-ease, push-in, a light sweeping the metal | 4 RGBA sequences |
| the edit | `edit.py` — Blender sequencer: cuts on the bars, white flashes on the hits, punch-ins with shake, a slow push on every shot, blurred gameplay under the cards, glow, grade (teal shadows, warm highlights, S-curve, +12 % saturation), vignette, 2.39:1 bars sliding in, English captions | PNG frames |
| the master | ffmpeg: H.264 CRF 17, yuv420p, AAC 320k | `Build/Teaser/ParagonArena_Teaser.mp4` |

Structure (55 s): 0–7 the arena from above ("THE ARENA AWAITS") · 7 **11 HEROES** (all names) · 9–17 hero close-ups,
two in slow motion · 17 **5 VS 5** · 19–29 team fights · 29 **CONQUEST** · 31–43 the Conquest map and its jungle fights ·
43–47 a cut on every beat · 47 the **PARAGON ARENA** logo (the working title then; `titles.py` now writes TARTARUS ARENA), "11 HEROES · ARENA · CONQUEST", a fan-project credit line.

**Publishing:** the heroes, the map art and the name "Paragon" belong to Epic Games (the Paragon assets are free to use
in Unreal projects, the trademark is not). The teaser is for the operator's own use; publishing it under this name is a
hard stop (operator's decision, and a rename would be needed).

## 4. Teaser v2 (operator 2026-10-01: "the camera was a bit too high — fix the 5v5 fights and use parts of my two recordings")

- **The fight camera** is set from the command line now: `-TeaserOrbit=<cm>` `-TeaserHeight=<cm above the fight's middle>`
  (look-at 0.7 m above it, FOV 62, a gentle 15 cm breathing). Tried: 10 m / 3.4 m (v1: "too high"), 7.6 m / 1.2 m
  (operator: "too close, you can't see anything" — heroes walked into the lens), **9.5 m / 2 m** (used: about the hero
  size of v1, the view ~8° down instead of ~13.5°). Takes fight_e–fight_h (`batch3.ps1`, fight_g in 0.6× slow motion).
- **The operator's recordings** (two screen recordings, 2558×1438, 30 fps, with the HUD; not in the repository):
  `extract_user.ps1` cuts 13 windows to PNG takes, a centre crop zoomed 1.3× (1968×1106 → 1920×1080, no upscale) so the
  score bar falls off the top and the ability bar and minimap sit under the 2.39:1 bars. Used: the hero roster behind
  "11 HEROES", the line-up before "FIGHT!" behind "5 VS 5", two beast close-ups, the double-kill fight, the lightning
  team fight, the tower's beam strike in Conquest, and five montage beats. Left out: a tower-laser shot (a tutorial hint
  over the picture), a wraith shot (its FIRST BLOOD banner sat just under the bar), a shot with an ability tooltip and
  a spinning camera. Their sound is not used (a screen recording may hold other system sounds).
- **The cut** is `edl_v2.py` (`edit.py --edl edl_v2.py`), the same soundtrack and bars as v1.
- Output: `Build/Teaser/ParagonArena_Teaser_v2.mp4` (154.5 MB, CRF 17) and `_v2_web.mp4` (52.1 MB).

## 5. Teaser v3 (operator 2026-10-02: game music in the gameplay, another music on the transitions, more gameplay, calmer, the best-looking shots)

- **Facts checked first:** the game has no music (only the Paragon voice lines and the synthesised hit ticks) and the
  operator's screen recordings are silent (−70 LUFS). Asked; the operator chose **game-style music** composed for the
  gameplay scenes and a different music on the transitions, from the footage we have.
- **The score** (`music_v3.py`, 79.4 s, −14.9 LUFS): under the gameplay a fantasy game theme in D minor at 100 BPM —
  Karplus-Strong harp arpeggios, string pad, an 8-bar melody on a breathy flute (heroes), a horn (5v5), both a whole
  step up with a choir (Conquest), frame drums, shaker, low string spiccato; it fades out just before every card. On
  the cards: dark stingers (reverse swell, braam, taiko, formant choir, bells), the logo's long one resolving to D major.
- **The edit** (`edit3.py`): 79% gameplay (62 of 79 s), shots of 1–2 bars cut on the bar lines, 10-frame dissolves
  inside a section, no flashes inside the gameplay, no shake, no montage, a slow push on every shot; 3-second cards
  (`titles.py --cardframes 90`) with a soft flash and blurred gameplay. Best-looking material: the director's own
  takes (full-quality renders), the operator's recordings only where they shine (a beast close-up, the lightning team
  fight raised 45 px so its FIRST BLOOD banner hides under the bar, the tower's beam strike).
- Output: `Build/Teaser/ParagonArena_Teaser_v3.mp4` (230.9 MB, CRF 17, 79.4 s) and `_v3_web.mp4` (75.7 MB); the score alone `teaser_music_v3.wav`.

## 6. The short teaser (operator 2026-10-02: "a short teaser, about a minute, from my recording, with your
motion-designer skills and the music you just picked" + "weave in your best 5v5 scenes")

- **Source:** the operator's first recording (`extract_short.ps1`: the countdown line-up, the fire ring, the shop
  uncropped, three fights) + the earlier cuts of it (roster, line-up, lightning, double kill, the Tartarus cube) +
  the director's best low 5v5 takes (fight_e, fight_g, fight_h, fight_f, follow_b's rainbow beam).
- **The music** (`music_short.py`, generated from music_v3.py by `make_music_short.js`): the same theme and
  instruments on a 59 s timeline — a harp intro, drum-and-bell ticks on a 3-2-1 countdown with a riser, a stinger on
  FIGHT!, the flute section, a stinger on the Power of Tartarus, the climax a step up with the choir, the logo.
- **Motion design** (`edit_short.py`): kinetic type slamming in with an exponential ease and a glow (PICK YOUR HERO,
  3-2-1 on the drum hits, FIGHT!, POWER OF TARTARUS), the shop as a panel sliding in with a gold frame drawn on
  around it (BUILD YOUR HERO · parts, upgrades, legendaries), a three-way split screen sliding in with gold
  dividers and a band (OUTPLAY YOUR RIVALS), a lower third with a band and a drawn line (TEAM FIGHTS · 5 VS 5 · BOTS
  OR FRIENDS), dissolves inside a section, flashes on the hits, the 3D logo. The game's own countdown and FIGHT!
  banners are hidden (a blur under the countdown, the FIGHT! shot raised 40 px); KHAIMERA: DOUBLE KILL! is kept.
- The shop is open for only the first 72 frames of its window (measured: the mean luma of the panel area 75 vs 100 when closed), so the panel plays it at 0.48x. Output: `Build/Teaser/ParagonArena_Teaser_Short.mp4` (133.3 MB, 59 s) and `_Short_web.mp4` (50.2 MB).

## 7. The repository in English (operator 2026-10-02: "change everything in the ParagonArena repo to English, from start to finish — I am preparing it to show the world as a demo of making a game with Claude and the DF patterns, so everything must be described from that angle")

- **Code:** the last Polish in comments and strings (the mode's old name → Conquest, item and ability names → the
  game's English names, the menu labels) translated; the HUD's class icons and the balance spec no longer accept the
  old Polish class names (the data has been English since 10-01).
- **Maps:** the Training and Proto maps still carried Polish signs (the run-time translation hid them). A first pass
  edited the signs in place from a headless commandlet; the saved Proto map then broke the prototype's bot
  (ProtoLab 23/26: "the bot reaches the player and lands hits: 0"; the backup map: 26/26). Both maps were therefore
  regenerated by their build scripts, which write English signs and labels. `Tools/check_map_text.py` (read-only)
  confirms all four maps: `polish_signs=0 polish_labels=0`. The run-time sign translation (`UI/ArenaSigns.h`) is removed.
- **Docs:** the Polish names quoted in the design and QA documents (abilities, items, menu labels, the mode) mapped to
  today's English names, 120 pairs taken from `heroes.json` before and after the English pass; README, CREDITS and two
  new guides written for the public: [BUILT-WITH-CLAUDE.md](../BUILT-WITH-CLAUDE.md) and [TESTING.md](../TESTING.md).
- **Evidence:** editor build succeeded; automation tests 50/51 (the one failure is `Arena.Data` reading the DDC on the
  damaged drive D:, as before); ProtoLab 26/26 and the Hades lab 16/16 on the regenerated map; `-ArenaTrainingDemo`
  0 fails; a repo-wide scan of every tracked text file finds no Polish.
- **Not changed:** the git history (commit messages before 10-01 are partly Polish; rewriting a pushed history is the
  operator's call). The packaged build in `Build/` predates these source changes; its next cook takes the English maps
  as they are.

## 8. Ready to be public (operator 2026-10-02: "prepare the repo to be public and the information needed about using Epic's Paragon")

- **Epic's terms, checked at the source:** the Fab listing of a Paragon pack (publisher Epic Games: "free to use as you
  like in your Unreal Engine projects", "You may not use the trademark PARAGON to advertise or name your game",
  Standard License, NoAI) and the Fab EULA (redistribution, §6(a) incompatible licenses, §6(b) restrictions, NoAI).
  Written up in [USING-PARAGON-ASSETS.md](../USING-PARAGON-ASSETS.md) with sources.
- **The operator's decisions:** the public name **Tartarus Arena** (everything visible renamed: menus, hero select,
  loading screen, window and project title, the prototypes' back button, the teaser logo script, README and docs; the
  internal `ParagonArena` module stays); the **MIT** license, scoped to the project's own work; a **new repository
  with one initial commit** (the full history stays private).
- **Audit of the tree and of every revision:** no Epic content ever committed, no secrets (the Android file server's
  default token removed from `DefaultEngine.ini`), Python bytecode untracked and ignored; the author's machine paths,
  user name and recording file names replaced by repository-relative paths, `PATH` lookups and parameters
  (`FFMPEG`, `PARAGON_EXE`, `TEASER_REC1/2`).
- Editor build succeeded after the rename.

## 9. The final teaser under the public name (operator 2026-10-02: "fix the final teaser so its logo matches the current, Epic-compliant state")

- The short teaser re-rendered as `Build/Teaser/TartarusArena_Teaser.mp4` (133.2 MB, 59 s; `_web` 50.1 MB): the
  **TARTARUS ARENA** 3D logo (`titles.py`, rendered to `titles_tartarus/logo`), the closing line "Built with Epic Games'
  free Paragon assets · Not affiliated with or endorsed by Epic Games", the file's title and comment metadata to match.
- The opening roster comes from the operator's recording and carries the old working title in its header. The 2.39:1
  bars now stand in place from the first frame (they used to slide in during the fade from black), so the header
  never shows. A contact sheet every 1.5 s of the final file: no old title anywhere.
- The earlier teaser files (`ParagonArena_Teaser*.mp4`) keep the old logo and are not for publishing.
