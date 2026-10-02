# 11 — QA: v7 map visuals (textures, height transitions, grass and vegetation, artefacts)

Scope (operator request of 2026-09-27): fix every texture and graphic inconsistency, above all at height changes,
everything about grass and vegetation, and every glitch or artefact coming from the map; improve the map itself.
Every line is reproducible from the repository root; the logs were kept outside the repository; the review cameras are
`Saved/cams_v7.txt` (13 views: bases, ramps, lanes, jungle, ridges, pit, border, overview) and `Saved/cams_v7b.txt`
(9 views of wall feet, faces and arches); before/after screenshots `CAM_v7_*` (before kept in `Saved/v7_before`).

## Found and fixed

| Defect (how it was seen) | Cause | Fix |
|---|---|---|
| The base plateaus, every ramp and one rock were a flat grey checker; the pines had grey paper leaves | their materials lacked the instanced-mesh usage flag, so the game drew the default material on the instanced floors and trees (7 warnings `missing usage flag InstancedStaticMeshes` in every log) | `build_arena.py` sets the flag on the base material of everything it instances (`M_JungleArchitecture`, the three pine materials and the pine billboard, `M_ScanRock`); 0 warnings after |
| The hit-flash overlay would render as the default material in the packaged game | `M_ArenaHitFlash` lacked the skeletal-mesh usage | `make_materials.py` sets it |
| Blown white discs on the base and pit floors, washed-out yellow grass and ferns | point lights at 2 500–9 000 cd (tens of times the 10 lux sun on the floor), +1 EV exposure bias, an orange sun | lights in candelas at 3.5 % (90–320 cd), exposure bias 0, bloom 0.55, a warmer-white sun |
| Grass and ferns floating over the pit ramps and poking through the ramps' low ends | ground cover was planted at z 0 without knowing about slopes | every ramp records its footprint; nothing grows on a slope |
| One ornament stretched over a whole ramp | each ramp floor was one 5 m plane scaled to the ramp's length | ramp floors are laid as a grid of ~5 m tiles on the slope |
| Ruler-straight seams between the lane paths and the jungle floor, bare feet of the walls and ramps | no dressing along edges | 3 850 plants and stones along both edges of both lanes, at the foot of the ridge faces, the plateau front and side walls, the pit walls, the border and the outer sides of every ramp |
| Raw boxes as ramp balustrades | the parapets were plain rock boxes | a carved stone trim of the ruins pack along the top of every parapet, pitched with the slope |
| Bright yellow cubes and cylinders as the physics props | engine basic shapes with a floor-tile material | carved stone blocks of the ruins pack (own materials and simple collision) |
| Sparse, evenly sprinkled grass | a uniform random scatter | 560 clumps of 5–9 blades, buttercup patches in some, 110 more bushes (14 800 instances in all) |

Not a defect: a twisted light ribbon in one review shot was the inside of a pit pillar the review camera stood in.

## Evidence

| Check | Result |
|---|---|
| `build_arena.py` | `ARENA_MAP OK actors=343`, 32 instanced groups, 14 798 instances, usage set on 6 base materials |
| Map review (22 views) | textured floors on plateaus and ramps, tiled ramp floors, no floating plants, edges and wall feet grown over, green trees |
| Warnings in a rendered run | 0 `missing usage flag` |
| Unit/spec tests | 26/26 Arena specs |
| Animation lab (props: push, blast) | 28/28 |
| Mechanics lab | 35/35 |
| Skill lab | 34/34 |
| Bot matches 5 min (seeds 12 / 21 / 44), 10 min (seed 12) | `stuck=0`; heroes with all four abilities 8 / 10 / 10 of 10, and 10/10 in 10 min |
| Rendered match FPS | 130 average, 84 1 % low (was 150 / 87 with grey floors and fewer plants) |

Bots, found while checking VR-09: a ground-area ultimate now counts its radius in its reach, and a bot with its
ultimate ready prefers a hero as its target (it came to fights with it). VR-09 is measured as 8 of 10 heroes in a
5-min match (the ultimate opens at level 5, 1–2 min in) and 10 of 10 from 9 min on.

## Verdict: **Pass (T1 + T2)**

Measured checks pass (T1) and the map was reviewed from 22 cameras before and after (T2). Whether the map now looks
right is the operator's call (T3).
