# 22 — QA: v21 melee heroes on par with the ranged ones; the prototype's combat without a guard, built on the mouse

Scope (operator 2026-09-29): "in the main game the melee characters must be much stronger (tougher) to stand level with
the ranged ones, and they need more fitting abilities to balance them against the rest — the dissonance is too big; in
the new prototype remove parrying and blocking completely, add more animations and make everything fluid with what we
have, and show the controls at the start; the combat itself must also be intuitive and dynamic, using the mouse fully."

## 1. Melee heroes (Tartarus Arena)

### The measurement before

Conquest 5v5 bot matches, seeds 11–14 (v20): melee heroes dealt **31 % less damage** to heroes (8 446 vs 12 192 a match)
and scored a third fewer kills (1.8 vs 2.6). The 1v1 duel lab had melee heroes winning 58 % — a duel is not where
the gap shows; a team fight is (the melee hero walks through everyone's shots to reach anyone).

### The change: the melee trait (rules in heroes.json, the same for every hero whose basic attack reaches under 5 m)

| Rule | Value | Why |
|---|---|---|
| `meleeRangedResist` | damage from ranged heroes −10 % | it has to walk through the shots to fight |
| `meleeTenacity` | stuns and slows 20 % shorter | kiting slows kept it out of reach |
| `meleeDashShieldPct` | a dash ability gives a shield of 5 % max health for 3 s (a top-up, not a stack) | it leaps in behind a guard |
| `meleeLifesteal` | basic attacks heal 5 % of their damage (unchanged: at 8–10 % the duels ran away) | it fights in the enemy's face |
| health, armour | +12 % health and health per level, +3 armour, +0.2 armour per level, +0.5 health regen | tougher |
| basic reach | 2.8–3.0 m (was 2.4–2.8) | the swing lands on a target that backs away |
| gap closers | Greystone's leap, Kwang's sword throw, Crunch's charge 9 s (were 11), Khaimera's leap 8 s (9) | they reach the fight more often |
| Sparrow | the arrow 23 damage (was 21) | the one ranged hero under its band once the melee heroes got tougher |
| Kwang, Revenant | Kwang's sword 40 damage every 0.8 s (38 / 0.85), his light strike 76 (70); Revenant's revolver 30 every 0.74 s (32 / 0.68), Obliteration 92 (100); Sevarog's hammer 35 (34) | the band test once the approach counts |

The hero browser spells the trait out for every melee hero (with the numbers from the rules).

### Rounds (the numbers decide, not the first guess)

| Round | Duel lab: melee wins | 5v5, 4 matches: melee k / d / damage | ranged k / d / damage |
|---|---|---|---|
| v20 | 58 % | 1.8 / 1.7 / 8 446 | 2.6 / 2.8 / 12 192 |
| a (+20 % health, +5 armour, resist 20 %, tenacity 30 %, lifesteal 10 %, shield 8 %) | **97 %** | 1.6 / **0.6** / 10 014 | 1.8 / 3.0 / 11 873 |
| b (half: +12 % health, +3 armour, resist 10 %, tenacity 20 %, lifesteal 8 %, shield 5 %) | 88 % | 1.9 / 1.2 / 9 200 | 2.5 / 3.2 / 12 729 |
| **c (shipped: b with lifesteal 5 %, the band fixes)** | **82 %** | 1.5 / 0.6 / 8 532 (2 matches) | 2.1 / 3.1 / 12 588 |

Round a overshot (melee heroes nearly stopped dying); round c is shipped. What it gives: the melee heroes die far less in
team fights (1.7 → 0.6–1.2 a match) and reach them (kills and damage as before or higher), the ranged heroes still deal
the most damage and die the most — the classic split of a MOBA; in a 1v1 the melee hero now wins four duels in five
(the lab starts at the ranged hero's range). The knobs are one line each in the rules (`meleeRangedResist`,
`meleeTenacity`, `meleeDashShieldPct`, `meleeLifesteal`) if play shows the duels too one-sided.

### The balance spec (VR-07) counts the approach

The time-to-kill band test compared stand-up fights: a ranged hero was timed as if it met a melee hero face to face,
which never happens — it shoots while the melee hero closes in (the kite model already in the same spec). Now a ranged
attacker's time against a melee target counts from the moment they meet (its standing time less those free shots), and
the melee trait's resist is in the simulation. All 51 tests pass (VR-07 with the band fixes above).

## 2. The prototype's combat (the new game)

### No guard

Block and parry are gone (the shield, the Q key, the guard break, the bot's blocking, their HUD and lab lines). The
defence is the dodge: a tap of Shift (or the mouse's front thumb button), 5.3 m, 0.3 s invulnerable.
**Perfect dodge:** out of a blow in the first 0.18 s of the dodge — time slows to 30 % for half a second, the attacker
loses its balance for 0.35 s, and the next blow within 1.6 s is a **counter** ×1.5 (and quicker); the crosshair turns gold.

### Built on the mouse

- **The camera aims.** Every attack goes where the camera looks and homes in on the enemy nearest the crosshair
  (within 5.6 m and 50°, or pressed against the body), with a lunge of up to 3.2 m — look at the enemy, click.
- **LMB**: the light combo of three (8 / 10 / 15); held, it keeps going; out of a sprint, the running strike; in the air,
  up to three **air slashes** that keep the body up and drift to the target.
- **RMB**: tapped, the quick heavy (16, a shove); held, the charge (20–46); a full charge **launches** the target 3 m up
  — jump after it and slash (the juggle); in the air, the **slam** (18 in 3.2 m).
- **The wheel**: zooms the camera; locked on, it moves the lock to the next enemy. **The middle button** (or Tab):
  lock-on — the body keeps facing the target and circles it with the 8-way walk and jog. **Thumb buttons**: dodge
  (front) and sprint (back).

### Fluid

- a press during a swing is queued; one before a move is possible waits 0.3 s in a buffer (light, dodge, jump);
- the combo chains right after each blow lands (0.3 s), not after the follow-through;
- the tail of any swing is cancelled by a step (a 0.2 s blend into the run), a dodge or a jump; a launch's heavy can be
  cancelled 0.4 s in, to jump after the target;
- a quick 0.1 s turn into each swing instead of a snap; hit-stop on every blow (longer for the big ones); the camera
  kicks on a blow given or taken, widens in a sprint, closes in while charging, zooms with the wheel.

### More animations (all from the UE template's mannequin set, nothing new downloaded)

8-way walk and jog while locked on (the template blend space: backpedal, strafe, diagonals); hit reactions by the blow's
weight and side — light ×4, medium ×2, heavy, from behind; deaths by the blow's side — front ×3, back, left, right;
air slashes; the landing after a launch, a slam or a long fall (over 12.5 m/s); the dash, the wall jump and the
climb as before.

### The controls at the start

A controls card opens with every match and the game waits for it: a drawn mouse with each button's use, the keys, four
tips (the perfect dodge, the launch and the juggle, cancelling, the sneak attack); LMB, Space or the button start the
fight. F1 opens it again (paused). The prototype's menu shows the mouse at a glance beside its buttons.

### The bot

No blocking. It reads the player: a swing is seen at once but the body moves after the reaction time (0.45 / 0.28 /
0.18 s by difficulty) — a quick jab lands before it can dodge, a heavy or a running strike does not; a combo in progress
is foreseen (out before the next swing lands); a charge is seen and stepped out of, or, close, broken by a quick blow.
The hard bot aims its dodges into the perfect window. It circles the player locked on, mixes combos, quick and charged
heavies, launches, and follows a launch with an air combo.

### Evidence

| Check | Result |
|---|---|
| ProtoLab headless (`Proto -game -nullrhi -ProtoLab`), 26 checks | **26 / 26 PASS** |
| Duel headless, hard bot (`-ProtoDuel -ProtoDiff=2`) | spots the player at 29 s; blows by kind: light 818, finisher 233, quick heavy 153, launch 139, air slash 342, slam 37, charged 25; no game errors |

The new lab lines: a dodge 530 cm and a late blow during it is dodged, not perfect; a three-hit combo 33 (33); a
tapped heavy 16 with the heavy reaction; a full charge launches (46, thrown up 4.5 m with the juggle); air slashes hit
the launched target (2 hits, 14); a perfect dodge (counter open, no damage) and the counter ×1.5 (12); a swing where the
camera looks away misses, where it looks homes in (facing within 0°); hit reactions by side (back, then front); the
sneak attack 20; the slam 18; locked on, circling: facing the target within 6°, moving sideways at 460 cm/s; the bot
lands hits, mixes its moves and reads the player's swings (a dodge on a read, a charge interrupted).

Found and fixed by the lab: a jump pressed just before the launch's heavy could be cancelled waited in the buffer and a
later click replaced it (the heavy is now cancellable 0.4 s in); the bot read swings only on its think ticks (every
0.2–0.45 s) and missed most of them (it reads every frame now, with its reaction time as the delay).

The lab's bot fight is repeatable now (the lab seeds the dice: `FMath::RandInit` at its start; two runs gave the same
26 / 26 and the same counts). A jab the bot cannot react to in time no longer uses up its read: it foresees the combo's
next swing instead (the check "the bot reads the player's swings" was a coin toss before).

## The packaged exe (Build/Windows, Shipping, commit be3898a)

Verified headless (no window on the operator's desktop): `ParagonArena.exe -nullrhi -ArenaStartMap=Proto -ProtoLab` —
**26 / 26** in `Saved/ProtoLab.txt` (68 s); an Arena bot match (seed 2, 3 min) finished, 1 hitch, no crash reports;
`GameUserSettings.ini` restored byte for byte. The cook carries the new animations (the medium and back hit reactions,
the side deaths, the 8-way jog) and the Proto map. Not verified: the rendered look (a window needs the operator's
go-ahead).
