# Tartarus Arena

**A 5v5 MOBA prototype in Unreal Engine 5.8, built end to end by Claude following the Dark Factory (DF) patterns.**

*Working title during development: "Paragon Arena". It was renamed because Epic's Paragon asset listings say "You may
not use the trademark PARAGON to advertise or name your game". The Unreal project and its C++ module keep the internal
identifier `ParagonArena`.*

Eleven heroes, two game modes on two maps, smart bots, a training center, LAN play, two experimental prototypes of a
new game, and a full verification harness. The C++, the Python tools that build the maps, the data, the tests, the
in-game labs, these docs and the teasers were all written by Claude (Anthropic's Claude Code) over seven days
(2026-09-26 → 2026-10-02). The human operator set the goals, played the builds, gave feedback in plain language and
made the decisions only a human should make. The art is Epic Games' free Paragon content.

> **Not affiliated with Epic Games.** *Paragon* is a trademark of Epic Games, Inc. This is an unofficial,
> non-commercial demo. The Paragon characters, environments, animations and voices are Epic content, used under
> Epic's license for Unreal Engine projects. None of it is redistributed in this repository. What the terms say and
> how the project follows them: [docs/USING-PARAGON-ASSETS.md](docs/USING-PARAGON-ASSETS.md).

| | |
|---|---|
| **How it was built** | [docs/BUILT-WITH-CLAUDE.md](docs/BUILT-WITH-CLAUDE.md): the method, the timeline of 22 versions, the evidence, the lessons |
| **Design and decisions** | [docs/dark-factory/](docs/dark-factory/): game design (vision, rules, scenarios), technical design with 14 ADRs, a QA report per version |
| **Verification** | [docs/TESTING.md](docs/TESTING.md): 6 automation specs, a dozen in-game labs, seeded bot matches, packaged-exe checks |
| **Teasers** | rendered by the game itself ([Tools/Teaser/](Tools/Teaser/)): an in-engine film director, a synthesised score, Blender title cards and edit |
| **Epic's Paragon assets** | [docs/USING-PARAGON-ASSETS.md](docs/USING-PARAGON-ASSETS.md): what is used, the Fab license, the trademark, the "NoAI" flag, how the repo complies |

## At a glance

| | |
|---|---|
| Engine | Unreal Engine 5.8, C++, Gameplay Ability System, Enhanced Input, Chaos |
| Code | ~22,000 lines of C++ in 80 files and ~5,500 lines of Python tooling, all written by Claude |
| Heroes | 11 Paragon heroes (Greystone, Countess, Gideon, Sparrow, Kwang, Crunch, Iggy & Scorch, Khaimera, Morigesh, Revenant, Sevarog), 5 abilities each, 38 skins |
| Modes | **Arena** (5v5, 3v3 Skirmish, 1v1 Duel on the Arena map) and **Conquest** (5v5 on its own map: three lanes, towers, inhibitors, cores, jungle camps, a boss) |
| Maps | 4, all built by Python scripts: Arena, Conquest, Training, Proto |
| Players | single player with bots (three difficulties), LAN multiplayer (listen server, join by IP, bots fill the rest) |
| Prototypes | a new game in a grey-box town: third-person action combat, and the same game with Hades-style controls |
| Verification | automation specs (~50 cases), ~15 self-checking in-game labs, seeded headless bot matches, a packaged-exe check every release |

## What you can play

- **Arena** — a Smite-style 5v5 over-the-shoulder MOBA: team points from kills and minions, a Power of Tartarus orb
  to fight over, levels 1–20 with ability ranks, a 27-item shop with recipes and legendary passives, recall, fountain,
  potions, revive for gold, bounties, a death recap.
- **Conquest** — LoL-style lanes on a big jungle map: towers and inhibitors protecting each core, heavy minions, neutral
  camps with buffs and the Prime Helix boss; the match ends when a core falls.
- **Training center** — a grey room with dummies, a moving target, a ramp, no-cooldown and level-20 switches and a DPS meter.
- **Hero browser** — every hero on a live 3D preview; clicking an ability plays it with its effects.
- **Bots** that read the game: they kite, dodge, jump, focus weak targets, respect towers, back off when outnumbered,
  manage mana, siege when they have the numbers, and play a lane in Conquest.
- **New game · prototype** and **New game 2 · Hades controls** — the same town, characters and combat (combos,
  heavy attacks, perfect dodges, juggles, mantling, wall jumps) with two control schemes, each with its own bot.

Settings: quality presets up to *Epic · Lumen* (Lumen GI and reflections on DirectX 12 SM6), window mode,
resolution, render scale, frame limit, V-sync, volume, mouse, three ability-casting modes (quick / instant / with
confirmation), aim assist, camera shake, damage numbers, field of view, UI scale, a colour-blind mode, key rebinding
and gamepad support.

## Controls (Arena and Conquest)

| | |
|---|---|
| Move · aim · jump | WASD · mouse · Space |
| Basic attack | left mouse button (hold to keep attacking) |
| Abilities | 1–4 (4 is the ultimate); **Ctrl + 1–4** ranks an ability up; right mouse button cancels; **Alt** shows the full tooltip |
| Shop · recall | **B** in base opens the shop, outside it recalls (6 s); **P** opens the shop anywhere (buying works in base or while dead) |
| Potions · revive | **5** health, **6** mana · **F** revives for gold after death |
| Team | **G** pings · **Tab** scoreboard · **Esc** pause menu |
| Gamepad | A jump, RT attack, RB / LB / X / Y abilities, LT + ability ranks up, B shop / recall, Start pause |

The prototypes show their own controls card when a match starts.

## Getting started

### Requirements

- **Unreal Engine 5.8** (Epic Games Launcher) and **Visual Studio 2026** with *Game development with C++*.
- About 60 GB of free disk space for the packs, the derived data and a cook.
- From your Fab library, **added to this project** (launcher → Library → Fab Library → *Add to project* → ParagonArena):
  the Paragon heroes **Greystone, Countess, Gideon, Sparrow, Kwang, Crunch, Iggy & Scorch, Khaimera, Morigesh,
  Revenant, Sevarog**, **Paragon: Minions**, **Paragon: Agora and Monolith** (the environment props) and the
  **Open World Demo Collection** (`KiteDemo`, the nature).
- Optionally the engine's template content (mannequins and prototyping blocks used by the prototypes):
  `powershell -File Tools/setup_placeholders.ps1`

### Build and generate the content

1. Build the editor:
   `"<UE_5.8>\Engine\Build\BatchFiles\Build.bat" ParagonArenaEditor Win64 Development -Project="<repo>\ParagonArena.uproject" -WaitMutex -NoHotReloadFromIDE`
2. Run the content scripts, each with
   `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<repo>\Tools\<script>.py" -unattended -nullrhi -culture=en`
   (the editor's language must be English for the material scripts), in this order:
   `make_materials.py` → `make_fade_materials.py` → `import_env_meshes.py` → `resave_pack_meshes.py` →
   `build_arena.py` → `build_conquest.py` → `build_training.py` → `build_proto.py` → `make_hitreacts.py` →
   `build_manifest.py` → `cap_textures.py`.
   - `make_hitreacts.py` turns the packs' additive hit reactions into regular animations and `make_fade_materials.py`
     makes copies of the character materials that can dither out after death. Both write to `Content/Arena/Generated/`,
     which is derived Epic content and therefore not in the repo.
   - `import_env_meshes.py` imports the project's own rock skirts from `Tools/Blender/out/*.fbx`, generated by
     `Tools/Blender/rock_skirts.py` in Blender 5.2 (`blender -b --factory-startup --python-exit-code 1 -P Tools/Blender/rock_skirts.py -- Tools/Blender/out`).
   - The icons in `Content/Arena/Icons` are already imported (`Tools/import_icons.py` re-imports `Tools/Icons/*.png`).
3. Open `ParagonArena.uproject`, or run the game from the editor build:
   `UnrealEditor.exe <uproject> -game` (Arena), or with `/Game/Maps/Conquest`, `/Game/Maps/Proto` before `-game`.

### Package a Windows build

`"<UE_5.8>\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="<uproject>" -noP4 -platform=Win64 -clientconfig=Shipping -build -cook -stage -pak -iostore -compressed -archive -archivedirectory="<repo>\Build" -IgnoreCookErrors -nocompileeditor`
→ `Build/Windows/ParagonArena.exe`

- `-IgnoreCookErrors`: the packs' sample `*PlayerCharacter` blueprints do not compile in UE 5.8. The game does not use
  them, so they are left out of the build.
- Assets the code reaches only through a path in `heroes.json` must be listed under `DirectoriesToAlwaysCook` in
  `Config/DefaultGame.ini`.
- The Zen store is off (`bUseZenStore=False`): it rewrote a ~15 GB oplog on the system drive and silently dropped
  packages when that drive filled up. A full cook takes about 1 h 40 min and ~27 GB of free space.
- A Shipping build ignores a map given on its command line: use `-ArenaStartMap=Conquest` (or `Proto`, `Training`).
  It also writes no log; its labs and measurements write `.txt` files to `%LOCALAPPDATA%\ParagonArena\Saved`.

## Verification

Every feature lands with evidence. The full catalogue is in [docs/TESTING.md](docs/TESTING.md).

- **Automation tests:** `UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests Arena;Quit" -unattended -nullrhi`
  (rules, data, bot brain, Conquest, balance).
- **In-game labs** (`-ArenaAnimLab`, `-ArenaMechLab`, `-ArenaAimLab`, `-ArenaSkillLab`, `-ArenaFxLab`, `-ArenaBaseLab`,
  `-ArenaConquestLab`, `-ProtoLab`, …): each runs a scripted scenario inside the real game, prints `LAB PASS/FAIL` per
  check and `LAB_SUMMARY fails=N`, and takes screenshots.
- **Seeded bot matches:** `/Game/Maps/Arena -game -nullrhi -ArenaBotMatch -Seed=1 -Minutes=3 -benchmark -fps=60`
  plays ten bots and prints `ARENA_SUMMARY` (stuck bots, casts, turn snaps, jumps, deaths by cause) and validation-rule
  lines such as `PASS VR-09`.

## Repository layout

| Path | What is there |
|---|---|
| `Source/ParagonArena/Core` | pure rules: damage, shields, team points, gold, shop and recipes, ranks and XP, respawn, Conquest rules (unit-tested, no UObjects) |
| `Source/ParagonArena/Data` | the data contract of heroes, abilities, items, structures and camps; loads and validates `Content/Data/heroes.json` |
| `Source/ParagonArena/GAS`, `Abilities` | attributes, and one data-driven GAS ability (melee, projectile, area, dash, buff) with its projectile and area actors |
| `Source/ParagonArena/Heroes` | the character (heroes, minions, structures, monsters): camera, status effects, hit-stop, reactions and deaths from the packs' animations, the melee trait |
| `Source/ParagonArena/AI` | the bot brain (a pure decision function, unit-tested) and the bot controller |
| `Source/ParagonArena/Game` | game mode and state (phases, waves, Conquest, LAN), the player controller, the labs, the teaser director |
| `Source/ParagonArena/UI` | the Canvas HUD and menus, settings, the icon studio (portraits and icons shot from the models in game) |
| `Source/ParagonArena/Arena` | ability indicators, jump pads, the orb, foliage scatter, effects and explosion physics |
| `Source/ParagonArena/Proto` | the new-game prototypes (character, bot, two player controllers, HUD, game mode) |
| `Source/ParagonArena/Tests` | the automation specs |
| `Content/Data/heroes.json` | every hero, ability, item, structure, camp and rule as data |
| `Content/Maps`, `Content/Arena` | the four maps, the project's materials and icons |
| `Tools/` | the map builders and content scripts (Python for the UE editor), Blender scripts, the icon sources, the teaser pipeline |
| `docs/` | how it was built, the DF documents, the testing guide |

## Credits and legal

- **Epic Games:** the Paragon packs (heroes, minions, environment props, animations, effects, voice lines), the Open
  World Demo Collection and the engine's templates. They are Epic content, free for Unreal Engine developers on Fab
  and used under Epic's license. They are not in this repository; each user adds them from their own Fab library.
  *Paragon* is a trademark of Epic Games, Inc.; this project is not affiliated with or endorsed by Epic Games.
- **Icons:** game-icons.net, CC BY 3.0 (authors listed in [CREDITS.md](CREDITS.md)).
- **Fonts:** Barlow, Barlow Condensed, Rajdhani and Cinzel, SIL Open Font License 1.1 (license texts in `Content/Data/Fonts`).
- **Sound and music:** the hit sounds are synthesised in code (`Arena/ArenaHitSound.h`); the teasers' music is
  synthesised from scratch by `Tools/Teaser/music*.py`, with no samples.
- **Code, tools and docs:** written by Claude for the project owner, released under the [MIT License](LICENSE) (scope: [NOTICE.md](NOTICE.md)).
  The license covers only the project's own work. It does not cover Epic Games' content, which stays under Epic's terms
  ([docs/USING-PARAGON-ASSETS.md](docs/USING-PARAGON-ASSETS.md)), or the fonts (OFL) and icons (CC BY 3.0), which keep
  their own licenses ([CREDITS.md](CREDITS.md)).
