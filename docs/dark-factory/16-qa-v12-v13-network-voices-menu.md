# 16 — QA: v12 fixes and the network foundation, v13 voices / state effects / HUD, the paused-menu fix

Scope (operator requests of 2026-09-28): carry the plan to v16 and come back with a new exe; multiplayer is the goal;
keep fixing what is faulty. During v14 the operator reported: "the menu buttons do not work, you cannot go back to the
menu or quit the game".

## v12 — fixes found by review and bot series (details in QA 15, findings 11–18)

- Kill credit and assists by contribution (the other heroes who hurt the victim in its last 10 s); a dead killer still
  gets its kill on its respawn record; level recomputed from XP.
- Casting and moving are gated by the match phase (no casts in the countdown or after the end).
- Minion waves level up every 2 minutes; bots recall only with no enemy hero within 30 m and no minion within 12 m;
  assassins retreat at 35 %.
- `AArenaGameState` (replicated): phase, clock, score, one record per hero, kill feed; the top bar, scoreboard, kill
  feed and end screen read only it (ADR-11). ARENA_PERF with p95 / p99 and the resolution.
- Balance: the time-to-kill simulator (`Core/ArenaBalance.h`, spec `Arena.Balance`) with role bands (VR-07); Gideon and
  Countess tuned into their bands.

## v13 — voices, state effects, HUD

- The player's hero speaks the pack's lines: ultimate ready, on cooldown / low mana, low health, kills (first blood,
  streak, shutdown), assist, level up / level 5, a rank taken, an item bought, revive, victory / defeat; the pick line
  on the hero select; the intro boast. The manifest cooks the cues (`rules.voiceEvents`).
- State effects from Paragon: Minions (SharedGameplay/States): stun (start + loop over the head), slow, haste, shield
  up / shield hit, the recall column (stopped when the recall breaks), mana potion, level up.
- HUD: the direction of a blow on your hero (red arc around the crosshair); arrows at the screen edge to enemy heroes
  within 35 m off screen; ultimate FX for Gideon, Kwang and Sparrow from their packs.

## The paused-menu bug (operator report)

| # | Finding | Fix |
|---|---|---|
| 1 | **Clicks in the pause menu did nothing** (RESUME, SETTINGS, MAIN MENU, QUIT GAME; the settings opened from the pause too). The pause menu pauses the game (`SetPause`), and Enhanced Input drops every action without `bTriggerWhenPaused` while paused; only Esc and Q had it, the left mouse button did not | the click (and RMB) actions trigger while paused; a click on a menu or while paused never reaches the game |
| 2 | "QUIT" asks twice (a stray Q no longer ends a session), but the only sign of it was a notice hidden behind the menu | the button itself reads "CLICK AGAIN TO QUIT" while armed (3 s) |

Test (`-ArenaUIDemo`, new step UI-PAUSE): the match is paused, then the demo clicks SETTINGS → BACK → RESUME through
the real input path (a simulated left-button press into the player controller at the button's centre; the operator's
own cursor is never moved) and checks that the menu closed and the pause lifted.
Adversary: with the fix reverted the step fails — `LAB FAIL UI-PAUSE: the paused menu stopped answering (menu=6
paused=1 left=2)` — and with it passes — `LAB PASS UI-PAUSE ... (menu=0 paused=0)`.

## Evidence

- Unit / spec tests 31/31 (`tests_v13.log`).
- Headless bot matches: seed 2 132/66, seed 7 133/59; all4 = 10, stuck = 0, yaw snaps 0.
- Foliage: floating 0, buried 0 of 12 664.
- Rendered labs: AnimLab 40 pass / 0 fail, BaseLab 13/0, MechLab 35/0, AimLab 14/0, SkillLab 42/0, FxLab 47/0,
  training 4/0; UI demo 79.2 fps average at 1600×900 in the editor, UI-PAUSE pass.
- Exe (Shipping, 2026-09-28 11:16), a 1-minute bot match: 132.1 fps average, 1 % low 88.2, no hitch; the operator's
  GameUserSettings.ini backed up and restored byte for byte (md5 26a54470…).
