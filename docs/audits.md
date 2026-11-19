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
