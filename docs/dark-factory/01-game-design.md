# 01 — Game Design: Tartarus Arena (working title during development: Paragon Arena)

Labels: [C] confirmed by the operator · [A] assumption with a default DF proceeds on · [O] open.
Authority for every number below: this document, then the data assets that copy it (`DA_*`, DataTables).

## 0. Frame (lens)
Per tick (fixed sim step, engine default 60 Hz): `(World, Inputs[player + 9 bots], dt) → World'`.
Pure: damage math, cooldown/mana checks, score and gold arithmetic, bot decisions from a snapshot.
Effects: none leave the machine in v1 (single-player). Saving settings is the only disk write.

## 1. Vision
A third-person, Smite-style hero brawler: 5v5 in one big arena built from Paragon's Agora/Monolith
environment, heroes from Epic's free Paragon library [C]. Combat is fast, readable and loud: every hit
has feedback, every ultimate is an event. v1 is single-player vs bots [C]; multiplayer comes later [C].

Pillars (each with a measurable proxy):
1. **Every fight feels dynamic** — ≥ 1 ability cast per hero every 4 s in a 5v5 bot fight (median).
2. **Every class plays differently** — each class has a unique movement/attack pattern (range, dash, CC);
   TTK matrix differs by ≥ 20 % between the fastest and slowest matchups.
3. **Built from the library** — 0 new art assets; everything visual/audio comes from Paragon packs [C].

Non-goals (v1): online multiplayer, towers/jungle camps, matchmaking, accounts, cosmetics, new art, publishing
a build. (The item shop and paid revive were added on 2026-09-27 [C], see §3.) (Multiplayer is planned; v1 does not add replication [C].)

## 2. Heroes and classes [C: roster, A: numbers]

Roster change 2026-09-26 [C]: the operator's Fab library has no Kallari, Twinblast or Muriel; replaced by
Countess, Sparrow and Aurora (all in the library). Only library assets + UE 5.8 content are used [C].
Update 2026-09-26 [C]: the Aurora pack was not added to the project; the operator added Kwang (plus Crunch,
Iggy & Scorch, Morigesh, Revenant). Kwang takes the Guardian slot with the same numbers — one JSON entry, so
Aurora can come back by swapping it in `heroes.json`.
| Hero (Paragon pack) | Class | Role | HP | Mana | Move m/s | Basic attack |
|---|---|---|---|---|---|---|
| Greystone | Warrior | bruiser, frontline | 620 | 220 | 6.0 | melee cleave 2.6 m, 1.0 s |
| Countess | Assassin | burst, mobility | 520 | 200 | 6.6 | melee 2.3 m, 0.7 s |
| Gideon | Mage | AoE burst, zoning | 430 | 320 | 5.8 | projectile 25 m, 1.1 s |
| Sparrow | Hunter | sustained ranged DPS (bow) | 450 | 200 | 6.0 | fast arrow 30 m, 0.55 s |
| Kwang | Guardian | frontline CC, ally shields | 600 | 300 | 6.1 | jade sword melee 2.5 m, 0.85 s |

(v12: the exact numbers live in `Content/Data/heroes.json`; this table is a summary. Balance is checked by the TTK
matrix, VR-07.)

Each hero: basic attack + abilities 1, 2, 3 and ultimate 4. Abilities reuse the hero's own Paragon
animations and particle effects. Ability archetypes (data-driven): skillshot projectile, ground AoE,
dash/leap, melee cone, self/ally buff (shield/heal), crowd control (stun/slow/knock-up).
Cooldowns: 1–3 → 6–14 s [A]; ultimate 60–90 s [A]; mana cost 40–100 [A]. Exact kits: §2a (filled at
Stage 2 from the animations/FX actually present in each pack).

## 3. Arena mode (Smite-style) [C mode, A numbers]
Rules changed 2026-09-27 [C]: shared team **points** and a **match clock** replace the tickets.
- Points: hero kill +5, minion kill +1, own minion walking into the enemy base +2 [A, `rules` in heroes.json].
- Length 5 / 10 / 15 min, picked on the hero-select screen (F5/F6/F7) [C]; the score limit is 30 points per
  minute on the clock (150 / 300 / 450) [A]. First team to the limit wins; at time-up the leader wins; a tie goes
  to overtime where the next point wins (bot-only matches call a draw after 30 s of overtime).
- Gold: 600 at the start, +3 per second, +300 per hero kill, +150 per assist, +20 per minion [A].
- Shop (B), MOBA item tree (2026-09-27 [C: "items must build like in MOBA games and matter more"]): six slots,
  27 items in three tiers — 9 parts (300–400 gold), 8 upgrades built from parts, 10 finished items built from
  upgrades and parts, each with a unique passive (execute, infinity crits, overheal shield, on-hit, spell blade,
  archon, echo, thorns, last stand, regeneration). Owned parts are used up and come off the price (a full inventory
  can still buy an item that uses one of its parts); selling returns 60 %. Stats: power, armour, health, mana,
  regen, move speed, cooldown reduction, attack speed, lifesteal, crit chance, armour penetration (caps: CDR 40 %,
  lifesteal 30 %, move 40 %, attack speed 60 %, crit 100 %). Crits are items only and deterministic: 25 % = every
  4th basic attack (175 %, 225 % with Blade of Infinity). The shop shows the three tiers, the hero's
  recommended build, the recipe tree with the parts you own, what an item builds into and your price. Open in your
  own base or while dead. Bots buy the next item of their build whole, else its priciest affordable part.
- Revive (F while dead): back at the base at once for 150 + 50 × level gold, then a 120 s cooldown [C, A numbers].
- Minion waves every 30 s per team (3 melee + 1 ranged) walk one of three lanes to the enemy base [A]. Since v11
  the Paragon: Minions pack: the Dawn army for team 0, the Dusk army for team 1, with their own animations; minions
  fight minions and switch to a hero who hits one of their heroes (VR-30). Since v12 a wave's level grows by one every
  two minutes (+5 % stats a level).
- Levels and ranks (2026-09-27 [C: "the biggest gap is hero levelling and upgrading skills with the level"]):
  levels 1–20; XP to the next level = 110 + 35 × (level − 1); XP from time (4/s), minions (28 to every hero of
  the killing team within 15 m), hero kills (110 + 18 × victim level, assists 60 %). Every level adds the hero's
  own growth (health, mana, power, armour, regen; the basic attack + ~2 damage). One skill point per level
  (Ctrl + 1–4, or the key of an ability not learnt yet): abilities 1–3 open rank r at level 2r − 1 (1/3/5/7/9),
  the ultimate at levels 5/9/13/17/20; each rank adds damage, heal, shield, stun, slow, buff time and mana and
  shortens the cooldown (`…PerRank` in heroes.json). Bots spend by the hero's `skillOrder`, the ultimate first.
- Respawn: 5 s + 0.75 s per hero level [A].
- Sustain (2026-09-27 [C: "improve the game elements after the top MOBAs"]): recall (B away from the base, 6 s, broken
  by moving, casting, a blow, a stun or a knock), fountain (own: +12 %/s health and mana; enemy hero: 25 %/s true
  damage), potions (health 220 / mana 160 over 8 s, 50 gold, max 5 each, keys 5 / 6), death recap (who and what hit
  you in the last 10 s), shutdown gold (100 per kill above 2 of the victim's streak, max 400) [A numbers].
- Default match length 10 min (F5/F6/F7 on the hero-select screen: 5/10/15).

## 3b. Conquest (LoL-style) [C mode 2026-09-28, A numbers] — v14
The second mode on the Play screen: the team that destroys the enemy core wins (a 35-minute safety clock: then the
team that lost fewer structures, then the score). Everything below is decided by the game mode (the server) and
shown from the replicated game state (ADR-11).
- **Structures** (Paragon turrets, Dawn for team A, Dusk for team B). Per team, on each of the three lanes: one tower
  and, at the lane's exit from the base, an inhibitor; the core on the base plateau (7 per team; the first layout had
  two towers per lane, 10 per team — too dense for a 140 × 90 m map, operator 2026-09-28). A tower (3000 HP, armour
  35) and an inhibitor (2200) shoot homing shots (11 / 10 m); the core (3500) shoots too.
- **Protection chain**: a lane's tower is open; the inhibitor opens when the lane's towers fell; the core when any
  inhibitor of its team fell. A fallen inhibitor comes back after 240 s; while it is down the other team's waves on
  that lane are led by a super minion.
- **What hurts a structure**: minions, and heroes' basic attacks from within 12 m of it; abilities never; a hero
  with none of its own minions within 14 m deals a third (backdoor protection). The player is told why a blow did
  little or nothing.
- **Tower targeting**: an enemy hero that just hurt one of the tower's heroes in range is taken at once ("call for
  help"); else it keeps its target; else the nearest minion; else the nearest hero. Its shots on the same hero grow
  +40 % each (at most +120 %); a minion loses a fixed share of its health per shot (melee 45 %, ranged 70 %, siege
  14 %, super 7 %). The tower's range is painted on the ground when the player's hero comes near (red: it shoots you).
- **Rewards**: a structure pays every hero of the team (tower 125, inhibitor 75 gold; XP to those near); the hero who
  hurt the tower last gets 100 more. A hero killed by a tower, a minion or a monster is the kill of the last enemy hero
  who hurt it (10 s).
- **Waves** on all three lanes every 30 s (3 melee + 1 ranged per lane), a siege minion every third wave.
- **Jungle** (team 2, neutral, hostile to both): per half a red guardian (buff: +15 % damage), a black hunter (buff:
  +100 % mana regeneration, 15 % shorter cooldowns), two camps of three jungle minions (gold, XP); buffs last 90 s.
  A monster fights whoever hit it or its camp; dragged past its leash it walks home, taking no damage, and heals.
  Camps come back 100–150 s after the last one fell; monsters grow with the match.
- **Prime Helix** (the boss) in the centre from 7:00 (again 5 min after its fall): immune to stuns and displacement;
  its killers' team gets Moc Helixa for 150 s (+20 % damage, +10 % speed, waves +50 % health and damage) and
  150 gold each.
- The fountain burns enemies only within 5.5 m of the spawn (the core stands outside it); XP comes at 65 % (a longer
  match).
- Bots: a lane each (one mid, two per side), push behind their wave, never step into an enemy tower's range without
  their minions under it (and step out when it shoots them), siege an open structure when no enemy hero is near,
  defend a structure under attack, take their half's buff camps, go for the boss with two enemies down or late with
  the team up.

## 4. Controls (Smite) [C]
Over-the-shoulder camera; WASD move; mouse aims (crosshair); LMB basic attack; Space jump/evade; Tab
scoreboard; Esc pause. Abilities (2026-09-27 [C]): 1/2/3/4 show the ability's range and area on the ground (range
ring, area disc, cone, projectile lane, dash path; red while it cannot be cast); LMB or the same key again casts at
the shown spot; RMB or Esc cancels.

Aim assistance and hit confirmation (2026-09-27 [C: "attacks are hard to land and it is hard to tell when you hit"]):
the crosshair picks the enemy the next basic attack or single shot goes for (a shot: an enemy whose body is within
0.6 m + 3.5 % of the distance of the crosshair ray; a swing: an enemy in reach + 40 cm within 65° of the view); the
crosshair turns red and brackets frame that enemy, whose health bar grows with its health in numbers. A shot at it
leads its movement (taken again when the shot leaves), a swing turns the body to it, sticks to it through the
wind-up and steps up to 70 cm to close a small gap. Every landed player hit shows an X on the crosshair (white,
yellow critical, red kill), a body flash on the victim, a larger damage number beside it and a tick sound; a whiffed
swing shows its grey arc on the ground. Bots get none of this. `arena.AimAssist 0` turns the assistance off.

## 5. Game feel (must be measurable) [A]
Hit-stop 0.08 s (heavy blow) / 0.12 s (ultimate) between heroes only, at most one per 0.4 s per unit (a stream of
minion pokes never keeps a hero in slow motion); camera shake on ult cast and big impacts; floating damage numbers; hit flash on the victim; kill feed; announcer text for double/triple
kills and first blood; low-HP screen vignette; ability FX from the pack on cast and impact.

Reaction animations come from each hero's own pack (2026-09-27 [C]): pre-match intro (LevelStart), hit
reaction from the hit side, stun start + loop, airborne knock pose until landing, death animation held on the
last frame then the body fades (v8; Countess vanishes in her shadow burst), drop-in landing with a light column at
every respawn, victory emote for the winners. Minions play their Paragon death clip and go limp (a slump into a
ragdoll, never a launch), then fade.

## 6. Bots [A]
Difficulty Easy/Normal/Hard = reaction 0.6/0.35/0.2 s, aim error 3.0/1.5/0.6 m. Bots pick targets
(close, weak heroes first), use all four abilities, retreat below 25 % HP (potions, recall with no enemy hero within
30 m and no enemy minion within 12 m), return after respawn, contest the power orb. Team focus, dodging telegraphs and
lane strategy are planned for v16.

## 7. Validation rules
| ID | Rule | Scope |
|---|---|---|
| VR-01 | Damage applied ≥ 0; HP clamped to [0, MaxHP]; mana to [0, MaxMana] | one event |
| VR-02 | An ability on cooldown or without enough mana is rejected, no cost spent | one event |
| VR-03 | One ability activation damages a given target at most once (unless the ability says "ticks") | one activation |
| VR-04 | Friendly fire = 0 (allies are never damaged or CC'd by allies) | one event |
| VR-05 | Team points only increase, by exactly the §3 amounts; the match ends the tick a team reaches the limit, or at time-up (tie → overtime) | match |
| VR-06 | Respawn happens at the own base after exactly the §3 timer (±1 tick) | per death |
| VR-07 | Time to kill (`Core/ArenaBalance.h`, spec `Arena.Balance`): each hero's average time to kill the roster at levels 5 / 10 / 15, against the roster average, within its role band — assassin −35…−10 %, guardian +5…+35 %, others ±15 % | global |
| VR-08 | No hero/bot/minion stuck > 3 s or below kill-Z, across 10 seeded bot matches | global |
| VR-09 | Every bot casts each of its 4 abilities at least once in a 3-min 5v5 bot match | global |
| VR-10 | p95 frame time ≤ 16.7 ms during a 5v5 fight, Development build, 1080p | global |
| VR-12 | Gold never negative; a purchase needs gold, a free slot and the shop (base or dead); a revive needs gold and its cooldown | one event |
| VR-13 | Nothing is thrown unnaturally: knock-up ≤ 4.5 m/s (rise ≤ 1.4 m), knockback ≤ 5 m/s, a blast lifts a 40 kg prop ≤ 1.2 m, dead bodies play their death animation (no ragdoll impulse), bot recovery never hops | one event |
| VR-14 | Nothing stands on or spawns inside another unit: a pawn or a loose prop is never a floor; spawns pick a free spot; visible map pieces that look solid have collision | global |
| VR-15 | What an indicator shows is what happens: a ground area lands on the shown spot (≤ 30 cm), a dash ends on the shown path end (≤ 90 cm); shapes equal the ability's range / radius / angle | one cast |
| VR-16 | Walls, rocks and trees block every hit (area, cone, blast); a hit reaches a target when the target's body edge is inside the area on the same level | one event |
| VR-17 | Melee units fight at body contact (swing at ≤ 150 cm centre distance) and bodies never overlap; no see-through hole anywhere on the map | global |
| VR-18 | The player's attacks land where the player means: basic shots at a strafing hero 12 m away with the crosshair on its body land ≥ 70 % and ≥ 25 points more than without assistance; a swing at an enemy 35 cm past the reach lands; the shot lane shows the led direction (≤ 4°) | global |
| VR-19 | Every landed player hit is confirmed exactly once (one hit marker per hit, none for another unit's hit), the body flash ends ≤ 0.6 s after the last hit; bots never get assistance | one event |
| VR-20 | An ability does exactly what its rank says: damage, slow, stun, mana and cooldown at rank r = base + perRank × (r − 1); ranks open by level (abilities 1/3/5/7/9, ultimate 5/9/13/17/20), one point per level; a level adds exactly the hero's growth | one cast |
| VR-21 | Displacements and timings are fixed: knockback = 0.45 s × speed m, knock-up height v²/2g for 2v/g s, a ground area lands Delay s after the cast and its telegraph fills to the rim at that moment, a shot hits a body whose edge is inside the lane the indicator draws | one cast |
| VR-23 | Recall completes after RecallSeconds and any blow, move, cast or crowd control breaks it; the fountain heals its team and burns enemy heroes by the rules; potions heal their amount over their time and are bought only in the shop; shutdown gold follows the streak | one event |
| VR-24 | No plant, stone or tree of the play area hangs above the ground (> 6 cm) or stands inside a ramp, wall or terrain block (`-ArenaFoliageCheck`) | global |
| VR-22 | Items build like MOBA recipes (owned parts come off the price and are used up), passives do what their text says, crits come every 1/chance hits | one purchase / hit |
| VR-25 | A dead body lies on the ground (a hero at least 2 s after its fall, a minion goes limp after a short stagger), then fades away within 1-1.5 s; nothing is flung, nobody is pushed by a body | one death |
| VR-27 | An ability does what its indicator shows: a shot flies the lane drawn (up and down slopes), an explosive shot bursts on the first enemy it touches and that enemy takes the blast, a lobbed shot comes down on the circle drawn, a knockback stops at a wall (`-ArenaFxLab`) | one cast |
| VR-28 | A key of an ability that cannot go off (cooldown, mana, stun, not learnt) never enters the aim nor blocks the basic attack: the slot flashes and the reason is shown; a cast pressed while the hero cannot act for a moment waits up to 0.6 s (input buffer); three cast modes: quick with a preview (default), instant, with confirmation (`-ArenaSkillLab`) | one key press |
| VR-29 | Every marking is relative to the player (LoL / Smite): ring under each hero green = you, blue = ally, red = enemy; bars, names, minimap frames and bases in the same colours | global |
| VR-30 | Minions fight minions; a hero draws them only after hitting one of their heroes (2.5 s, "call for help") | one decision |
| VR-31 | Conquest (§3b): the protection chain, what hurts a structure, tower targeting and ramp, a fall's rewards and the next structure opening, the camp leash and buffs, the boss, the core ending the match (`Arena.Conquest` spec, `-ArenaConquestLab`) | one event |
| VR-26 | Bots and minions turn at a rate (no yaw jump over 25 degrees in a frame: `yaw_snaps=0`); the player's camera trails movement by at most 1.3 m, never shows the inside of a wall, and glides back out after an obstacle | global |

## 8. Test scenarios (State 0 → input → State 1)
| ID | Scenario |
|---|---|
| GS-01 | Arena loaded, player picks Greystone → 10 heroes at their bases, score 0:0, clock at the chosen length, HUD shows HP/mana/cooldowns |
| GS-02 | Player basic-attacks an enemy in range → enemy HP drops by the formula result, hit-stop + number shown |
| GS-03 | Player casts ability 1, then again at once → second cast rejected, cooldown shown, mana spent once |
| GS-04 | Gideon skillshot fired through an ally at an enemy → ally untouched, first enemy hit once |
| GS-05 | Enemy hero HP reaches 0 → death anim, killer team +5 points, kill feed entry, respawn at base after timer (or paid revive) |
| GS-06 | Minion wave spawns at t=30 s; a minion dies → the killing team +1 point; a minion reaches the enemy base → its team +2 |
| GS-07 | A team reaches the score limit or the clock runs out → victory screen within 1 s, input disabled, match stats shown |
| GS-08 | 5v5 all-bot match (seed s) → at least 8 of 10 bots cast all 4 abilities in 5 min and all 10 in 10 min (the ultimate opens at level 5), no crash, errors in log = 0 |
| GS-09 | Countess's dash into a wall → stops at the wall, no tunnelling, no stuck state (`-ArenaMechLab`) |
| GS-10 | Ultimate cast → camera shake + FX + hit-stop 0.12 s, cooldown 60–90 s |
| GS-11 | Kwang shields the weakest allied hero (never a minion), who takes damage → shield absorbs first, then HP |
| GS-12 | Easy vs Hard bots, same seed → Hard wins ≥ 4 of 5 matches |
| GS-13 | Conquest all-bot match (seed s, 25 min) → structures fall on both sides, a camp buff and the boss are taken, no stuck bot, no crash |
| GS-14 | The player's hero shoots a tower from 17 m / with an ability → no damage and the reason shown; from 7 m with its minions near → full damage, without them a third |

## 9. Platforms and budgets [A]
Windows 64-bit, keyboard + mouse; 1080p 60 fps target on the operator's PC; Development build ≤ 20 s to
first playable.

## 10. T3 (only the operator judges)
Fun, fight readability, hero fantasy, "emotional" feel. DF delivers metrics (casts/s, TTK table,
hit-stop timings), captures and a playable build for the operator to judge.

## 11. Open
- [Closed] Packs in the project: Greystone, Countess, Gideon, Sparrow, Kwang, Crunch, Iggy & Scorch, Khaimera,
  Morigesh, Revenant, Sevarog, Minions, Agora & Monolith, Props (v11 inventory, QA 15/16).
- [Closed] The Paragon particle effects are Cascade systems and run on UE 5.8.
- [O] Multiplayer (the operator's goal, 2026-09-28): the network foundation is v12 (ADR-11); LAN play is v16.
