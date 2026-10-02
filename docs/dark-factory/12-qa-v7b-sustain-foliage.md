# 12 — QA: v7b MOBA sustain (recall, fountain, potions, death recap, shutdown) and the foliage check

Scope (operator requests of 2026-09-27): keep improving the game elements after the top MOBAs, report progress; no
grass or plant may hang in the air or stick out of a ramp anywhere, from any angle.

## What changed for the player

| Feature | Rule (data in `heroes.json` rules) |
|---|---|
| Recall | B away from the base: 6 s channel with a bar, then home (a free spot at the fountain). Moving, casting, a blow, a stun or a knock breaks it; B again cancels. In the base B opens the shop; P opens the shop anywhere to look and plan (buying still needs the base or death) |
| Fountain | within 11 m of your base: +12 % of max health and mana per second; an enemy hero there burns for 25 % of max health per second, true damage (nobody camps a base) |
| Potions | health (+220 over 8 s) and mana (+160 over 8 s) for 50 gold, up to 5 of a kind, keys 5 / 6, one at a time; bought only in the shop; kept through a death |
| Death recap | the death panel names the killer and lists what hit you in the last 10 s (who, which ability, how much, how many times), the biggest first |
| Shutdown | a hero with 3+ kills since its last death carries 100 gold per kill above 2 (max 400) for whoever ends the streak; announced |
| Bots | drink a potion under 55 % health away from the fountain, recall when retreating with no enemy hero within 25 m (break it when one comes within 10 m), never chase into or stand at the enemy fountain, buy two potions when they shop, keep mana for a ready ultimate |

## Foliage check (`-ArenaFoliageCheck`)

Every plant, stone and tree instance of the play area is traced to the ground under it: floating = its base more than
6 cm above the first surface below; buried = its base inside blocking geometry (a ramp, a wall, a terrain block).

| Run | Result |
|---|---|
| after the v7a map pass (screenshot review only) | 30 floating (1.5 m over the pit floor at the foot of both pit ramps), 40 buried (in the pit-edge walls, under the plateau walls, at the pit wall foot) |
| fixes | ramp edge bands only along the two long sides (a pit ramp is wider than long: its ends had been planted at ground height, one end is in the pit); pit area and edge-wall thickness excluded from ground cover and edge bands; screes in front of the plateau walls; pit-foot band 2 cm off the wall |
| final | `FOLIAGE_SUMMARY floating=0 buried=0 checked=12897` |

Eight low-angle views of every ramp type (`Saved/cams_v7d.txt`) confirm it; plants stand at the parapet feet, none on
a slope.

## Evidence

| Check | Result |
|---|---|
| Unit/spec tests | 27/27 Arena specs (new: shutdown gold) |
| Base lab `-ArenaBaseLab` | 11/11: recall home in 6 s (3 m from the centre), a blow breaks it, fountain heals 25 % in 2 s (12 %/s), burns an intruder 49 % in 2 s (25 %/s) and the recap names it, two potions for 100 gold in the base and none away from it, one potion at a time, a potion heals 220 over 8 s (measured regeneration taken off) |
| Bot matches after the bot fixes | 5 min seeds 12 / 44: 10/10 heroes cast all four abilities; 10 min seed 21: 10/10; `stuck=0`; recalls 9 of 12 completed (was 5 of 38 before the thresholds); 107 potions drunk, 4 shutdowns in a 10-min match |
| Six more 5-min matches (seeds 1–6) | all 10/10, `stuck=0` |
| Rendered match | death panel with the recap, the recall bar, potions over the item grid (`UI_05_Death`, `BASE_Recall`) |

Found while checking: Kwang reached level 20 with his ultimate at rank 5 and never cast it — the bot spent every
ready ability and never held 120 mana in a fight. Bots now keep the mana a ready ultimate needs.

Open (T3): over nine 5-min seeds team A won 7; the teams and the map are mirrored, so this may be chance — to watch.

## Verdict: **Pass (T1 + T2)**

Shipping package: `RunUAT BuildCookRun ... -clientconfig=Shipping -iostore -compressed -IgnoreCookErrors` → `BUILD SUCCESSFUL`, exit 0,
`Build/Windows/ParagonArena.exe` (2026-09-27 17:07, 55 icons in the manifest). The packaged exe ran `-ArenaBaseLab` to the
end (`BASE_Recall`: the recall bar, the potions over the item grid) and `-ArenaUIDemo -ArenaBotMatch -Seed=3 -Minutes=3`
(all seven `UI_*` screenshots: the textured map, the shop with the potions under the upgrades, the death panel with the
recap), both exit 0; screenshots in `%LOCALAPPDATA%\ParagonArena\Saved\Screenshots\Windows`.
