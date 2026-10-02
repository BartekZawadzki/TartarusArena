# 19 — QA: v18 bots that read the situation, a natural jungle, playtest bugs

Scope (operator 2026-09-29): "play and catch every bug for the next update; improve the bots so they do not do stupid
things like attacking at the end of their health under my tower — the bots must understand the situation and how
the game works; make the space look more natural, with fewer empty places, and the excess vegetation may become
static objects or rocks from the Paragon pack; report progress to the new exe".

## Measuring "stupid" first: the death audit

Every hero death now logs `evt=death_audit` (killer and its kind, inside an enemy tower's reach and shot by it,
enemy and allied heroes within 15 m, the health lost in the last 6 s, retreating or not, level) and a category:

| Category | Meaning |
|---|---|
| tower_dive | killed by a structure, or inside an enemy tower's reach while it shot the victim |
| outnumbered | two or more enemy heroes more than allies around |
| low_engage | went in with less than 35 % of its health left and was not retreating |
| fair | everything else |

`DEATH_AUDIT` sums them at the end of a match; `bots5v5.sh` pools them over matches.

## Bot changes (`ArenaBotBrain.h`, `ArenaBotController.cpp`; specs in `Arena.Bot`)

| # | Situation the audit / the operator named | New rule |
|---|---|---|
| 1 | Fighting (or finishing a hero) under the enemy's tower at low health | The bot sees every armed enemy structure: its reach, whom it shoots, whether it is busy with our minions. A target inside a tower's reach is off limits unless the tower shoots our minions, the bot is above 60 % and the hero is below 30 % (or we outnumber them by two). A ranged bot may shoot into the reach from outside. Minions under a tower only while it is busy with our wave. |
| 2 | The finish rule ("chase the hero lower than me") pulled bots under towers | Only outside the enemy's towers, never while a tower shoots the bot, never outnumbered |
| 3 | A fixed 25 % retreat line | Adaptive: +10–20 % when outnumbered, +20 % under a tower's fire, +5 % on hard (at most 60 %) |
| 4 | Ganks: 37 of 39 "outnumbered" deaths were already retreating from ~73 % health — too late | Two more enemies than friends within 30 m, or one more within 16 m below 80 %: no fight with heroes, back to our nearest tower (hold there) or home, shooting what reaches it |
| 5 | A caught runner never used its escape | A retreating bot with an enemy hero within 7 m uses its leap towards home (a backwards leap aimed at the chaser); an ultimate leap only below 15 % |
| 6 | Inhibitor or core attacked while the team was elsewhere | Everyone above 30 % comes back from anywhere on the map (towers: within 60 m as before) |

Specs (48/48 with the three new ones): no dive under a free tower; the dive when the tower shoots our wave and the
hero is nearly dead, never at low health; home at 30 % under a tower's fire; outnumbered by two it falls back to its
tower and does not shoot out of reach.

### Result (4 Conquest matches of 12 min, seeds 11–14, the same map, before → after)

| Deaths of bots | Before | After | Change |
|---|---|---|---|
| tower_dive | 33 | 13 | **−61 %** |
| outnumbered | 96 | 62 | **−35 %** |
| low_engage | 2 | 1 | |
| fair | 86 | 74 | |
| **all** | **217** | **150** | **−31 %** |

Matches still end on points or at the time limit with 0 stuck episodes; the melee heroes die least (2.2–3.5 deaths
a match against 4.6–4.7 for the ranged).

## The jungle ("more natural, less empty; rocks and objects from the Paragon pack")

Conquest (`build_conquest.py`): the even stand of ~200 trees became **groves** (3–6 trees around 6 centres a
quadrant) with **clearings** between them; the clearings hold **rock outcrops** of the Paragon Monolith Nordic rocks
(a big rock with 2–4 small ones, 20) and **old ruins** (broken jungle pillars, damaged statues, the Agora's ruin
remains, with rubble); flat rocks breaking through the ground and fallen branches over the open floor; the rows
behind the bases became rock shoulders with a few trees; stumps and small rocks between. Mirrored in both axes (a fair
map), every footprint checked against the lanes, paths, plazas and camp clearings (`rock_at`), and kept out of the
ground cover. Two eye-level tour cameras in the jungle judge it from the player's height.

The Arena (`build_arena.py`, `dress_rock`): 12 outcrops of the same rocks in the jungle pockets and along the border,
20 flat rocks and branches over the open ground, on its own random generator (the rest of the map keeps its layout).

Bugs found while building it (screenshots from the tour):

| Finding | Fix |
|---|---|
| The KiteDemo scree meshes drew 30 m yellow discs over the jungle (their bounds do not match what they draw) | not used |
| A wide flat ruin scaled by its height alone came out 54 m across and lay over a side lane; its 4 corners were outside the keep-outs, and it pushed 1 963 edge tufts out of the lane borders | a rock is never wider than 2.2 × its height; its footprint is probed at 9 points; edge tufts back to 5 260 |
| 46 → 0 buried plants (v17) | still 0: foliage check Arena 12 542, Conquest 9 351 checked, floating 0 buried 0 |

## Playtest bugs (rendered bot matches from the player's camera, the logs of 8 matches)

| Finding | Fix |
|---|---|
| **The camera sat inside big heroes**: at a fixed 4.2 m arm Sevarog (2.8 m) and Crunch covered the crosshair and half the screen | the arm, the pivot's lift and the shoulder offset grow with the model's head height (the reference pose's head bone; the bounds counted Sparrow's bow and Revenant's coat): Sevarog 5.9 m, Crunch 5.7 m, the 1.9 m heroes unchanged |
| A ragdolled body "sank" by moving its simulated mesh: nothing moved (90 warnings per body) and it popped away after 1.5 s | a ragdoll lets go of the ground and drifts down |
| `Divide by zero` from the Paragon anim blueprints (their YawDelta / DeltaTime on a zero-length frame), `AnimDynamics` bone chains missing in Revenant's blueprint, null particle notifies in the packs' animations | the packs' own assets, warnings only (none in Shipping) — left |

## Regression (`regress_v18.sh`, editor build)

- Tests **48/48** (Arena.Bot: the three new situation specs; the first run failed "puts an area ultimate where it
  catches the most heroes" — the new 30 m odds made a lone bot back off from three heroes without its area ultimate:
  a falling-back bot now still lands an ultimate that catches two heroes or more, then backs off).
- Arena bot matches seeds 2 and 7: stuck 0; Conquest 20 min: 0 stuck, boss taken, 5 towers down; its death audit:
  tower_dive 1, outnumbered 32, low_engage 0, fair 35.
- Foliage: Arena 0/0 (12 542), Conquest 0/0 (9 351).
- AnimLab 71, BaseLab 14, MechLab 67, AimLab 14, SkillLab 42, FxLab 102, ConquestLab 22, UIDemo, training 4.
  MechLab failed once "a melee bot swings at body contact: 7 swings at 104..177 cm": the check still held the v8
  bound (contact + 40 cm) although v17 lets a bot start its swing within its blade plus the swing's lunge (it passed in
  v17 by timing; here the first swing came right after a knock-up). The check now asks that the target's body be in
  the blade's reach at every swing (Kwang: 294 cm; swings at 81..133 cm) — 67/67.

## Open

- Bots still die outnumbered (62 in 4 matches): most of them are caught in the open while walking the lanes; a map
  awareness of missing enemies (LoL's "missing" calls) would be the next step.
