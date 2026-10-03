# Tartarus Arena as an Argus system under test

> **Status: ready for Argus, not yet tested by Argus.** The game is prepared as a system under test (SUT) for
> [Argus](https://docs.onedroid.ai/argus), OneDroid's end-to-end testing platform.
>
> - **What exists:** the game writes its own evidence as JSON, a small HTTP harness starts the packaged game on
>   request, and the configuration Argus needs is drafted.
> - **What does not exist yet:** an Argus workspace, a tester's scenarios, a run and a certificate. Those need access
>   to an Argus control plane, which this project does not have yet.
> - **Where the results will go:** when the tester's run is done, its report and certificate will be added to
>   `docs/argus/`.

## Why Argus, and what it needs from a system

Argus tests a real, running system rather than mocks, and it judges each check on the system's own logs. Its central
idea is the **holdout**:

- A **tester** writes the scenarios in Markdown: a trigger and what healthy looks like.
- The **builder**, the session that built the system, never sees the scenarios. It sees only redacted reports of what
  the system did.
- A final run can be sealed and anchored on a ledger, so anyone can check that the scenarios existed before the run
  and never changed.

This is the independent adversary the [Dark Factory field report](DARK-FACTORY-FIELD-REPORT.md) asks for in its
conclusions 1 and 5, made into a platform.

To be judged that way, a system needs three things:

- **a surface Argus can trigger:** HTTP, a message broker or a database;
- **a correlation id** (`X-Request-Id`) that the system writes into its log lines;
- **structured logs** that Argus's execution plane can read.

The examples in OneDroid's docs are web services. Tartarus Arena is a Windows game with no API, and its Shipping
build compiles logging out. Two additions bridge that gap.

## 1. The evidence journal (in the game)

`-EvidenceJournal=<id>` makes the packaged game write its own log lines as JSON, one per line, to
`Saved/Evidence/<id>.jsonl`. In a packaged build, `Saved/` is `%LOCALAPPDATA%\ParagonArena\Saved`. The id is the
caller's correlation id: up to 64 characters from `A-Z a-z 0-9 . _ -`. Every one of the game's 180 log statements
(the `LogArena`, `LogProto*` and `LogTeaser` categories, plus two `LogTemp` lines) goes through one macro, `ARENA_LOG` in
[`Game/ArenaEvidence.h`](../Source/ParagonArena/Game/ArenaEvidence.h). The macro keeps the normal log line in editor
and development builds and also journals the line when the switch is on. Without the switch, the macro adds nothing but one
check.

```json
{"ts":"2026-10-02T23:58:11.740Z","requestId":"selftest-1790985430689","service":"tartarus-arena","source":"game","level":"info","logger":"LogArena","event_type":"log","event":"ARENA_SUMMARY","seq":681,"msg":"ARENA_SUMMARY seed=1 duration=55 score=30/19 limit=30 winner=0 heroes=10 casts=355 all4=3 stuck=0 items=16 yaw_snaps=1/56226"}
{"ts":"2026-10-03T00:34:59.283Z","requestId":"sweep-skilllab-1790987695523","service":"tartarus-arena","source":"game","level":"info","logger":"LogArena","event_type":"log","event":"LAB","seq":15,"msg":"LAB PASS cast Cleave at rank 1"}
```

| Field | Meaning |
|---|---|
| `requestId` | the correlation id from `-EvidenceJournal=` |
| `level` | `info`, `warn` or `error`, from the log statement's verbosity |
| `event` | the line's kind: its first word (`LAB`, `LAB_SUMMARY`, `ARENA_SUMMARY`, `DEATH_AUDIT`, …), or `x` for `ARENA evt=x` |
| `event_type` | `log` for every line. `saga` for the first line (`EVIDENCE_OPEN`, with the build configuration) and the last (`EVIDENCE_CLOSE`, with the line count) |
| `msg` | the line exactly as the game logs it; the meaning of each kind is in [TESTING.md](TESTING.md) |

## 2. The SUT harness

[`Tools/ArgusSUT/server.mjs`](../Tools/ArgusSUT/server.mjs) is a small HTTP service: Node 20 or later, no
dependencies. It runs on the Windows machine that holds the packaged game. On request it starts the game in one of
its headless check modes and passes `-EvidenceJournal=<X-Request-Id>`. While the game runs, the harness relays every
journal line to its own log, unchanged. It reports **facts, never verdicts**: judging them is the tester's job.

```
Argus execution plane (container) --HTTP + X-Request-Id--> harness (host:8787) --starts--> ParagonArena-Win64-Shipping.exe -nullrhi …
        ^                                                        |                                    |
        |                                         sut.jsonl (harness lines + the game's journal lines, all with requestId)
        +---- Argus's log stack reads it through the compose project's `sut-log` container (Tools/ArgusSUT/compose.yaml)
```

### The HTTP contract

Every endpoint except `/health` needs `Authorization: Bearer <token>`. The token comes from `SUT_TOKEN`, or else
from `Saved/ArgusSUT/token`, which is created on first start. It is never logged. Every response echoes
`X-Request-Id`.

| Endpoint | What it does |
|---|---|
| `GET /health` | liveness: the harness version, the game build (size, date, SHA-256), whether a run is in progress, whether the operator's own game is running, whether rendered runs are allowed |
| `GET /catalog` | the runs it can start, with their parameters and ranges (below) |
| `POST /runs` | `{"check": "<id>", "params": {…}}` starts a run. The run's id is the request's `X-Request-Id`, or a generated id if there is none. Answers `202` with the run and a `Location` |
| `GET /runs` · `GET /runs/<id>` | the last 50 runs · one run: `state` (`running`, `exited`, `timeout`, `cancelled`, `launch_error`), exit code, duration, the build it ran, whether the operator's settings were kept, and the **facts** parsed from the journal: line count, `LAB PASS/FAIL` counts and the failing checks' names, the game's own `PASS/FAIL VR-xx` claims, and the key=value pairs of every summary line |
| `GET /runs/<id>/evidence` | the game's journal for that run as NDJSON; `?event=A,B`, `?level=error` and `?limit=N` filter it |
| `POST /runs/<id>/cancel` | stops a run that is in progress |

| Answer | When |
|---|---|
| `200` with `"replayed": true` | the same `X-Request-Id` with the same body again: idempotent, no second run |
| `400` | invalid JSON, an unknown field, check or parameter, a value out of range, an invalid `X-Request-Id` |
| `401` / `403` | no bearer token / a wrong one |
| `403 rendered_not_allowed` | a rendered run when the operator has not allowed them |
| `404` / `405` | an unknown path or run / a wrong method |
| `409` | `busy` (one run at a time), `operator_playing` (the operator's own game is running), `request_id_reused` (the id already started a different run), `not_running` (cancel) |
| `413` / `429` | a body over 16 KB / more than `SUT_RATE_LIMIT` (20) new runs a minute, with `Retry-After` |
| `503 game_not_built` | no packaged game at `SUT_EXE` |

### The catalog

All entries are headless (`-nullrhi -benchmark -fps=60`: no window, no GPU, a fixed 1/60 s step) except `uidemo`.

| Check | Parameters | The game's mode ([TESTING.md](TESTING.md)) |
|---|---|---|
| `botmatch` | `map` Arena / Conquest, `minutes` 1–20, `seed`, `difficulty` 0–2, `conquest` on / off | `-ArenaBotMatch` |
| `skilllab`, `mechlab`, `fxlab`, `baselab`, `aimlab`, `animlab`, `skindeath` | none | the Arena labs |
| `conquestlab` | none | `-ArenaConquestLab` on Conquest |
| `locolab` | `hero` (optional) | `-ArenaLocoLab` on Conquest |
| `duellab` | `only` (a `Ranged-Melee` pairing, optional) | `-ArenaDuelLab` |
| `foliage` | `map` Arena / Conquest | `-ArenaFoliageCheck` |
| `navcheck` | `map` Arena / Conquest | `-ArenaNavCheck` inside a one-minute `-ArenaBotMatch` (its checks run when a match starts) |
| `protolab`, `hadeslab` | none | the two prototypes' labs on Proto |
| `uidemo` | none | **rendered** `-ArenaUIDemo`: opens a window, so it is refused unless the operator starts the harness with `SUT_ALLOW_RENDERED=1` |

### What the harness guarantees

- **One run at a time**, and **no run while the operator's own game is running**. A copy of the packaged exe that the
  harness did not start answers `409`.
- **The operator's `GameUserSettings.ini` is restored byte for byte after every run.** Every run's record says
  whether the file had changed.
- **It stops only processes it started:** on a timeout, a cancel or its own shutdown.
- **It listens on 127.0.0.1 by default** (`SUT_HOST` / `SUT_PORT`), so it is not exposed to the network, and Windows
  shows no firewall prompt.

Settings: `SUT_EXE` (default: the packaged build under `Build/Windows`), `SUT_GAME_SAVED`, `SUT_SETTINGS_INI`,
`SUT_DATA_DIR` (default `Saved/ArgusSUT`), `SUT_TOKEN`, `SUT_ALLOW_RENDERED`, `SUT_RATE_LIMIT`.

## 3. Onboarding to Argus (when access exists)

These steps follow OneDroid's [tester guide](https://docs.onedroid.ai/argus-tester-guide) and its
[Docker Compose example](https://docs.onedroid.ai/argus-example-documenso). They have not been run for this game yet.

1. **Access.** You need:
   - an account on an Argus control plane, with a workspace for this game (`tartarus-arena`);
   - the `argus` CLI;
   - read access to the execution-plane image, which the control plane's operator gives out.

   The tokens are minted in the Argus app and set as environment variables by the person, never pasted into a chat.
2. **Start the SUT** on the Windows machine with the packaged game: `node Tools/ArgusSUT/server.mjs`. Then bring up
   its compose project: `docker compose -f Tools/ArgusSUT/compose.yaml up -d`. This pulls `busybox` on the first
   `up` and starts the `sut-log` container, which streams `Saved/ArgusSUT/sut.jsonl`.
3. **The tester session** is a separate Claude session in its own folder or machine, holding the **author** token. It:
   - copies [`argus-config.example.yaml`](../Tools/ArgusSUT/argus-config.example.yaml), sets the harness token as the
     `TARTARUS_SUT_TOKEN` secret, and runs `argus validate-config`;
   - onboards on the compose tier with the instance id `tartarus-<initials>-compose`, then checks
     `argus cloud-executor-status` until both `registered` and `poll_accepted` are true;
   - writes the scenarios with `propose-scenario`, `validate-scenario` and `write-scenario`. They live in the Argus
     catalog, **never in this repository**: the builder must not be able to read them.
   - runs them, reads the reds, and seals a final run for a certificate.
4. **The builder**, this project's development session, gets only a **builder** token. It sees redacted reports
   (`runner__get_report`), fixes what goes red and re-runs.
5. **The record.** The tester's final report, the run ids and the certificate go to `docs/argus/`, together with the
   problems hit on the way. The OneDroid docs ask the same of an onboarding example.

What the game offers a tester:

- Argus's HTTP ingestion, error-path, permission and rate-limit layers, through the contract above.
- End-to-end flows: a `POST /runs`, then polling the run, then judging its facts and the game's own journal lines,
  correlated by `requestId`.
- The run lifecycle as saga events: `run.accepted`, `run.launched`, `EVIDENCE_OPEN`, `EVIDENCE_CLOSE`,
  `run.settings_restored`, `run.exited`, `run.collected`.

Which of these to test, and what counts as healthy, is the tester's decision.

## 4. What has been verified so far (by the builder)

This is the builder's own readiness evidence, not an Argus result. It was all run on 2026-10-03 against the packaged
Shipping build with the journal, SHA-256 `7cc9df91a7b4…`.

- **The contract smoke test.** [`Tools/ArgusSUT/selftest.mjs`](../Tools/ArgusSUT/selftest.mjs) passed **28 of 28**
  checks:
  - every refusal in the tables above;
  - one real headless `botmatch` of 1 minute: 34 s on the wall clock, exit code 0, 369 journal lines, every one
    carrying the run's `requestId`;
  - the idempotent replay and the busy refusal;
  - all seven saga events in order;
  - `GameUserSettings.ini` byte-identical afterwards.
- **The catalog sweep.** [`Tools/ArgusSUT/sweep.mjs`](../Tools/ArgusSUT/sweep.mjs) ran all **15** headless
  checks through the harness. Every run:
  - ended by itself, with exit code 0;
  - wrote a journal the game opened and closed;
  - left the operator's settings unchanged.

  The lab checks the game makes of itself came to **374 passed, 0 failed**:

| Check | Wall time | Journal lines | The game's lab checks (pass / fail) | Other facts in the journal |
|---|---|---|---|---|
| `botmatch` (3 min, seed 1) | 188 s | 2,072 | — | `PASS VR-08`, `PASS VR-09`; ARENA_SUMMARY, ARENA_PERF, ARENA_BOTS, DEATH_AUDIT |
| `skilllab` | 34 s | 102 | 42 / 0 | |
| `mechlab` | 35 s | 228 | 67 / 0 | |
| `fxlab` | 165 s | 353 | 102 / 0 | |
| `baselab` | 41 s | 41 | 14 / 0 | |
| `aimlab` | 48 s | 110 | 14 / 0 | |
| `animlab` | 41 s | 198 | 71 / 0 | |
| `skindeath` | 177 s | 709 | — | SKIN_DEATH_SUMMARY |
| `conquestlab` | 39 s | 144 | 22 / 0 | CONQUEST_SUMMARY; `FAIL VR-09` (see below) |
| `locolab` | 172 s | 376 | — | the per-hero locomotion lines |
| `duellab` (Sparrow vs Greystone) | 383 s | 1,662 | — | DUEL_SUMMARY |
| `foliage` | 4 s | 41 | — | FOLIAGE_SUMMARY |
| `navcheck` (in a 1-minute match) | 64 s | 748 | — | `navcheck points=18 unreachable=3` (see below); `FAIL VR-09` |
| `protolab` | 64 s | 68 | 26 / 0 | |
| `hadeslab` | 52 s | 56 | 16 / 0 | |

`FAIL VR-09` is the game's own claim that not every hero cast all four abilities. It comes from the short matches
inside `conquestlab` and `navcheck`, and from the 1-minute self-test match; the 3-minute `botmatch` claims `PASS`.
The harness reports the claim and does not judge it.

**What the first sweep found, and what was done about it.** The packaged build had never been run through every
check before. The first sweep, on the build of the same day, turned up four things:

1. **A stale lab expectation.** `skilllab` reported 41 / 1: "stun: 0.80 s (data 1.00)". This is the v21 rule
   working, not a fault. Since v21, melee heroes shrug off 20 % of every stun (`rules.meleeTenacity`), and the lab's
   dummy is a melee hero, so the stun lasts 0.80 s. The lab still expected the raw value from the data; it had not
   been run since v21. It now plans for the tenacity and reports 42 / 0. The same lab still named the ultimate by
   its old Polish name; it now says Annihilation.
2. **A wrong catalog entry.** `navcheck` is not a mode of its own: its checks run when a match starts. Started
   alone, the game waited in its menu until the harness stopped it at the 600 s timeout. The catalog now runs it
   inside a one-minute bot match, and [TESTING.md](TESTING.md) says so.
3. **Two log lines outside the journal.** The Hades lab's `LAB_SUMMARY` and the HUD's font line used `LogTemp`. They
   now go through the journal too.
4. **Still open, not investigated.** On the packaged Arena map, the nav check reports 3 unreachable points of 18:
   - the path from BaseA to itself, most likely an artifact of the check, since a path to its own start comes back
     empty;
   - partial paths to both ends of the Mid lane: `Lane0_0` at x = −4000 and `Lane0_4` at x = 4000.

   This is the first recorded run of that check.

- **Editor automation tests.** The journal macro did not change their results. There are **50 project tests in 6
  specs**: 49 pass, and the one failure is `Arena.Data`'s asset check, which reads the derived-data cache on drive D:.
  That drive's filesystem needs a repair, as before. A run reports 51 tests because the `Arena` filter also picks up
  one engine test.

## 5. Known limits

- **Not yet checked with the CLI:** `argus validate-config`, onboarding and every Argus step above, because the CLI
  and the execution-plane image are not available here.
- **Networking:** it is not yet verified that an execution plane on Docker Desktop reaches a harness listening on
  the host's 127.0.0.1 through `host.docker.internal`. If it does not, start the harness with `SUT_HOST` set to the
  host's Docker-facing address.
- **LAN play** (`-ArenaNetHost` / `-ArenaNetGuest`, two game instances) is not in the catalog yet.
- **Rendered checks** (`uidemo`, screenshots) need the operator's go-ahead. They open a window on the desktop.
- **Platform:** the harness is for Windows (it uses `tasklist` and `taskkill`). The game is a Windows build. OneDroid's
  onboarding scripts are bash scripts; on this machine they would run from Git Bash or WSL, which is untested.
