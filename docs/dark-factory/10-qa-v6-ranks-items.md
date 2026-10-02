# 10 — QA: v6 hero levels, ability ranks, MOBA items, predictable skills, icons

Scope (operator request of 2026-09-27): keep improving combat, physics and above all the skills — they must work
better, with concrete, predictable behaviour like in every MOBA; proper icons for them and for the shop; items must
build like in MOBA games and matter more; the biggest gap is hero levelling and upgrading skills with the level.
Use free libraries instead of making things first; come back with a new exe.
Every line is reproducible from the repository root; the logs were kept outside the repository.

## What changed for the player

| Area | Now |
|---|---|
| Levels | 1–20 on a growing XP curve (time, shared minion XP within 15 m, kills and assists); every level adds the hero's own growth (health, mana, power, armour, regen, basic attack damage). 10 min of play: level 13–15; 15 min: 20 |
| Skill points | one per level; Ctrl + 1–4 ranks up (the key of an unlearnt ability learns it); abilities 1–3 rank 1–5 at levels 1/3/5/7/9, the ultimate from level 5 (5/9/13/17/20); a pulsing "+" and the free points above the ability bar, rank pips under each icon, locked abilities greyed with "Ctrl+N" |
| Ranks | each rank adds damage, heal, shield, stun, slow, buff time and mana and shortens the cooldown, exactly as the data says |
| Tooltip | aiming an ability shows every number at every rank (the current one in gold), its scaling and what it is now, range/width/radius, displacement in metres and seconds, cooldown after item reduction, mana, what the next rank needs |
| Predictable effects | knockback is a fixed distance (1.8 m for 4 m/s), knock-up a fixed height and air time (1.03 m, 0.92 s for 4.5 m/s), both carried by root motion and stopped by walls; a ground area telegraphs from the cast and its fill reaches the rim the moment it lands (enemy areas red); a shot's lane is exactly as wide as the shot; crits come from items only, every 1/chance hits (25 % = every 4th); crowd control is written over the unit (STUN 1.2, SLOW 47 %, AIRBORNE) and in the player's status row with timers |
| Items | 27 items in three tiers: 9 parts, 8 upgrades, 10 finished items each with a unique passive; owned parts come off the price and are used up; selling for 60 %; the shop shows the tiers, the hero's recommended build, your price, the recipe tree with the parts you own, what an item builds into |
| Icons | game-icons.net (CC BY 3.0; authors in `CREDITS.md`, a credit line on the hero-select screen): 52 icons for all 25 abilities and 27 items, tinted in their colour over the hero's pose |
| Bots | spend points by the hero's skill order (ultimate first), buy their build part by part, use a ready ultimate on any hero in reach |

## Evidence

| Check | Command | Result |
|---|---|---|
| Compile | `Build.bat ParagonArenaEditor Win64 Development ... -NoHotReloadFromIDE` | Succeeded, 0 errors, 0 warnings in `Source/` |
| Icons | `-run=pythonscript -script=Tools/import_icons.py` | `ICONS OK n=52` |
| Materials | `-run=pythonscript -script=Tools/make_materials.py` | `MATERIALS OK` (indicator with `Fill`) |
| Data | `Arena.Data` spec | heroes.json parses; recipes, tiers and rank-5 values validated |
| Unit/spec tests | `-ExecCmds="Automation RunTests Arena;Quit" -nullrhi` | 26/26 Success (new: XP curve and cap, rank gates, deterministic crits, recipe prices) |
| Skill lab | `UnrealEditor <uproject> -game -ArenaSkillLab` | 34/34 PASS (`skilllab3.log`); screenshots `SKILL_Telegraph`, `SKILL_Knockback`, `SKILL_Tooltip`, `SKILL_Shop`, `SKILL_Bar` |
| Aim lab (regression) | `-game -ArenaAimLab -Seed=12` | 14/14 |
| Mechanics lab (regression) | `-game -ArenaMechLab` | 35/35 |
| Animation lab (regression) | `-game -ArenaAnimLab` | 28/28 (knock-up now the real displacement: rise within 60–140 cm) |
| Headless 5v5, 5 min, seeds 12 / 21 | `-game -nullrhi -benchmark -fps=60 -ArenaBotMatch -Seed=N -Minutes=5` | `stuck=0`, `all4=10` (VR-09) |
| Headless 5v5, 15 min, seed 33 | same, `-Minutes=15` | `stuck=0`, `all4=10`, level 20 reached, 31 finished items bought, 452 displacements |
| Headless 5v5, 10 min, seed 7 | same, `-Minutes=10` | `stuck=0`, `all4=10`, heroes level 13–15 at the end |
| Rendered match | `-game -ArenaUIDemo -ArenaBotMatch -Seed=3 -Minutes=3` | 150 FPS average, 87 FPS 1 % low (v5: 133 / 87) |

What the skill lab measures:

| Check | Measured |
|---|---|
| Cleave rank 1 / rank 5 damage | 103 / 222 dealt = the data (90 / 230 base + 80 % power, after armour) |
| slow, knockback, mana, cooldown by rank | 35 % / 47 %; 180 cm both (planned 180); 46 / 66 spent (data 50 / 70, regen during the cast); 7.0 / 5.4 s |
| ultimate telegraph | half full at half the delay (0.49) |
| knock-up | 103 cm up (planned 103), 1.10 s off the ground (0.92 flight + 0.12 hit-stop), stun 1.20 s |
| levels | level 2 exactly at 110 XP; +54 health, +3.0 power per level (Sparrow's data); level 5: the bot took the ultimate and spent all 5 points |
| crits | 25 % crit: 86, 86, 86, 193 (×2.25 with Blade of Infinity), the HUD counts 4, 3, 2, 1 |
| execute, thorns, last stand, spell blade | 115 vs 116 (+25 % below 35 %, 12 armour pen); 5 of 35 reflected (20 % after armour); a 191 shield at 28 % health (25 % of 770); the basic attack after an ability deals +100 % power and uses the charge |
| shot width | a line 8 cm inside the body edge hits, 8 cm outside misses (edge 61 cm = 25 shot + 36 body) |
| recipe | Executioner's Blade costs 1250 with the owned Warhammer (2300 − 1050), the hammer is used up, power +32 (60 − 28) |

## Defects found and fixed in this pass

| Defect (how it was seen) | Fix |
|---|---|
| A ground area landed after twice its delay (the cast waited Delay, then the area waited Delay again); the telegraph showed only the second half | the area spawns at the cast and lands after its Delay; the telegraph fills over exactly that time |
| A basic attack could only repeat when its animation ended (up to 1.2 s), so attack speed from items did little (every other lab swing was refused) | the basic attack's ability ends after its Delay: the cooldown (attack speed) sets the rate |
| Random 10 % crits on every hit, 150 % | no base crit; crit only from items, deterministic, 175 % (225 %) |
| Knockback / knock-up were launches: the distance depended on friction and frame time | fixed displacements by root motion (distance, height, air time from the data) |
| The shot lane (60/90 cm) was wider than the shot (52 cm) | the lane is the shot's width from the data |
| The watched hero in a bot match never bought items | every hero a bot plays shops |
| A bot sat on its ultimate unless its current target was a hero (Kwang never cast it in 5-min matches once the ultimate opened at level 5) | a ready ultimate goes to the nearest enemy hero in reach |
| A respawned bot re-spent its points on fresh ranks before its saved ranks came back (noisy rank log) | ranks are restored first; only a fresh hero auto-spends |
| A shield carried over a refill (lab) | a refill clears the shield |

## Verdict: **Pass (T1 + T2)**

Measured (T1): 26/26 specs, skill lab 34/34, aim 14/14, mechanics 35/35, animation 28/28, bot matches 0 stuck with
all four abilities cast; HUD, tooltip, shop and icons checked on screenshots (T2). Balance of the new ranks and items
and how they feel is the operator's call (T3).

Shipping package: `RunUAT BuildCookRun ... -clientconfig=Shipping -iostore -compressed -IgnoreCookErrors` → `BUILD SUCCESSFUL`, exit 0,
`Build/Windows/ParagonArena.exe` (2026-09-27 15:25), the 52 icons in the package manifest. The packaged exe ran
`-ArenaSkillLab` to the end (all five `SKILL_*` screenshots: telegraph, knockback with the slow over the unit, tooltip,
recipe shop, ability bar) and `-ArenaUIDemo -ArenaBotMatch -Seed=7 -Minutes=3` (all seven `UI_*` screenshots; the shop
prices Mage's Boots at 500 for the watched hero who owns Buty), both exit 0; Shipping writes no log, the screenshots are
in `%LOCALAPPDATA%\ParagonArena\Saved\Screenshots\Windows`. A last 5-min bot match after the final fix (seed 44):
`all4=10`, `stuck=0`, the watched hero bought 12 items.
