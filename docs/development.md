# Developing ESPHome Freematics

## Layout

| Path | What |
| --- | --- |
| `packages/freematics-oneplus-b.yaml` | The board package every device config pulls in; its `esphome: project: version` is the release version |
| `packages/wifi-first.yaml`, `packages/uplink-*.yaml` | Wi-Fi first; one uplink package per configuration ([configurations.md](configurations.md)) |
| `components/traccar_uplink/` | The Traccar uplink component; `teltonika.h` and `tracker.h` are plain C++ with no ESPHome dependencies, tested on the host |
| `examples/` | A complete device config per configuration; CI validates and compiles each with `examples/secrets.example.yaml` |
| `docs/plan.md` | Status, decisions, phases and open questions: keep it current |
| `docs/hardware.md`, `docs/networking.md` | Hardware facts and sources; Wi-Fi/LTE/WireGuard and data use |
| `docs/backlog.md`, `docs/audits.md` | Ideas set aside; review records |
| `scripts/release.py`, `scripts/markdown.py` | CI's changelog, version, test build and release-notes helpers; the Markdown line check |
| `tests/` | Tests for the scripts and workflows, and `test_traccar_uplink.py`, which builds `tests/cpp/codec_cli.cpp` with the host's C++ compiler and checks the encoder against a mirror of Traccar's decoder |

## Setup and testing

```sh
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements-dev.txt
pytest tests/
ruff check scripts/ tests/ components/ && ruff format --check scripts/ tests/ components/
python3 scripts/markdown.py --check $(git ls-files '*.md')
python3 scripts/release.py check
cp examples/secrets.example.yaml examples/secrets.yaml   # git-ignored
esphome config examples/car-traccar.yaml
esphome compile examples/car-traccar.yaml                 # first run downloads ESP-IDF (~1 GB)
```

Claude sessions can run everything but the compile (the session proxy blocks dl.espressif.com); CI compiles when it's run. Lambdas are C++, so YAML with lambdas isn't done until a compile passes.

Testing on hardware: on the bench first (12 V into the OBD plug, a USB cable for logs), then in one car, then the other. Keep a backup of each dongle's stock flash.

## Vehicle safety

The stock firmware triggered warnings on both cars, most likely through its OBD requests. So for now nothing is sent to the vehicle bus at all: no OBD requests, no protocol set-up, no writes, no clearing codes. Only commands the co-processor answers itself are allowed (`ATI`, `ATRV`, the GNSS commands); [hardware.md](hardware.md) lists them. Reading the bus means listening to what's already on it. Active requests are a later, separate decision, tested on one car at a time.

## Changelog

`CHANGELOG.md`'s `[Unreleased]` section is release notes for people using the package, not a development log.

- **One line per paragraph or list item**, in every Markdown file: the renderer wraps it. CI checks it (`scripts/markdown.py --check`; without `--check` it joins the lines).
- **Open with a summary**: a few sentences under the version heading saying why the release matters, before the first `###` section.
- **Never edit an already-released section**; CI compares them with `main`'s.
- **Describe the net difference for a user, not the journey**: something broken and fixed within one unreleased cycle gets no `Fixed` entry.
- **Changes with no user-facing effect** (dependencies, tooling, CI) go under `### Internal`.
- Sections are headed `## [X.Y.Z] - YYYY-MM-DD` ([Keep a Changelog](https://keepachangelog.com/)). Each released section is also its GitHub release's notes, and `[Unreleased]` is the current test build's.

## Versioning

Semantic versioning. Before 1.0.0, a change that makes existing device configs need editing (a renamed substitution, entity or id) is a minor bump; anything else a patch.

**`dev` holds the expected next version** in the board package's `esphome: project: version`. When the first change after a release lands on `dev`, the version moves to what that change calls for; a later change that calls for more raises it. Changes users don't get (docs, CI, tooling) don't move it. The changelog stays `[Unreleased]` until the release pull request dates it. `DRAFT` at the root blocks the first release until the owner says it's ready; the release pull request deletes it.

## Branches and releases

| Branch | What goes there | What a push does |
| --- | --- | --- |
| `main` | Releases only: one squash-merged pull request per release. Never pushed to directly. | Releases the version if it has no tag yet (and there's no `DRAFT`); deletes test builds; resets `dev` (sync-dev). |
| `dev` | Adopted work, and Dependabot's updates. `[Unreleased]` is kept current here. | Publishes a test build when `packages/` or `components/` changed (while CI is manual: when CI is run on `dev`). |
| `try/<topic>` | Experiments that may be dropped. Branched from `dev`. | Nothing by itself: open a (draft) pull request into `dev` to run CI. |

Adopted `try/` work is squash-merged into `dev`; ideas dropped for good get a line in [backlog.md](backlog.md).

Day to day:

1. Claude runs `dev`: small, safe changes are committed to it directly; anything else goes on a `try/<topic>` branch with a pull request into `dev`, which Claude squash-merges once CI is green. Either way the `[Unreleased]` line comes with the change and the checks above pass.
2. The owner builds the test build (the package `ref` pinned to its tag, or `dev`) and tries it on the bench or in a car.
3. `dev` stays releasable: a change that needs the owner's decision first waits on its `try/` branch until they've seen it.
4. Each step updates [plan.md](plan.md): status, log, open questions.

Releasing (the owner decides when):

1. On a branch from `dev`, rename `[Unreleased]` to `[X.Y.Z] - YYYY-MM-DD` with a fresh empty `[Unreleased]` above, check the package's version is `X.Y.Z`, update the compare links, and delete `DRAFT` for the first release. A review per [audits.md](audits.md) comes first.
2. Open the release pull request into `main`, titled "ESPHome Freematics X.Y.Z", with the new changelog section as its body.
3. Squash-merge it. CI tags `X.Y.Z` (no leading "v"), publishes the release with the changelog section as notes, deletes the test builds, and resets `dev` to `main`.

No tags are pushed by hand. CI refuses a pull request into `main` whose version has no dated changelog section (a merge into `main` that releases nothing needs `dev` back on the released version).

### What CI does

`.github/workflows/build.yml` runs **only when started by hand** while the repository is private, to save Actions minutes: from the Actions tab, or `gh api -X POST repos/raetha/esphome-freematics/actions/workflows/build.yml/dispatches -f ref=<branch>`. Run it on a `try/` branch before merging anything with lambdas or C++, and on `dev` to publish a test build. When the repository goes public, the push and pull request triggers come back (they're in a comment at the top of the workflow). Each run:

- `scripts/release.py check`: `CHANGELOG.md` has `[Unreleased]`; into and on `main`, also a dated section for the package's version; released sections match `main`'s.
- The Markdown line check, ruff, and `pytest tests/` (including the host build of the Traccar uplink's encoder and filter).
- `esphome config` and `esphome compile` on every example, with the ESP-IDF toolchain cached per ESPHome version.
- `CI passed`: a gate job that passes only when every check above did, the one status check a `main` ruleset would require.

Then, on `main`, the release and sync-dev jobs; on `dev`, the test build. Every checkout uses `persist-credentials: false` except sync-dev's, the one job that pushes. `tests/test_workflows.py` checks permissions on every job, actions pinned to commit SHAs, no `${{ }}` inside `run:` scripts, credentials dropped, and `CI passed` depending on every check job.

The compile takes several minutes, and the free plan's private repositories get 2,000 Actions minutes a month.

### Test builds

A run on `dev` (by hand, for now) that finds changes in `packages/` or `components/` since the last test build publishes a GitHub pre-release tagged `<expected next version>-dev.<n>` (right after a release, `<next patch>-dev.<n>`), with the `[Unreleased]` section as notes and no assets: a device config pins it as its package `ref`. `<n>` counts test builds of that version. Older test builds are deleted. To publish one anyway, run the Build workflow by hand on `dev`.

### Repository settings

The owner sets these (sessions can't read most of them):

- The repository is private on the free plan, so GitHub offers no rulesets or branch protection: the branch rules (no pushes to `main`, squash-merged release pull requests, `dev` never deleted) are kept by convention. If it becomes public or moves to Pro: a ruleset for `main` (target the default branch, enforcement **Active**, no deletion, no force pushes, a pull request before merging, squash only, no required approvals, **Require status checks:** `CI passed`), and a classic branch protection rule for `dev` (deletions not allowed, force pushes allowed for sync-dev).
- **Settings → General → Pull Requests:** squash merging only; **Automatically delete head branches** on.
- The default branch stays `main`.

## Dependencies

Dependabot proposes updates weekly into `dev` (`.github/dependabot.yml`): GitHub Actions (pinned to full commit SHAs, version in a comment) and the pins in `requirements-dev.txt`, including the ESPHome version CI builds with. Nothing here ships: device configs build with whatever ESPHome the owner runs, 2026.11 or later.

## Audits

A "full review" follows the checklist at the top of [audits.md](audits.md), and is recorded there.
