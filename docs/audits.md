# Audits

Quality, security and housekeeping reviews. Repeat when the owner asks for a full review, and before every release; record each one below, newest first.

## Checklist for a full review

1. **Open pull requests**, Dependabot's especially (`gh api "repos/raetha/esphome-freematics/pulls?state=open"`; sessions have the REST API, not GraphQL). For each: read the diff; check its CI passed; for a GitHub Action, check the pinned commit is the tag it claims (`git ls-remote https://github.com/<owner>/<action> refs/tags/<tag>`); for the ESPHome pin, its release notes for breaking changes to `modem`, `wireguard`, `mqtt`, `uart` or the components here. Squash-merge good ones into `dev`, never into `main`.
2. **Security alerts:** sessions can't read code scanning, Dependabot or secret scanning alerts: ask the owner.
3. **Checks:** everything in development.md → "Setup and testing" passes, and CI's compile is green on `dev`.
4. **Secrets:** nothing real in `examples/`, no keys or passwords in logs the package writes, the debug MQTT topic level appropriate for LTE.
5. **Vehicle safety:** every bus access follows development.md → "Vehicle safety".
6. **Workflows:** `contents: read` at the top, write only on the jobs that release or push; actions pinned to commit SHAs; no `${{ }}` inside `run:`; no secrets on pull requests (`tests/test_workflows.py` checks the first three).
7. **Docs:** plan.md's status and open questions current; hardware.md's observations recorded; README and CLAUDE.md match how things work.
8. **Leftovers:** stopgaps that a component has replaced, stale comments (comments explain why, never history).
9. **Record it:** a dated section below, and changelog lines for anything users notice.

## 2026-11-20: full review (before the first weekend in the car)

The whole project on `dev`, after phases 1 to 3 worked on the owner's dongle and sleep while parked was built.

- **Pull requests and dependencies:** none open. ESPHome 2026.11.0, pytest, PyYAML, ruff and clang-tidy are the latest; clang-format stays at 23.1.3 (23.1.5 is out) because it matches ESPHome's own CI. Actions at their latest tags, pins checked against the tags. ESPHome's `dev` has nothing new in `modem` that matters here (one refactor of `setup()`); the SIM7070 SIM issue drafted for the owner to post upstream, with #22957 (SIM7600) likely the same cause.
- **Security:** CodeQL (Python, C++, Actions security-and-quality suites, CodeQL's latest bundle) over the tracked files: nothing in Python or the workflows. C++: "file created without restricting permissions" on the card's files (FAT on an ESP32 has no permissions), "unused static variable" on each component's `TAG` (used through ESPHome's log macros, which a build-less analysis doesn't expand), and two test CLIs that skip a character by advancing their loop counter (deliberate); no changes. Workflows: `contents: read` at the top, write only where needed, checkouts without credentials, actions pinned, no `${{ }}` in `run:` (`tests/test_workflows.py`). Logs: the SD log redacts secrets; the examples hold no real values.
- **Correctness:** host tests, ruff, clang-format, clang-tidy, the markdown and release checks pass; the Traccar server tests pass against a local Traccar 6.17; every example (with its optional packages) and every component test compiles.
- **Vehicle safety:** `freematics_link`'s fixed command set now includes `ATGPSOFF` and `ATLP` (co-processor power, answered locally); `bench-obd-listen.yaml` sends only co-processor set-up commands and is in no configuration.
- **Fixed:** the standby package armed wake-on-motion before powering the modem down, so a door shut in those seconds woke the device at its first check (now armed last); its SIM7600 power-down sent the SIM7070's `AT+CPOWD=1` (now `AT+CPOF`, as Freematics' library does); the changelog said the SIM7600 works (untested); the `[Unreleased]` section had history and overlapping entries (rewritten as first-release notes); plan.md's status and component table, configurations.md's parked data use and Traccar `status.timeout` advice, networking.md and the README predated sleep while parked; comments that cited test dates rewritten as reasons. Removed `examples/bench-modem-esphome.yaml`, whose question is answered and whose copy of the start-up had drifted.
- **Fixed (from the owner's bench log during the review):** GPS drift started trips while parked (one fix at 6 km/h was enough), which in the car would also have kept standby awake; a trip now needs 3 fast fixes in a row and 50 m from the parked position, with host tests for drift, sustained speed and the parked position moving with each stop.
- **Needs a decision / proposed:** altitude isn't sent to Traccar (GH3000 carries it in 2 bytes, left out for data: about 0.8 KB an hour of driving, 7% more); the owner's call. the "took a long time for an operation" warnings (55 to 70 ms) come from SD card writes on the main loop: each stored record is written to the card when it's queued (inside `freematics_link`'s fix callback) and synced every 30 s. Harmless for now (it delays the loop, nothing is lost), but against ESPHome's no-blocking rule; the fix is to give `file_queue` a writer task, as `file_logger` has. Proposed for after the weekend.
- **Held back:** the remote branches `try/freematics-link` and `try/sd-storage` are merged and stale; sessions can't delete remote branches (the owner can).
- **Not measured yet:** sleep current and the data a 6-hourly report costs; the weekend's SD log gives the second.

