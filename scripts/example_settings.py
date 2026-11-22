#!/usr/bin/env python3
"""Write every optional setting into the example configs, commented out.

Each example lists, under its substitutions, every substitution the packages it
can load define, with the package's default and comment, so users see what
they can change without reading the packages. The block sits between two
marker lines; this script rewrites it from the packages.

    scripts/example_settings.py            rewrite the examples
    scripts/example_settings.py --check    fail if an example is out of date
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BEGIN = "  # Optional settings, with their defaults: uncomment a line to change it."
END = "  # End of the optional settings."

# A heading for each package's group of settings.
TITLES = {
    "freematics-oneplus-b.yaml": "The board: cellular module, GPS, beeps, logs",
    "wifi-first.yaml": "Wi-Fi first",
    "uplink-traccar.yaml": "Traccar",
    "uplink-home-assistant.yaml": "Home Assistant",
    "uplink-ha-webhook.yaml": "Home Assistant integration (uplink-ha-webhook.yaml)",
    "sd-traccar.yaml": "SD card: records (sd-traccar.yaml)",
    "sd-logs.yaml": "SD card: log (sd-logs.yaml)",
    "sd-logs-download.yaml": "SD card: log download (sd-logs-download.yaml)",
    "standby.yaml": "Sleep while parked (standby.yaml)",
}


def package_settings(path: Path) -> list[str]:
    """The lines of a package's substitutions block, as written there."""
    lines, inside = [], False
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("substitutions:"):
            inside = True
            continue
        if inside and line and not line.startswith(" "):
            break
        if inside and line.strip():
            lines.append(line)
    return lines


def packages_of(example: str) -> list[str]:
    """The package files an example loads, its commented-out optional ones included."""
    return re.findall(r"^\s*#?\s*- packages/([\w.-]+\.yaml)", example, re.MULTILINE)


def block(example: str) -> list[str]:
    out = [BEGIN]
    for name in packages_of(example):
        settings = package_settings(ROOT / "packages" / name)
        if not settings:
            continue
        if len(out) > 1:
            out.append("")
        out.append(f"  # {TITLES.get(name, name)}")
        out += [f"  # {line.strip()}" for line in settings]
    out.append(END)
    return out


def updated(text: str) -> str:
    lines = text.splitlines()
    if BEGIN in lines:
        start, end = lines.index(BEGIN), lines.index(END)
        del lines[start : end + 1]
    else:
        # After the example's own substitutions, before the next top-level key.
        start = lines.index("substitutions:") + 1
        while start < len(lines) and (lines[start].startswith(" ") or not lines[start]):
            start += 1
        while not lines[start - 1].strip():
            start -= 1
    lines[start:start] = block(text)
    return "\n".join(lines) + "\n"


def main() -> int:
    check = "--check" in sys.argv[1:]
    stale = []
    for path in sorted((ROOT / "examples").glob("car-*.yaml")):
        text = path.read_text(encoding="utf-8")
        new = updated(text)
        if new != text:
            stale.append(path.name)
            if not check:
                path.write_text(new, encoding="utf-8")
    if check and stale:
        print(f"Out of date: {', '.join(stale)}; run scripts/example_settings.py")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
