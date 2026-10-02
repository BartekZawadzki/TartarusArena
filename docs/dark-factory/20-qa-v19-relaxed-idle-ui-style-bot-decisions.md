# 20 — QA: v19 relaxed idles, a stylized UI with new fonts, bots that decide better

Scope (operator 2026-09-29): "fix the characters that keep a stiff arm stretched out in front of them while moving
and idling (the shooters); we still have no fonts and no stylized UI and HUD"; then "improve the bots so they are not
more accurate in the hitting itself but make better decisions in the game's logic"; then "the font is much too big".

## The stiff arm

`-ArenaLocoLab` (side camera, idle / forward / strafe / back / stop / relax) and the anim blueprint dumps
(`-ArenaDumpAnimBP`) showed it: the Paragon anim blueprints play one combat idle, and for the shooters it aims the
weapon (Revenant's pointed revolver, Sparrow's drawn bow, Gideon's raised hand); the packs ship relaxed idles
(`Idle_NonCombat_Loop`, `idle_relaxed`, `Idle_Relaxed`…) that the shipped blueprints never use.

Fix: `heroes.json` `idleRelaxed` per hero (Revenant, Sparrow, Gideon, Iggy & Scorch, Countess, Khaimera — the heroes
whose packs have one); `AArenaCharacter::TickRelax` plays it on the upper-body slot (blend in 0.6 s, out 0.25 s) when the
hero has been standing for 0.35 s with no cast, blow or other montage for 3 s, and stops it on the first step, cast or
hit. Evidence: `LOCO_Sparrow_relax` / `LOCO_Revenant_relax` (the bow and the revolver down) against the `_idle` shots.

## Fonts and the stylized UI

- Fonts (operator approved the download: SIL OFL 1.1, CREDITS.md): **Rajdhani** Medium / SemiBold / Bold (Indian Type
  Foundry, google/fonts) for all HUD and menu text and numbers, **Cinzel** Bold / Black (NDISCOVER/Cinzel) for the
  titles (main menu, screen titles, victory, the big announcements). Barlow stays the fallback.
- A real bug under it: the engine's large, medium and subtitle fonts are one asset (Roboto), so the v17 split "large =
  headings, medium = text" never worked (everything was drawn in Barlow Condensed; the first v19 build drew everything
  in Cinzel). The role now comes from the call: no font = a heading or a number (Rajdhani Bold), the medium font = text,
  `TitleFont()` = the Cinzel font object itself.
- Size (operator: "much too big"): every size a fifth under the first v19 build (text ×0.94, headings ×0.92 of the
  measured layout; the carved titles ×0.7 of their scale, the menu's titles set on their own).
- Style (canvas primitives, no textures): chamfered panels and buttons (top-left and bottom-right corners cut) with a
  vertical gradient body, a lit top edge, a soft shadow, the accent strongest on the cut corners; buttons steel when
  idle, lit with a gold edge and a gold bar on hover, warm gold when selected; bars with a gradient, a gloss line and a
  lit leading edge; the player's health in 100 HP segments (a taller mark every 500); chamfered ability frames; screen
  titles over a fading band with a gold rule and a diamond clasp; announcements on a band fading to both sides between
  two gold rules; the main menu's backdrop one smooth gradient (48 strips showed as vertical stripes) with a gold rule.
- The menu and overview cameras no longer constrain the aspect ratio (black bars on 21:9).

## Bots: decisions, not aim

| Change | Why |
|---|---|
| Aim error hard 25 → 70 cm, normal 110 → 140 cm; reaction hard 0.12 → 0.2 s; lead of a moving target 1.0 / 0.6 → 0.75 / 0.5; dodging hard 100 → 80 % of areas and 70 % of shots, normal ~50 / 35 % | a human hand — the operator asked for better decisions, not better aim |
| A healthy enemy hero running away out of reach (no gap closer ready) is not chased | chases dragged bots off their lane into the enemy's jungle and towers |
| A stun or a hard slow is kept for a hero when one is within 22 m | bots spent their crowd control on the wave |
| Below 45 % mana the wave is farmed with the basic attack | the mana is for the heroes |
| Home to shop with a full purse (1 400 gold) or with no mana (below 10 %, when hurt or with 900 gold), when nothing is near and nothing of ours is under attack | bots shopped only when they died or fled |
| On the enemy's half with three enemy heroes unseen by our whole team, or the team two heroes down: back to our wave, fighting only what reaches it | walking into a gank |
| The retreat line also rises with an empty mana pool and with the team two heroes down | a caster without mana fights with its basic attack only |

The first version (two unseen enemies, home below 15 % mana) kept the bots on their half: 119 trips home for mana in
4 matches and 1–3 towers down in 12 minutes; tuned to the values above (33 trips: 13 mana, 11 shop, 9 health).
Specs (51/51): no chase of a healthy runner, a nearly dead one is chased; a stun kept for a hero; the pull-back.

### Result (4 Conquest matches of 12 min, seeds 11–14, the same map)

| Bot deaths | v17 | v18 | **v19** |
|---|---|---|---|
| tower_dive | 33 | 13 | **8** |
| outnumbered | 96 | 62 | **41** |
| low_engage | 2 | 1 | **2** |
| fair | 86 | 74 | **62** |
| **all** | **217** | **150** | **113** |

Towers still fall (2–5 structures a 12-minute match), 0 stuck in 3 matches, 1 episode in the fourth.

## Regression (`regress_v19.sh`, editor build)

- Tests **51/51**; Arena bot matches seeds 2 and 7 stuck 0; Conquest 20 min stuck 0 (5 towers down, the boss taken).
- Foliage 0/0 on both maps (12 542 and 9 351 checked).
- AnimLab 71, BaseLab 14, MechLab 67, AimLab 14, SkillLab 42, FxLab 102, ConquestLab 22, UIDemo (menu, play, heroes,
  settings, hero select, match, shop, scoreboard, aim, death, end screens in the new style), training 4 — every check
  passed.

## Packaged game (Shipping, 2026-09-29)

Cook 9 min 58 s (10 278 packages from the DDC, 0 storage errors), staged via C: in 2 min 9 s, moved to
`Build/Windows`. The fonts are in the pak (`Content/Data/Fonts/Rajdhani-*.ttf`, `Cinzel-*.ttf` in the UFS manifest).
A headless run of the exe (`-nullrhi -ArenaStartMap=Conquest -ArenaBotMatch -ArenaConquest`, 60 s) loaded the Conquest
map and played the match to its end with no error (GameUserSettings.ini backed up and restored byte for byte). The
operator declined the windowed exe test on their desktop earlier in the session, so the rendered check (fps,
screenshots) is left for them to allow.
