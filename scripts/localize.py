#!/usr/bin/env python3
"""Turn an example device config into one that uses this checkout instead of GitHub.

The examples fetch the packages and components from GitHub (`url:` packages and a
`github://` external component), so users can paste them into ESPHome Device
Builder. CI and local testing need the files in this checkout instead:

    python3 scripts/localize.py examples/car-traccar.yaml build/local

writes build/local/car-traccar.yaml with the remote package files as `!include`s
of packages/ and the external components from components/. Copy the secrets file
next to it (CI copies examples/secrets.example.yaml).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO_URL = "https://github.com/raetha/esphome-freematics"
_TOP = re.compile(r"^([A-Za-z_][\w-]*):")


def _blocks(text: str) -> list[tuple[str | None, list[str]]]:
    """Split a YAML file into its top-level blocks: (key, lines)."""
    blocks: list[tuple[str | None, list[str]]] = [(None, [])]
    for line in text.splitlines():
        top = _TOP.match(line)
        if top:
            blocks.append((top.group(1), [line]))
        else:
            blocks[-1][1].append(line)
    return blocks


def _package_files(lines: list[str]) -> list[str]:
    body = "\n".join(lines)
    if REPO_URL not in body:
        raise ValueError("packages: doesn't fetch from this repository")
    inline = re.search(r"files:\s*\[([^\]]*)\]", body)
    if inline:
        return [f.strip() for f in inline.group(1).split(",") if f.strip()]
    files, in_files = [], False
    for line in lines:
        if re.match(r"^\s+files:\s*$", line):
            in_files = True
        elif in_files:
            item = re.match(r"^\s+-\s+(\S+)", line)
            if item:
                files.append(item.group(1))
            elif line.strip() and not line.strip().startswith("#"):
                in_files = False
    if not files:
        raise ValueError("packages: lists no files")
    return files


def localize(text: str, root: Path = ROOT) -> str:
    out: list[str] = []
    for key, lines in _blocks(text):
        if key == "packages":
            out.append("packages:")
            for f in _package_files(lines):
                out.append(f"  {Path(f).stem}: !include {root / f}")
        elif key == "external_components":
            body = "\n".join(lines)
            if "github://raetha/esphome-freematics" not in body:
                out.extend(lines)
                continue
            comps = re.search(r"components:\s*(\[[^\]]*\])", body)
            if not comps:
                raise ValueError("external_components: lists no components")
            out += [
                "external_components:",
                "  - source:",
                "      type: local",
                f"      path: {root / 'components'}",
                f"    components: {comps.group(1)}",
            ]
        else:
            out.extend(lines)
    return "\n".join(out) + "\n"


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    src, outdir = Path(argv[0]), Path(argv[1])
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / src.name).write_text(
        localize(src.read_text(encoding="utf-8")), encoding="utf-8"
    )
    print(outdir / src.name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
