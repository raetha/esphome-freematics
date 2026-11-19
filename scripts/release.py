#!/usr/bin/env python3
"""Release helpers for CI (.github/workflows/build.yml) and release pull requests.

python3 scripts/release.py check [--release] [--released-from <changelog>]
    CHANGELOG.md has [Unreleased]; with --release (pull requests into and pushes
    to main), also a dated section for the package's version; with
    --released-from (main's CHANGELOG.md), every section released there is
    unchanged here.
python3 scripts/release.py version
    The package's version (esphome: project: version in the board package).
python3 scripts/release.py dev-version <last> <tagged>
    The test build's version: the next patch once the current version is
    tagged (<tagged> = true|false), numbered one past <last> (the latest test
    build's tag, or "") for the same version and from 1 for a new one, so the
    number counts test builds rather than workflow runs.
python3 scripts/release.py notes <version> [--test-build <n>]
    Release notes: that changelog section plus install text ("Unreleased"
    for test builds).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGE = ROOT / "packages" / "freematics-oneplus-b.yaml"
CHANGELOG = ROOT / "CHANGELOG.md"
REPO = "https://github.com/raetha/esphome-freematics"
# The changelog links docs and images on main, where they land with the
# release; a test build's notes link dev's, where they are until then.
_ON_MAIN = re.compile(
    r"(https://github\.com/raetha/esphome-freematics/blob/|"
    r"https://raw\.githubusercontent\.com/raetha/esphome-freematics/)main/"
)

_HEADING = re.compile(r"^## \[([^\]]+)\](?:\s*[-—]\s*\S+)?\s*$")
_LINK_REF = re.compile(r"^\[[^\]]+\]:\s")
_PLAIN_VERSION = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
# The one version line in the package: under esphome: project:
_PACKAGE_VERSION = re.compile(r'^    version: "([^"]+)"\s*$', re.MULTILINE)


def package_version(text: str) -> str:
    found = _PACKAGE_VERSION.findall(text)
    if len(found) != 1:
        raise ValueError("the board package needs exactly one project version line")
    return found[0]


def sections(changelog: str) -> dict[str, str]:
    """Return changelog sections by heading name ("Unreleased", "0.1.0", ...)."""
    out: dict[str, str] = {}
    name: str | None = None
    lines: list[str] = []
    for line in changelog.splitlines():
        heading = _HEADING.match(line)
        if heading or _LINK_REF.match(line):
            if name is not None:
                out[name] = "\n".join(lines).strip()
            name = heading.group(1) if heading else None
            lines = []
        elif name is not None:
            lines.append(line)
    if name is not None:
        out[name] = "\n".join(lines).strip()
    return out


def check(
    changelog: str,
    version: str,
    release: bool = False,
    released_from: str | None = None,
) -> list[str]:
    """Return the problems that should fail CI.

    On dev the package holds the expected next version, which has no section
    until the release pull request dates it; `release` (pull requests into and
    pushes to main) requires that dated section, so an undated release can't
    merge.
    """
    found = sections(changelog)
    problems = []
    if "Unreleased" not in found:
        problems.append('CHANGELOG.md has no "## [Unreleased]" section')
    if release and version not in found:
        problems.append(
            f"CHANGELOG.md has no dated section for version {version}: "
            "date it in the release pull request"
        )
    if released_from is not None:
        for name, text in sections(released_from).items():
            if name != "Unreleased" and found.get(name) != text:
                problems.append(
                    f"CHANGELOG.md changes the released [{name}] section: "
                    "new entries go under [Unreleased]"
                )
    return problems


def dev_version(version: str, last: str, tagged: bool) -> str:
    match = _PLAIN_VERSION.match(version)
    if not match:
        raise ValueError(f"not a plain X.Y.Z version: {version}")
    major, minor, patch = (int(part) for part in match.groups())
    base = f"{major}.{minor}.{patch + 1}" if tagged else version
    previous = re.fullmatch(rf"v?{re.escape(base)}-dev\.(\d+)", last)
    return f"{base}-dev.{int(previous.group(1)) + 1 if previous else 1}"


def notes(changelog: str, version: str, test_build: int | None = None) -> str:
    body = sections(changelog).get(version)
    if body is None:
        raise ValueError(f"CHANGELOG.md has no section for {version}")
    ref = "dev" if test_build is not None else version
    install = (
        "**Use it** from a device config's `packages:` with "
        f"`url: {REPO}`, `ref: {ref}` and "
        "`files: [packages/freematics-oneplus-b.yaml]`, then install the "
        f"firmware from ESPHome. More: [README]({REPO}#readme)"
    )
    parts = []
    if test_build is not None:
        parts.append(
            f"**Test build {test_build}** from `dev`, for trying what's coming. "
            "Pin a device config's package `ref` to this tag to build it. "
            "The changes since the last release:"
        )
        if body:
            body = _ON_MAIN.sub(r"\1dev/", body)
    parts += [body or "_No changes listed yet._", install]
    return "\n\n".join(parts) + "\n"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)
    chk = sub.add_parser("check")
    chk.add_argument("--release", action="store_true")
    chk.add_argument("--released-from", type=Path)
    sub.add_parser("version")
    dev = sub.add_parser("dev-version")
    dev.add_argument("last")
    dev.add_argument("tagged", choices=["true", "false"])
    note = sub.add_parser("notes")
    note.add_argument("version")
    note.add_argument("--test-build", type=int)
    args = parser.parse_args(argv)

    changelog = CHANGELOG.read_text(encoding="utf-8")
    version = package_version(PACKAGE.read_text(encoding="utf-8"))
    if args.cmd == "check":
        released = (
            args.released_from.read_text(encoding="utf-8")
            if args.released_from
            else None
        )
        problems = check(changelog, version, args.release, released)
        for problem in problems:
            print(f"::error file=CHANGELOG.md::{problem}", file=sys.stderr)
        if problems:
            return 1
        print(f"CHANGELOG.md OK for {version}")
    elif args.cmd == "version":
        print(version)
    elif args.cmd == "dev-version":
        print(dev_version(version, args.last, args.tagged == "true"))
    else:
        sys.stdout.write(notes(changelog, args.version, args.test_build))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
