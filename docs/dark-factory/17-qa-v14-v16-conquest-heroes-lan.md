# 17 — QA: v14 Conquest, v15 eleven heroes and skins, v16 accessibility, smarter bots and LAN

Scope (operator 2026-09-28): carry the plan to v16 and come back with a new exe; multiplayer is the goal. During v14
the operator added: "the towers are too many and too dense for the size of the whole map".

## v14 — Conquest (01 §3b)

| # | Finding (how) | Fix |
|---|---|---|
| 1 | Structures and camps were placed off the navmesh in bot matches (placement log `on_nav=0`): the match starts in the first frame, before the navigation data exists; then its tiles were still loading / rebuilding around the props' obstacles | the mode waits until both bases and every lane's middle project onto the navmesh and no build is in progress (20 s safety) |
| 2 | **Too many towers, too dense** (operator) | 7 structures per team instead of 10: one tower per lane, an inhibitor at the lane's exit from the base, the core; towers smaller (scale 0.62); the chain works for one or two towers per lane (spec) |
| 3 | A tower shot took `max(damage, share)` of a minion: a melee minion died in 2 shots, a wave melted | a fixed share of the minion's health (LoL: melee 45 %) — 3 shots |
| 4 | Pace: in 20–25 min only 2–3 towers fell; the structure damage counters showed the waves did ~2k in 25 min | lane minions ×2.5 on structures; bots siege an open structure when no enemy hero is within 15 m (22 m was never true in a 5v5); structures' health lowered |
| 5 | 38 stuck episodes in one match: the side inhibitors stood inside the base's side gate | moved 12.5 m along the lane, out of the corridor |
| 6 | The boss sat 6 m from the mid lane's path and joined lane fights | moved to the pit's side, leash 10 m, shot range 14 m |
| 7 | The boss's death effect (the Paragon core's explosion, made for an 85 m core) covered the screen | a siege-minion burst; the core's death uses the turret explosion |
| 8 | The structure bars stacked into the top bar and showed through the terrain; the range ring drawn as a line broke near the camera | bars hidden behind the terrain and behind a nearer bar; the range is a decal on the ground (red when the tower targets you) |

Evidence: `Arena.Conquest` spec (chain, targeting, ramp, hero damage rules, layout, timeout) in the 43 tests; the
`-ArenaConquestLab` 22/22 with screenshots (CQ_Tower, CQ_TowerShot, CQ_Ruin, CQ_Camp, CQ_Boss, CQ_Overview, CQ_HUD);
bot matches: camp buffs taken, the boss killed, structures falling on the side that wins.

## v15 — eleven heroes, skins, teams without mirrors

- Crunch, Iggy & Scorch, Khaimera, Morigesh, Revenant, Sevarog from their Paragon packs (data drafted and verified by
  a sub-agent: every asset path exists, the time-to-kill simulator ported to JS; the engine's `Arena.Balance` passes
  for all 11 at levels 5 / 10 / 15). Kwang's basic 34→36 and Sparrow's 26→25 keep every hero ≥ 2 % inside its band.
- Hit reactions: non-additive copies made by `make_hitreacts.py` (the editor crashes during the async animation
  compression after a few copies; the tool skips what exists, so it was run until done — 24 copies).
- 38 skins (the packs' `Skins` meshes on the same skeleton, checked by a probe), chosen in the hero browser, worn by
  your hero; bots wear one now and then.
- A match draws ten different heroes (no mirrored teams). The pick screen is a grid; the browser two columns.
- Deaths: Khaimera, Morigesh and Sevarog do not end their death animations lying (kneeling / floating: the AnimLab
  failed them) → they vanish in their own bursts like Countess.

## v16 — accessibility, controls, the match's flow, smarter bots

- Settings: UI scale of the match HUD, a colour-blind mode (enemies orange), the heroes' voice volume, rebinding of
  16 keys (a click, then the key; a key taken swaps), the beginner's hints; a gamepad (A jump, RT attack, RB/LB/X/Y
  abilities, LT + button rank, B shop/recall, d-pad potions/ping/revive, Start pause).
- Pings (G / middle mouse): a marker for the team on the ground and on the minimap; allied bots with nothing in reach
  go there.
- Dead: the camera follows an ally (LMB: the next one). A loading screen with a tip on every map change. The end
  screen: MVP, most damage, most healing, best farm, the match's length (Conquest: structures destroyed).
- Bots (`Arena.BotSmarts` spec): straight shots lead a moving target (0 / 60 / 100 % by difficulty), an area ultimate
  goes where it catches the most heroes, the team focuses a weak hero an ally is on, bots step out of enemy
  telegraphs (normal most of the time, hard always).

## v16 — LAN multiplayer

Architecture (ADR-11 continued): the host is a listen server and the only authority. Each unit replicates which
definition it was made from (a client builds the body from the same data), its vitals for everyone, and its owner's
gold, ranks, cooldowns and items to its player only; casts, blows, deaths, spawns and states reach the clients as
multicasts and are drawn there (shots, areas, swings) without damage. A guest's actions go to the server as calls
(casts with the aim, ranks, shop, potions, recall, revive, ping, pick) and the server's answers come back as notices.
On a client the HUD and menus read a local mirror of the game mode filled from the replicated state (with the
server's clock converted to the local one). Menu: Play → HOST LAN MATCH / address / JOIN; a guest joins the other
team (the next one the host's); a guest joining mid-match takes over a bot; a guest leaving hands its hero to a bot.

Test `-ArenaNetHost` + `-ArenaNetGuest` (two processes on one machine, 127.0.0.1). The host opens a listen map and
waits for the guest's pick before starting; the guest joins through the menu's own join action, picks Sparrow,
walks to the nearest enemy and attacks with its basic attack and ranked abilities through the server calls.

| Run | Finding (how) | Fix |
|---|---|---|
| 1 | The host started the match before the guest had loaded and picked | the host waits for the pick |
| 2 | The host checked after the guest had already left (the guest quit first) | the guest stays 20 s after its own checks; a guest back in the menu after its test quits instead of rejoining |
| 3–4 | 60+ casts ran on the server but dealt 0 damage. Logged positions on both sides (`netguest_pos`, `nethost_guestpos`): the guest's hero stood at its spawn for 33 s — the test walked toward `AimTarget`, which the crosshair's aim recomputes every frame (usually none) | the test keeps its own target. Movement itself replicated correctly (the host saw every step the guest made) |
| 5 | — | **host 4/4, guest 7/7**: the guest's pick reached the host; the guest plays its own hero on team 1; 42 of its casts ran on the server; its blows landed (977 damage to heroes); the guest sees 10 hero bodies and the minion waves, its cooldowns come back from the server, enemies' health changes, the HUD mirror follows the match (screenshot NET_Guest) |

### Blind code review of the network and Conquest code (a separate agent, read-only, 13 findings, all confirmed in the code)

| # | Finding | Fix |
|---|---|---|
| 1 | **The abilities kept the engine's default `LocalPredicted`**: for a guest's hero the server told the guest to run the whole ability on its own machine — client-side damage, deaths that the server never had, every cast seen twice | `NetExecutionPolicy` / `NetSecurityPolicy` = ServerOnly; damage and heals return at once on a client |
| 2 | A guest's pick screen never closed when the match started: the menu held the mouse (no camera, no basic attack, no ping) — the test drove the hero directly and missed it | the screen closes when the phase leaves the pick; the guest test now checks it |
| 3 | Victory, "our core is under attack", "we lost a tower" were written from the host's side and replicated word for word (a guest on team 1 read VICTORY when it lost); gold popups and structure hints of the guest's hits appeared on the host's screen | news carry the team and both texts (each machine shows its own), the end screen compares with the viewer's team, hints go to the hitter's machine |
| 4 | A pick accepted in any phase re-pointed a guest's shop and revive at another hero (e.g. the host's) | picks only during the pick phase |
| 5 | An out-of-range potion index from a crafted client read past an array on the host | the index is checked in the server calls |
| 6 | Deaths reached only who was connected (multicast): a late guest saw fallen towers standing | the death is replicated state too; a late client puts the unit down at once |
| 7 | The power orb (Arena's objective) existed only on the host | the orb replicates; taking it is the server's |
| 8 | The host's Esc paused the listen server: every guest froze | in a LAN match the menu opens without pausing |
| 9 | A tie put the second guest on team 1 again (two guests against a lone host); a late guest found no bot when its team's bots were dead | a tie joins the host's team, a full team is skipped; a late guest takes a bot waiting to respawn |
| 10 | A recall was cancelled by moving only when the client said so | the server cancels it when the hero moves |
| 11 | The target a guest named with a cast was not checked | the server checks it is alive, in reach and in sight |
| 12 | Conquest gold to a killer that had died meanwhile was lost | paid to its respawn record |
| 13 | The bot replacing a leaving guest in Conquest had no lane | it gets the hero's lane |

Re-run after the fixes: host 4/4, guest 7/7 (64 casts on the server, 1590 damage to heroes).

### Conquest pace (bot matches with a once-a-minute snapshot of the cores: health, open or shut, attackers at them)

| Finding | Fix |
|---|---|
| One team took every tower and inhibitor by 11 min, then the open core stood untouched for 9 min: the bots flag enemies within the Arena's 11 m base radius as "safe" (never chased into the fountain) — Conquest's core stands 9.5 m from its base, so no hero ever attacked it | in Conquest only the fountain itself (5.5 m) is safe and a structure never is; the open core is a goal when the team is up in numbers or its wave is at the core; with at most one defender alive heroes hit it without minions (the backdoor cut still applies) |
| An even match: 4 structures in 20 min — a bot sieges only with no enemy hero within 15 m, nearly never true in a 5v5 | a siege also when our heroes around outnumber theirs (and the bot is above half health) |

## High end pass (operator: "improve everything we have, piece by piece, to a high-end version")

| # | Finding (how) | Fix |
|---|---|---|
| 1 | Portraits: 512 px, and a portrait shot while the backdrop's material compiled showed the grey checker | portraits 1024 px, ability icons 256 px, minimap 1280×800, browser stage 1080×1350; the studio also waits for the backdrops' shader maps |
| 2 | Portrait framing (contact sheet of the exported portraits, `-ArenaIconExport` + bone log): Crunch showed a fist (his menu pose stands 1.4 m behind the actor with the arms in front), Iggy & Scorch showed Scorch's jaw with Iggy cut off, Sevarog black under the hood | per-hero framing in heroes.json (`portraitPose`, `portraitBone`, `portraitCam`, `portraitLook`, `portraitLight`): Crunch in his combat idle from his right side, Iggy framed on `Goblin_head` with Scorch below, Khaimera wider, Sevarog lit from below |
| 3 | Texture streaming (guest screenshot, editor warning): "pool over budget 268 MiB" with the engine's 1000 MB Epic pool — faces and armour lose their top mips | pool 1300 MB (High) / 1600 MB (Epic), clamped to the card's memory |
| 4 | Overhead bars of three heroes in one spot drawn over each other (guest screenshot): two stacking steps, far bars excluded | each bar goes above the highest one in the way (up to 8 steps), far bars stack too |
| 5 | HUD (Conquest screenshot): mana costs ran into the next icon, "B recall · P shop" hung below the screen, the top bar said "structures" without saying which, the buff timers overlapped their frames, flat panels | costs and timers on dark tags inside the icon; the hint inside the panel; "destroyed" + one pip per enemy structure (tower / inhibitor / core by size, filled when destroyed); panels with a shadow, a shading ramp, a light top edge and accent corners |

## Results (editor build, 2026-09-28)

| Check | Result |
|---|---|
| Automation `Arena` | 43 / 43 |
| Bot matches (Arena, seeds 2 and 7) | 10 heroes, all four abilities used, stuck = 0 |
| Conquest bot matches | seed 4: core destroyed at 13 min (one-sided); seed 7: core destroyed at 21 min (even); stuck 0 / 1 |
| Foliage check | floating 0, buried 0 (12 664 checked) |
| AnimLab / BaseLab / MechLab / AimLab / SkillLab / FxLab / ConquestLab / training | 71 / 13 / 67 / 14 / 42 / 102 / 22 / 4 — all pass |
| LAN host + guest | host 4 / 4, guest 8 / 8 |
| UIDemo (editor, 1600×900, other apps on the GPU) | 41 fps average with a first-start shader hitch — measured on the packaged game below |

## Packaged game (Shipping, 2026-09-29)

| Finding (how) | Fix |
|---|---|
| The first package said BUILD SUCCESSFUL, yet the exe drew the pit and the stone paths as flat brown (screenshot compared with the editor's and with the v13 exe's): the cook stores its output in the Zen server on C:, which rewrites the whole oplog (~15 GB) — C: fell to 1.5 GB and ~150 packages were rejected (`Insufficient Storage 507`): the 8K floor textures, fade materials, the Training map, effect meshes | `bUseZenStore=False` (cooked files on D:), shader workers 8, asset compilation limited to 6 GB, a disk guard stopping the packaging below 6 GB on C:; a cook memory cap of 12 GB made the cooker unload and recompile materials in a loop — removed |
| Result | 10 237 packages cooked, 0 save errors; floors textured as in the editor; UIDemo seed 3 at 1600×900 (High): **137.8 fps average, 1 % low 98.9 fps**, 1 hitch (132 ms), 9 / 10 heroes used all four abilities in 2.5 min |
