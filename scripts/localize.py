#!/usr/bin/env python3
"""Turn an example device config into one that uses this checkout instead of GitHub.

The examples fetch the packages and components from GitHub (`url:` packages and a
`github://` external component), so users can paste them into ESPHome Device
Builder. CI and local testing need the files in this checkout instead:

    python3 scripts/localize.py examples/car-traccar.yaml build/local

writes build/local/car-traccar.yaml with the remote package files as `!include`s
of packages/ and the external components from components/. Copy the secrets file
next to it (CI copies examples/secrets.example.yaml).

With --optional it also writes build/local/car-traccar-optional.yaml, with the
package files the example lists commented out ("# - packages/sd-card.yaml")
included as well, so CI builds the optional packages too, and the settings an
optional package needs from the device config (REQUIRED below) filled in when
the example doesn't set them.
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


# What an optional package needs from the device config, for the optional build.
REQUIRED = {
    "packages/uplink-ha-webhook.yaml": {"ha_webhook_url": "!secret car1_webhook_url"},
    "packages/sd-logs-download.yaml": {"sd_log_password": "!secret car1_log_password"},
}

_OPTIONAL = re.compile(r"^\s+#\s*-\s+(packages/\S+\.yaml)\s*$")


def _package_files(lines: list[str], optional: bool = False) -> list[str]:
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
            item = re.match(r"^\s+-\s+(\S+)", line) or (
                _OPTIONAL.match(line) if optional else None
            )
            if item:
                files.append(item.group(1))
            elif line.strip() and not line.strip().startswith("#"):
                in_files = False
    if not files:
        raise ValueError("packages: lists no files")
    return files


def has_optional(text: str) -> bool:
    return any(_OPTIONAL.match(line) for line in text.splitlines())


def localize(text: str, root: Path = ROOT, optional: bool = False) -> str:
    out: list[str] = []
    needed: dict[str, str] = {}
    if optional:
        for key, lines in _blocks(text):
            if key == "packages":
                for f in _package_files(lines, optional=True):
                    needed.update(REQUIRED.get(f, {}))
    for key, lines in _blocks(text):
        if key == "substitutions" and needed:
            out.extend(lines[:1])
            for name, value in needed.items():
                if not re.search(rf"^\s+{name}:", "\n".join(lines), re.MULTILINE):
                    out.append(f"  {name}: {value}")
            out.extend(lines[1:])
            continue
        if key == "packages":
            out.append("packages:")
            for f in _package_files(lines, optional):
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
    optional = "--optional" in argv
    argv = [a for a in argv if a != "--optional"]
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    src, outdir = Path(argv[0]), Path(argv[1])
    outdir.mkdir(parents=True, exist_ok=True)
    text = src.read_text(encoding="utf-8")
    (outdir / src.name).write_text(localize(text), encoding="utf-8")
    print(outdir / src.name)
    if optional and has_optional(text):
        out = outdir / f"{src.stem}-optional.yaml"
        out.write_text(localize(text, optional=True), encoding="utf-8")
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
