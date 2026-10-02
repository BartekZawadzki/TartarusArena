# 05 — QA: v1 bot arena (placeholder visuals)

Scope: 01-game-design VR-01..VR-10 and GS-01..GS-12 for the single-player 5v5 arena vs bots, built with
engine-template placeholders (mannequins, prototype blocks) because the Paragon packs were not yet added
to the project. Verdict and evidence below; every line is reproducible with the command given.

## Evidence

| Check | Command (from the repository root) | Result |
|---|---|---|
| Compile, project code | `Build.bat ParagonArenaEditor Win64 Development -Project=... -NoHotReloadFromIDE` | exit 0, 0 errors, 0 warnings in `Source/` |
| Unit/spec tests (VR-01..06, data, bot brain) | `UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Arena;Quit" -nullrhi -ReportExportPath=Captures/tests_m1` → `parse_test_results.py Captures/tests_m1 --expect-min 17` | `TESTS total=17 passed=17 failed=0`, `VERDICT PASS` |
| Decoy: invalid data rejected | spec `Arena.Data` "rejects negative numbers" | PASS (the parser reports a problem) |
| Headless 5v5 bot match, seed 1 | `UnrealEditor-Cmd <uproject> /Game/Maps/Arena -game -nullrhi -ArenaBotMatch -Seed=1 -Minutes=3 -benchmark -fps=60` | `ARENA_SUMMARY seed=1 duration=180 tickets=167/157 heroes=10 casts=1194 all4=10 stuck=0` |
| Headless 5v5 bot match, seed 2 | same, `-Seed=2` | `ARENA_SUMMARY seed=2 duration=180 tickets=192/176 heroes=10 casts=1306 all4=10 stuck=0` |
| VR-09 every hero casts all 4 abilities | summary lines | PASS 10/10 (both seeds) |
| VR-08 no unit stuck > 3 s | summary lines (`evt=stuck` count) | PASS 0 (both seeds). First run FAILED with 18 episodes (ranged bots micro-stepping into blockers) → fixed with strafing + stop-in-range; the failing run is the decoy that proved the check fires |
| GS-08 match runs to the end, 10 heroes | summary | PASS 10/10 |
| Navigation data at match start | `ARENA evt=nav navdata=present` | PASS |
| Rendered play + HUD (editor -game) | `UnrealEditor <uproject> /Game/Maps/Arena -game -ArenaBotMatch -ArenaShots -Seed=3` | screenshots `Saved/Screenshots/WindowsEditor/ArenaShot_*.png`: HUD tickets/timer, kill feed, damage numbers (crit "!"), health bars, ragdolls, ability panel with cooldowns |
| Packaged Shipping build | `RunUAT BuildCookRun -platform=Win64 -clientconfig=Shipping -build -cook -stage -pak -iostore -archive` | `BUILD SUCCESSFUL`, `Build/Windows/ParagonArena.exe` (606 MB) |
| Packaged exe runs a full match | `ParagonArena.exe -ArenaBotMatch -ArenaShots -Seed=6 -Minutes=0.6` | exit 0 after the match (the game only quits via match end ⇒ data loaded, loop complete); screenshots in `%LOCALAPPDATA%/ParagonArena/Saved/Screenshots/Windows` show animated, tinted heroes fighting |
| Decoy caught: characters missing in the exe | first packaged run | FAILED visually (health bars, no bodies): soft-path assets were not cooked → fixed with `DirectoriesToAlwaysCook`; the template ABP_Manny_Platforming broke the cook → removed |
| Player path in the exe | launch → key `1` → key `2` (captures `Captures/exe_*.png`) | hero select screen, player possesses Greystone, over-shoulder camera, HUD; `2` = leap into the enemy group, cooldown starts |
| Stray casts seen once in the exe | same capture | NOT reproduced in a controlled Development run (0 casts by the player hero in 10 s); the capture coincided with the operator using the desktop → UNVERIFIED, re-check by playing |

## Verdict: **Conditional pass**

- Mechanics, rules, bots and the match loop are verified (T1) on two seeds.
- **Conditions (not yet met):** the visuals are engine placeholders, not the Paragon heroes/Agora map the
  operator asked for — they need the operator to add the Paragon packs through the Epic launcher
  (only the account owner can); then `heroes.json` paths and `Tools/build_arena.py` switch to them.
- T3 (fun, feel, "emotional" combat) is the operator's call: play the build.

## Known limitations (routed)

| Item | Lane |
|---|---|
| Placeholder art/animations (mannequins, prototype grid) | assets — waiting on the Paragon packs (operator) |
| Log errors in `-game` runs of the editor binary come from editor-only Python toolsets (Epic's and ours); not present in the packaged exe | tooling, no action for the game |
| Balance table (VR-07) and perf capture (VR-10) not yet measured in a packaged build | QA — next pass |
