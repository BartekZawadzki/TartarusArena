# 06 — QA: v2 Paragon heroes and arena

Scope: the placeholder visuals of 05 replaced by the operator's Paragon packs (Greystone, Countess, Gideon,
Sparrow, Kwang; Agora/Monolith props), plus the bot-movement fixes found while re-running the gates.
Every line is reproducible from the repository root; the logs were kept outside the repository.

## Evidence

| Check | Command | Result |
|---|---|---|
| Compile, project code | `Build.bat ParagonArenaEditor Win64 Development -Project=... -NoHotReloadFromIDE` | Succeeded; 0 errors, 0 warnings in `Source/` (engine-header deprecation warnings only) |
| heroes.json paths exist on disk | mapping script check before writing the file | 0 missing across Countess, Sparrow, Kwang |
| Asset manifest | `-run=pythonscript -script=Tools/build_manifest.py` | `MANIFEST OK assets=138 missing=0` |
| Arena map (Paragon dressing, Monolith jump pads, rock props) | `-run=pythonscript -script=Tools/build_arena.py` | `ARENA_MAP OK path=/Game/Maps/Arena actors=534` |
| Unit/spec tests | `-ExecCmds="Automation RunTests Arena;Quit" -nullrhi` | 17/17 Arena tests Success, `TEST COMPLETE. EXIT CODE: 0` |
| Headless 5v5, seeds 7 / 8 / 10 / 12 | `UnrealEditor <uproject> -game -nullrhi -benchmark -fps=60 -ArenaBotMatch -Seed=N -Minutes=3` | `stuck=0` on all four; `all4=10`, `heroes=10`; 833 / 920 / 957 / 930 casts |
| Rendered match with screenshots, seed 12 | same without `-nullrhi`, `-ArenaShots -Minutes=2` | full 120 s match, 8 screenshots; Paragon Kwang, Countess, Gideon, Greystone visible with their own FX, kill feed live |
| Root motion audit | `-run=pythonscript -script=Tools/audit_root_motion.py` | only Gideon `Blackhole_Start` (ultimate, 3.8 s) carries root motion |

## Defects found and fixed in this pass

| Defect (how it was seen) | Fix |
|---|---|
| Two bots locked on top of each other after a knock-up (`evt=stuck` Kwang Z=269 over Greystone) | `AArenaCharacter::SlideOffCharacters`: anyone standing on a character or a loose physics prop is pushed clear |
| Bots standing on 500 kg crates they could neither push nor leave (stuck at Z 190–290) | props weigh 30–40 kg, cannot be stepped onto, do not carve the navmesh (bots shove them) |
| Bots frozen with no recovery (the detector only logged) | unstick: after 1.5 s without progress the bot is kicked sideways and re-paths to a reachable point |
| Crowds body-blocking in melee | RVO avoidance on bot-controlled characters |
| **Metric bug**: the summary counted only live controllers, so episodes of heroes that later died vanished (log had `evt=stuck`, summary said 0) | match-wide `AArenaGameMode::StuckTotal` |
| Metric counted stuns and channelled root-motion ultimates as "stuck" | the stuck clock pauses while stunned or playing root motion |
| An 8K normal map inside Kwang's ultimate FX needs ~4.6 GB to encode → editor/cook out of memory | Kwang's ultimate uses his LightStrike FX; `Tools/cap_textures.py` caps the three debris textures at 2048 |

## Not yet done — blocked on the workstation

| Item | Blocker |
|---|---|
| Final Shipping package of the Paragon version | drive C: at 0 bytes free: UnrealBuildTool/UBA write there and the page file cannot grow (commit limit reached with the other open apps) |
| Balance table (VR-07), perf capture (VR-10) in the packaged build | after the package |

## Verdict: **Conditional pass**

Gameplay, rules and bots verified (T1) with the Paragon roster on four seeds; visuals verified (T2) from the
rendered-match screenshots. Condition: the packaged exe of this version, once C: has room again.
