# Tools/ArgusSUT

Tartarus Arena as a system under test for [Argus](https://docs.onedroid.ai/argus). The contract, the safety rules and
the onboarding steps are in [docs/ARGUS.md](../../docs/ARGUS.md).

| File | What it is |
|---|---|
| [`server.mjs`](server.mjs) | the HTTP harness: starts the packaged game's headless checks on request and serves the game's evidence journal (Node 20+, no dependencies) |
| [`checks.mjs`](checks.mjs) | the catalog: each check's parameters and the game switches they become |
| [`selftest.mjs`](selftest.mjs) | the builder's smoke test of the HTTP contract, with one real headless run |
| [`sweep.mjs`](sweep.mjs) | runs every headless check once and prints what each run's journal holds |
| [`compose.yaml`](compose.yaml) | the compose project Argus's local Docker tier attaches to; its `sut-log` container streams the harness log |
| [`argus-config.example.yaml`](argus-config.example.yaml) | a starting `argus-config.yaml` for the tester |

```bash
node Tools/ArgusSUT/server.mjs
```

The scenarios that judge the game are written by the Argus tester and are deliberately kept out of this repository.
