# Field report: the Dark Factory Patterns on a game project

A field test of [the Dark Factory method](https://github.com/OneDro1d/dark-factory) (*autonomous, governed,
evidence-gated delivery*) away from its home ground of services and pipelines. The subject is a real-time 3D game in
Unreal Engine 5.8, built in seven days by one AI agent for one human.

This page records:

- what was applied and how;
- where defects were caught;
- where the run departed from the method;
- the conclusions, and the options they open for the method's author and for anyone trying DF on a game.

The project story is in [BUILT-WITH-CLAUDE.md](BUILT-WITH-CLAUDE.md), and the evidence catalogue is in
[TESTING.md](TESTING.md). Before publication, a separate read-only agent checked every claim on this page against
the repository. The corrections it led to are part of the record (section 6).

## Summary

- **Tested:** the DF method at `OneDro1d/dark-factory@88223d2`, still the method's `main`, plus a local gamedev
  layer. Claude ran it in Claude Code from 2026-09-26 to 2026-10-02.
- **Result:** a 5v5 MOBA with 11 heroes, two modes, bots, LAN play and two prototypes, shipped in 22 versions. Each
  version came with evidence: 51 automation tests, ~15 self-checking in-game labs, seeded bot matches and, in most
  releases, a check of the packaged build.
- **What carried it:**
  - evidence over summaries;
  - pure rules tested headless;
  - labs that drive the real game;
  - the packaged build treated as the authority;
  - decoys that prove a check can fail;
  - one blind adversary review, which found 13 real defects.
- **What a game adds to the method:**
  - much of the truth is visual, auditory or a matter of taste (T2/T3), and headless evidence cannot see it;
  - the packaged build differs from the editor;
  - the workstation itself (disks, memory, GPU) is the infrastructure.

## 1. Setup

| | |
|---|---|
| Method | [OneDro1d/dark-factory](https://github.com/OneDro1d/dark-factory) at `88223d2` (the merge of PR #220, 2026-09-23); still identical to `main` on 2026-10-03 |
| Kit | a personal kit built from the method on 2026-09-26: 53 skills (the method's 43 plus 10 gamedev skills: `df-gamedev-build`, `df-unreal`, `df-unity`, `df-blender-3d`, `df-game-qa`, `df-npc-ai`, `df-combat-systems`, `df-character-controller`, `df-level-design`, `df-game-physics`) and 5 agents (`df`, `df-unreal-engineer`, `df-unity-engineer`, `df-3d-artist`, `df-game-verifier`) |
| Engine tooling | Unreal Engine 5.8 (C++, Python editor scripting and headless commandlets for nearly all of the work); Blender 5.2 headless; a small DF evidence toolset for Epic's Unreal MCP ([`Plugins/DFToolset`](../Plugins/DFToolset)), used while the project was set up |
| Agent | Claude in Claude Code. Opus was the main agent and ran the blind review; some helper agents (translation, research) ran on Sonnet |
| Mode | **interactive**: the operator stated each version's goal in a sentence or two (in Polish) and played the result. The agent ran each version end to end and stopped only at hard stops and genuine decisions |
| Continuity | the agent's persistent memory notes plus the design and QA documents in this repository. The main session's context was summarised ten times, and nothing was lost that those two sources did not hold |
| Not exercised | the method's enforcement hooks, the `vinculum-map` / `operator-todo` / notepad artifacts, and unattended `df-mission` runs: the run was interactive and version by version |

## 2. The stages in practice

| DF stage | Skill | What it produced here | Fit for a game |
|---|---|---|---|
| Product owner | `df-product-owner` | [`01-game-design.md`](dark-factory/01-game-design.md): pillars with numeric proxies, non-goals, 30 validation rules (VR-01…VR-31; there is no VR-11) and 14 scenarios (GS-01…GS-14). Every fact is tagged **[C]** confirmed, **[A]** assumed or **[O]** open | **Strong.** Rules like "a knockback is a fixed distance, stopped by walls" and "no bot is stuck for more than 3 s" turned straight into checks |
| Solution architect | `df-solution-architect` | [`02-technical-design.md`](dark-factory/02-technical-design.md): a locus table that gives the core rules (VR-01…VR-10) an enforcement point and a mechanism, plus 14 ADRs. Later rules were enforced in specs and labs without being added to the table | **Strong, but traceability lagged.** The data-transform lens fits game rules. Damage, gold, ranks, the bot brain and Conquest's protection chain are *pure* functions; spawning, physics and replication are the *effects*. The table was not extended as rules were added |
| Infrastructure | `df-infrastructure` | no DTAP environments, only one Windows workstation; packaging recipes and disk budgets live in the QA reports | **Recast.** The real infrastructure risks were on the machine: a project drive whose filesystem needs a repair, and a system drive that filled up during a cook |
| Observability | `df-observability` | a "consumable surface" of log lines and files: `ARENA_SUMMARY`, `DEATH_AUDIT`, `ARENA_BOTS`, `Saved/ArenaPerf.txt`, `ProtoLab.txt`, `Teaser.txt` | **Adapted.** A Shipping build writes no log, so any evidence the packaged game must give is written to files |
| TDD | `df-tdd-developer` | 6 automation specs: 17 tests in v1, 51 today. The test list follows the validation rules | **Strong** for rules. Not enough on its own for feel, animation, rendering or audio |
| QA | `df-qa`, `df-game-qa` | ~15 in-game **labs** (`-ArenaSkillLab`, `-ArenaFxLab`, `-ProtoLab`…) that script the real game and print `LAB PASS/FAIL` with screenshots. Also: seeded headless bot matches, packaged-exe checks, and 20 QA reports over the 22 versions. Explicit T1/T2/T3 verdicts run up to v11; the later reports list the evidence and the open items without tier labels | **The heart of it.** A lab is the game equivalent of an integration test with an unforgeable trace. The tier labels lapsing is a deviation (conclusion 3) |
| Adversary | `df-adversary-gate`, `df-game-verifier` | one blind, read-only review of the network and Conquest code, which found 13 defects, all confirmed; decoy checks that must fail | **High value, used too rarely:** one formal blind review in 22 versions |
| Control loop | `vinculum-loop`, `dark-factory-build` | the "decide what you can, come back only when done or blocked" contract, held interactively. Hard stops were respected: publishing, spending, downloads, deleting the owner's data, opening windows on the owner's desktop | **Partial:** practised in spirit, without the map or mission artifacts |
| Dispatch | `df-dispatch-subagents` | helper agents for research catalogues, a version timeline, translation passes and the blind review | **Useful, not to the letter.** The dispatch prompts were plain task descriptions, not the PROMISE / EVIDENCE / BOUNDS form the method asks for. One helper's count of 50 tests reached a README draft before a real run showed 51 |

## 3. Where defects were caught

The clearest way to judge an evidence-gated method is to ask who caught each defect, and when. The examples below come
from the QA reports; the list is representative, not exhaustive.

| Caught by | Examples | Reference |
|---|---|---|
| **A lab** | explosive shots that dealt 0 damage (FX lab); a prototype dodge that travelled 8.9 m because the animation's root motion stacked on the code's force; a duel bot that took 23 minutes to find the player (ProtoLab); a prototype bot that stopped finding the player after a map was saved from a headless editor run (ProtoLab 23/26 → map regenerated → 26/26) | [QA 14](dark-factory/14-qa-v9-hud-abilities.md), [QA 21](dark-factory/21-qa-v20-bot-jumps-proto-mode.md), [QA 24 §7](dark-factory/24-qa-english-ui-teaser.md) |
| **The packaged build** | the cook silently dropped ~150 packages while the build reported success (flat brown floors in the exe); the Conquest map never loaded in the exe (a Shipping build ignores a map on the command line); the IoStore build never mounted a translation patch | [QA 17](dark-factory/17-qa-v14-v16-conquest-heroes-lan.md), [QA 18](dark-factory/18-qa-v17-ui-maps-movement-balance.md), [QA 24 §1](dark-factory/24-qa-english-ui-teaser.md) |
| **The blind adversary** | 13 network and Conquest defects: client-side damage authority, unvalidated RPC indices, victory text replicated from the host's side, lost gold, a bot replacement with no lane, and more. The green specs and a passing LAN lab (host 4/4, guest 7/7) had missed all of them | [QA 17](dark-factory/17-qa-v14-v16-conquest-heroes-lan.md) |
| **Measurement** | the first melee-balance round overshot (97 % melee wins) and was brought back to 82 % over further rounds; bot deaths fell 217 → 150 → 113 across two versions of the decision rules | [QA 22](dark-factory/22-qa-v21-melee-balance-proto-combat.md), [QA 19](dark-factory/19-qa-v18-bots-terrain-playtest.md), [QA 20](dark-factory/20-qa-v19-relaxed-idle-ui-style-bot-decisions.md) |
| **The operator (escaped the checks)** | wrong team colours; a key on cooldown blocking attacks; a broken minimap; pause-menu clicks doing nothing; bots that never jumped; a stiff arm in the idle pose; fonts much too big; a teaser camera "too high", then "too close" | [QA 15](dark-factory/15-qa-v10-v11-menus-casting-minions-lighting.md), [QA 16](dark-factory/16-qa-v12-v13-network-voices-menu.md), [QA 21](dark-factory/21-qa-v20-bot-jumps-proto-mode.md), [QA 20](dark-factory/20-qa-v19-relaxed-idle-ui-style-bot-decisions.md), [QA 24 §4](dark-factory/24-qa-english-ui-teaser.md) |

The defects that escaped share one trait: none was a broken rule. Each was something a person sees or feels: a
colour, a pose, a camera, or a behaviour nobody had written a rule for. After most of these reports the agent turned
the report into a check so the same defect could not escape again; examples are the `UI-PAUSE` lab step and the
jump counts in `ARENA_BOTS`. The minimap and the teaser camera got no check.

## 4. Conclusions

1. **Evidence beats summaries, but only evidence that can see the claim.** The English version was first treated as
   done on the strength of headless labs, which pass whatever language the text is in. A rendered UI tour then showed
   Polish hero descriptions: the data patch had never been mounted. The evidence was real but did not cover the
   promise. That is exactly what `df-adversary-gate` asks ("does the evidence prove the promise?"), and no
   independent check was there to ask it. *Implication:* before calling a claim done, name the evidence that could
   falsify it.
2. **For engine work, the packaged build is the authority.** Three of the most serious defects existed only in the
   Shipping build. *Implication:* "verify in the packaged artifact" belongs among the method's default gates for engines.
3. **Headless evidence is blind to text, pixels and sound, and the T-labels need a keeper.** Games need T2
   evidence (screenshots, contact sheets, rendered tours). Those runs open a window on the owner's desktop, which is a
   hard stop, and that was the main source of friction and waiting. Separately, the explicit T1/T2/T3 verdicts
   stopped after v11, once the pace picked up. *Implication:* render off-screen (UE's `-RenderOffScreen`, a virtual
   display or a CI GPU) so T2 evidence can be produced autonomously, and check that every QA report carries a tier
   verdict.
4. **T3 is frequent and expensive in games.** Taste decided the camera height (three attempts), the music and the
   pacing of the teaser. *Implication:* calibrate taste cheaply first: two or three variants as stills or short clips
   before committing to a long render or a full feature.
5. **The adversary gate pays off when it is independent, and it should run every release.** The one blind review
   found 13 confirmed defects that the green specs and the LAN lab had missed. A second independent read-only pass,
   over this report, found an over-count, a misattributed defect and overclaims in the docs (section 6). The kit has a
   `df-game-verifier` agent, and it should run on every version, not once.
6. **Decoys make checks trustworthy.** A malformed `heroes.json` must be rejected, and reverting the pause-menu fix
   must turn its check red. A check that had never failed was treated as unproven.
7. **The data-transform lens fits games better than expected.** Pure rules (damage, ranks, gold, the bot's decisions,
   Conquest) were tested headless without starting the game. Effects (spawning, physics, replication) were left to
   labs and bot matches. The split kept the fast tests fast and honest. Traceability did not keep up, though: the
   rule-to-locus table covers the first ten rules only.
8. **Governance belongs in the product-owner stage, as data.** Epic's listing term *"You may not use the trademark
   PARAGON to advertise or name your game"* was known from v9 but deferred; the teaser's QA report still reads "a
   rename would be needed". So it forced a rename at release time. The "NoAI" flag on the listings and the full
   license review surfaced only at release. *Implication:* the PO's `governance` field should carry each content
   source's license and trademark terms from day one, and the publish gate should check them.
9. **Continuity held without the vinculum map, but implicitly.** The agent's memory notes and the QA documents
   carried the project through ten context summaries. They are not the method's standard artifacts, though, and the
   mission state ("what is open, who owns it") lived in the conversation. *Implication:* the next run should use
   `vinculum-map` and `operator-todo`, so the state can be inspected by the human and by another agent. The method's
   `handoff-precompact` hook pair exists for exactly this.
10. **For gamedev, the machine is the infrastructure.** Three examples:
    - The project drive's filesystem needs a repair, and one test (`Arena.Data`) still fails on it.
    - The system drive filled up during a cook, and the cook silently dropped packages.
    - Staging later ran short of space again because the pagefile had grown to 28 GB, and an old build had to be
      deleted.

    *Implication:* an environment pre-flight is a natural infrastructure stage for engine lanes. It would check
    filesystem health, free space on every drive involved, available memory and GPU state.
11. **Delegation works when both the promise and the evidence are crisp, and slips when they are not.** Helper agents
    produced research catalogues, a timeline, translations and a blind review. One helper's unverified count (50 tests)
    reached a README draft and was corrected only after a real run showed 51. The method's PROMISE / EVIDENCE / BOUNDS
    dispatch form exists to prevent exactly this.

## 5. Options: what next

These are options, not decisions; each belongs to the method's author or to this project's owner.

**For the method**

| Option | What it would take | Expected benefit |
|---|---|---|
| A. An engine lane in the method | promote these into a gamedev reference or skill: the packaged-build check, decoy-first labs, off-screen rendered evidence, the environment pre-flight, and "never save engine assets from a headless run" | conclusions 2, 3, 6 and 10 become defaults instead of lessons |
| B. A taste-calibration step for T3 | a skill step: produce N variants as stills or 5 s clips, let the human pick, then build | fewer "too high / too close" loops, and less time spent on long renders that miss |
| C. Governance of third-party content at PO time | license, trademark and AI-use terms as `governance` fields of each content source, checked again by the publish gate | no late renames; terms like "NoAI" handled up front |
| D. A per-release adversary for games | run `df-game-verifier` (or `df-adversary-gate`) on every version's evidence, read-only and blind to the implementation notes | catches evidence that does not cover the claim (conclusion 1) before the human does |
| E. Proxy evidence for what the agent cannot perceive | a catalogue of measurable proxies: loudness and spectrograms for audio, contact sheets for motion, OCR for on-screen text | T2/T3 claims become partly checkable without a human |

**For this project** (a re-run on the same game is a controlled comparison)

| Option | What it would take | What it would show |
|---|---|---|
| F. A gated, autonomous re-run | open the next feature as a `df-mission` with the method's hooks wired into the project, `vinculum-map` and `operator-todo` in use, and `df-game-verifier` on every release | interactive vs autonomous and gated, on the same codebase: escaped defects, human interventions, time |
| G. Publish the gamedev layer | release the 10 gamedev skills, the 5 agents and `Plugins/DFToolset` as an open DF kit (the owner's decision) | others can reproduce this test on their own engine projects |
| H. Metrics per version | add "defects found by the checks vs by the human" and "hard stops hit" to every QA report | a running measure of how much the checks carry |

## 6. How this report was checked

A separate read-only agent took every claim in a draft of this page and either found its source (file and line) or
flagged it. It led to these corrections:

- the rule count is 30, not 29;
- the rule-to-locus table covers ten rules, not all of them;
- one lab finding was attributed to the wrong report;
- the adversary review was said to beat "43 green tests", but the green checks at review time were the specs and the
  LAN lab;
- tier verdicts appear only up to v11;
- two infrastructure details had no source in the repository;
- the trademark term was known from v9, not first seen at release.

The same corrections were made in [BUILT-WITH-CLAUDE.md](BUILT-WITH-CLAUDE.md) and the README where they applied.

## 7. How to inspect the evidence

- The design that drove the work: [`01-game-design.md`](dark-factory/01-game-design.md) and [`02-technical-design.md`](dark-factory/02-technical-design.md).
- The QA reports, each starting with the operator's request and listing the evidence: [`docs/dark-factory/`](dark-factory/).
- Every lab, test and switch, and how to re-run them: [TESTING.md](TESTING.md).
- The teaser, produced by the same evidence-driven pipeline (an in-engine director, measured frames, contact-sheet
  review): [media/](../media/).
