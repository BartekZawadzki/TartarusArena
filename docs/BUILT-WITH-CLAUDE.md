# Built with Claude: how Tartarus Arena was made

Tartarus Arena is a case study in building a real game with an AI agent. Over seven days (2026-09-26 → 2026-10-02)
**Claude**, working as the operator's **DF agent** in Claude Code, took a 5v5 MOBA from an empty Unreal Engine 5.8
project to a packaged game with eleven heroes, two modes, smart bots, LAN play, two prototypes of a new game and its
own teasers. It did so in 22 versions, each shipped with evidence. This page explains how: who did what, the Dark
Factory (DF) patterns the work followed, the timeline, the numbers, the engineering lessons, and what is and is not proven.

## Who did what

| The operator (a human) | Claude (the DF agent) |
|---|---|
| set each version's goal in a sentence or two, in plain language (in Polish; quoted in English in the docs) | turned each goal into rules, a design and a test list, then into code, data and tools |
| played the packaged builds and reported what felt wrong ("the camera is too high", "melee heroes are too weak", "bots don't jump") | reproduced each report as a measurable check before fixing it, then proved the fix with that check |
| added the Epic packs to the project from the Fab library (an action only the account owner can take) | wrote every line of C++ (~22,000), the Python tools that build the maps and content (~5,500), the data, the tests and labs, all the docs, and the teasers' director, music, titles and edit |
| decided at the hard stops: deletions, downloads, windows opened on the desktop, publishing | stopped at every hard stop and asked; it never published, spent money, downloaded third-party assets or deleted the operator's data on its own |

Claude also delegated bounded, read-only jobs to helper agents (a blind code review of the network code, research
passes over the docs and the code) and checked their findings before acting on them.

## The Dark Factory patterns, as applied here

The [Dark Factory](https://github.com/OneDro1d/dark-factory) method (*autonomous, governed, evidence-gated delivery*)
treats an AI agent as a factory line with gates: intent becomes a specification, the specification
becomes tests, the work is built against the tests, an independent check tries to break it, and nothing ships without
evidence. In this repository that looks like:

1. **Semantics first, from the product side.** [`01-game-design.md`](dark-factory/01-game-design.md) states the vision,
   three pillars with numeric proxies (e.g. "at least one ability cast per hero every 4 s") and explicit non-goals.
   Every fact is tagged **[C]** confirmed by the operator, **[A]** the agent's assumption, or **[O]** open, so it is
   always clear who decided what.
2. **Rules as the specification.** The same document holds 29 numbered validation rules (VR-01 … VR-31: damage,
   cooldowns, friendly fire, physics caps, aim assist, sustain, Conquest…) and 14 test scenarios (GS-01 … GS-14),
   each a *state → input → expected state*, written before the code that satisfies them.
3. **Formalised design with traceability.** [`02-technical-design.md`](dark-factory/02-technical-design.md) maps every
   rule to where and how it is enforced (a unit spec, a simulation, a log check) and keeps 14 dated ADRs (architecture
   decision records) for the big turns: data-driven GAS abilities, indicators, aim assist, ranks and items, replication,
   LAN, the Paragon locomotion fix.
4. **Pure rules, tested in isolation.** Damage, gold, the shop, ranks, the bot brain and Conquest's protection chain
   are pure functions with no engine objects, covered by six automation specs (17 tests in v1, 51 today).
5. **Evidence the game produces by itself.** A dozen in-game *labs* (`-ArenaSkillLab`, `-ArenaFxLab`, `-ProtoLab`…)
   drive the real game through scripted scenarios and print `LAB PASS/FAIL` per check plus screenshots. Seeded,
   headless all-bot matches report stuck bots, casts, turn snaps, jumps and every death by cause. See [TESTING.md](TESTING.md).
6. **Decoys prove the gates bite.** A check is trusted only after it has been seen to fail: a broken `heroes.json` must
   be rejected, and reverting the pause-menu fix must turn the `UI-PAUSE` check red.
7. **A blind adversary.** For v14–v16 a separate read-only agent, which never saw the implementation notes, reviewed
   the network and Conquest code and found 13 real defects (client-side damage authority, unvalidated RPC indices…),
   all confirmed and fixed ([QA 17](dark-factory/17-qa-v14-v16-conquest-heroes-lan.md)).
8. **Measurement over opinion.** Balance moved in numbered rounds against target bands: 28 duel-lab rounds in v17, and
   four melee rounds in v21 where the first overshot (97 % melee wins) and was walked back to 82 %.
9. **The packaged game is the authority.** Every release is re-checked in the Shipping build, not just the editor:
   that is how the missing Conquest map, the dropped cook packages and a translation patch that never loaded were caught.
10. **Evidence tiers in every verdict.** Each QA report separates T1 (measured), T2 (seen in screenshots or renders)
    and T3 (only a human can judge: fun, feel, taste). T3 is left to the operator, never claimed.
11. **Hard stops.** Publishing, spending money, downloading third-party content, deleting the operator's data and
    opening windows on the operator's desktop all need an explicit go-ahead, one at a time. The operator's game
    settings file is backed up and restored byte for byte around every rendered run.

## Timeline

| Version | Date | The operator's ask (paraphrased) | What was built | Evidence |
|---|---|---|---|---|
| v1 | 09-26 | a playable 5v5 Smite-style arena against bots | GAS abilities, 5 heroes on engine placeholders, minions, team points, HUD, a scripted arena map | 17/17 tests; 2 seeded bot matches, stuck 0; the packaged exe plays a full headless match |
| v2 | 09-26 | replace the placeholders with the Paragon packs | 5 Paragon heroes and the props wired in, an asset manifest, bot movement fixes | manifest 138 assets, 0 missing; 4 bot matches, stuck 0 |
| v3 | 09-27 | a varied map, an icon HUD, locomotion, a shop, revive, points and a clock, performance | the team-points system, a 6-slot shop, revive, the reaction animations | 21/21 tests; AnimLab 28/28; UI demo 124 fps |
| v4 | 09-27 | close the map's holes, show ability range before casting, melee contact | ground indicators for range, area, cone, lane and dash; capsule-edge hits | MechLab 35/35; 5 bot matches stuck 0 |
| v5 | 09-27 | attacks are hard to land and to confirm | aim assist, shot lead, melee stick and lunge, hit markers and flashes | AimLab 14/14; hit rate on a strafing target 5–25 % → 80–85 % with assist |
| v6 | 09-27 | predictable skills, real icons, items that build, levels | levels 1–20, ranks, a 27-item recipe tree, deterministic crits, 52 icons | 26/26 tests; SkillLab 34/34; a 15-min match reaches level 20 |
| v7, v7b | 09-27 | fix every texture and height seam, foliage, recall and sustain | relit map, tiled ramps, 14,798 foliage instances, recall, fountain, potions, death recap | foliage floating 0 buried 0 of 12,897; BaseLab 11/11 |
| v8 | 09-27 | smoother animation, bodies that fall and fade, a better camera, free Blender props | pose-blended deaths, a dither fade, smooth turning, a new camera, Blender rock skirts | AnimLab 40/40; yaw snaps 513 → 0 |
| v9 | 09-27 | the minions from the library, sharp text, a better HUD, abilities that behave | a pixel-accurate HUD, 512 px portraits, the FX lab, two projectile bugs fixed | FxLab 47/47 |
| v10–v11 | 09-28 | a real front end, a training map, team colours, casting modes, the minion army, lighting | menus, a hero browser with live previews, Training, three casting modes with an input buffer, Lumen on SM6 | SkillLab 42/42; packaged exe 122.6 fps |
| v12–v13 | 09-28 | multiplayer foundations, voices; the pause menu did not respond | replicated game state, voice lines, the paused-input fix | 31/31 tests; the UI-PAUSE decoy fails when reverted |
| v14–v16 | 09-28/29 | a LoL-style mode, more heroes, accessibility, smarter bots, LAN | Conquest (towers, inhibitors, cores, camps, a boss), 11 heroes and 38 skins, rebinding and gamepad, LAN host and join | 43/43 tests; ConquestLab 22/22; LAN host 4/4, guest 8/8; blind review: 13 defects fixed |
| v17 | 09-29 | consistent fonts, a bigger Conquest map, the camera at walls, stiff movement, balance | Barlow fonts, a 250 × 160 m Conquest map, orient-to-movement locomotion, 28 balance rounds | 45/45 tests; exe 107 fps (Arena), 89 fps (Conquest) |
| v18 | 09-29 | playtest; bots must "understand the situation" | a death audit by cause, six new bot rules, natural jungle groves | bot deaths 217 → 150 over 4 matches; tower dives −61 % |
| v19 | 09-29 | relaxed idle poses, real fonts, bots that decide better (not aim better) | relaxed-idle blending, Rajdhani and Cinzel, chamfered panels, decision rules | 51/51 tests; bot deaths 150 → 113 |
| v20 | 09-29 | bots don't jump; start a separate prototype of a new game | bot jumping; a grey-box town with sprint, sneak, double jump, mantle, wall jump and a combo system | 188 / 249 bot jumps per match (was 0); ProtoLab 21/21 |
| v21 | 09-29 | melee heroes far tougher; no block or parry in the prototype; mouse-driven combat | a data-driven melee trait; dodge-only defence, perfect dodges and counters, camera-aimed combos | melee win rate tuned to 82 %; ProtoLab 26/26 in the exe |
| v22 | 09-29 | the same prototype with Hades' controls, plus a jump | a fixed top-down camera, cursor attacks, a cast, a dash-strike, a wrath gauge | Hades lab 16/16 in the editor and the exe |
| English | 10-01 | the whole game in English | ~280 data strings and every UI text translated; the packaged data rebuilt | 11 English UI screenshots from the exe; labs 26/26 and 16/16 |
| Teasers | 10-01/02 | record the best scenes and make a teaser; then a lower camera, the operator's own footage, game-style music, a calmer cut, a one-minute motion-design cut | the in-engine film director, a numpy-synthesised score, Blender title cards and edit | four teasers (55 s, 55 s, 79 s, 59 s) |
| Repo in English | 10-02 | everything in English for the public demo | the last Polish comments and data names, the maps' signs regenerated, these docs | tests 50/51 (the one failure is a damaged drive); ProtoLab 26/26, Hades 16/16, Training 0 fails |
| Public release | 10-02 | prepare the repo for the world, the Paragon terms, the teaser for the post | the Fab terms researched at the source, the public name Tartarus Arena, MIT, a one-commit public repository, the final teaser and a media kit in the repo | no Epic content or secrets in any revision; 60 of 60 doc links resolve |

## Numbers at a glance

| | |
|---|---|
| Calendar time | 7 days, 2026-09-26 → 2026-10-02 |
| Versions | v1 … v22 (plus v7b), the English pass, 4 teasers |
| Code | ~22,000 lines of C++ in 80 files; ~5,500 lines of Python in 26 tools |
| Design | 29 validation rules, 14 test scenarios, 14 ADRs, 20 QA reports |
| Tests | 6 automation specs, 51 tests; ~15 in-game labs; seeded bot matches in every release |
| Content | 11 heroes, 38 skins, 27 items, 4 maps built by scripts, 2 modes (+ 3v3 and 1v1 variants), 2 prototypes |

## Engineering lessons worth sharing

- **Paragon's animation blueprints only play a forward jog.** Strafing heroes moonwalked until movement switched to
  orient-to-movement with a separate aim-facing pass for casts (ADR-14, [QA 18](dark-factory/18-qa-v17-ui-maps-movement-balance.md)).
- **Lumen on Direct3D needs Shader Model 6** and silently falls back without it; SM6 cost ~30 % of the frame rate, so
  the game cooks both and switches to SM6 only for the Epic · Lumen preset (ADR-10, [QA 15](dark-factory/15-qa-v10-v11-menus-casting-minions-lighting.md)).
- **The Zen cook store can fill the system drive and silently drop packages** while the build still reports success;
  the cook now runs without it ([QA 17](dark-factory/17-qa-v14-v16-conquest-heroes-lan.md)).
- **A Shipping build ignores a map on its command line, and short map names do not resolve in an IoStore build**:
  every travel uses a full `/Game/Maps/<name>` path ([QA 18](dark-factory/18-qa-v17-ui-maps-movement-balance.md)).
- **Under IoStore, a `.pak` without a matching `.utoc` is not mounted.** A data-only patch passed the headless labs,
  which never show text, and was caught only by the rendered UI check ([QA 24](dark-factory/24-qa-english-ui-teaser.md)).
- **A localised editor breaks material scripts**: pin names come back translated, so the tools run with `-culture=en`
  ([QA 13](dark-factory/13-qa-v8-feel-death-camera-env.md)).
- **Additive hit reactions cannot play in a full-pose slot**: a script makes regular copies (ADR-3, [QA 07](dark-factory/07-qa-v3-arena-polish.md)).
- **A Shipping build writes no log**, so the labs that must run in the packaged game also write their own `.txt` files
  ([QA 21](dark-factory/21-qa-v20-bot-jumps-proto-mode.md)).
- **Saving a map from a headless commandlet can persist state that breaks AI navigation**: the prototype's bot stopped
  finding the player after a sign-only edit, so maps are regenerated by their build scripts instead (QA 24).
- **Render scale "auto" reads as a fixed 50 % and resets with every quality change**: a 1440p desktop rendered at 720p
  until the game kept its own setting ([QA 15](dark-factory/15-qa-v10-v11-menus-casting-minions-lighting.md)).

## What is proven, and what is not

- **Proven by the game itself (T1):** the rules, the data, the bot brain and Conquest (automation specs); abilities,
  aim, sustain, animation, physics, locomotion, LAN replication and both prototypes (labs); bots not getting stuck,
  casting everything and dying less (seeded matches); frame rates of the packaged build.
- **Seen (T2):** every lab takes screenshots, the UI demo shows every screen of the packaged game in English, and the
  teasers were checked frame by frame on contact sheets.
- **Not claimed (T3):** whether it is fun, how it feels, and whether the art direction lands. Those are the
  operator's judgements, and the operator's playtests drove most of the versions above.
- **Limits:** LAN was tested with host and guest on one PC. Balance is measured with bots, not with players. Claude
  cannot listen to audio, so the synthesised music was checked by spectrogram and loudness only. This public
  repository starts from a single commit; the day-by-day development history (its commit messages partly in Polish,
  the working language of the chat) is kept in the owner's private repository, and the dates above come from it and
  from the QA reports.

## Reading the repository as a case study

1. [`01-game-design.md`](dark-factory/01-game-design.md): what was asked for, as rules and scenarios.
2. [`02-technical-design.md`](dark-factory/02-technical-design.md): how it was built, and the decisions (ADRs).
3. The QA reports in order, [`05-qa-v1`](dark-factory/05-qa-v1-bot-arena.md) to [`24-qa-english-ui-teaser`](dark-factory/24-qa-english-ui-teaser.md).
   Each one starts with the operator's request and ends with the evidence and what remains unverified.
4. [TESTING.md](TESTING.md) to reproduce any of the evidence yourself.
