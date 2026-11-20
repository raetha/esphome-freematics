"""scripts/localize.py: examples fetch from GitHub; CI builds them from the checkout."""

from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location(
    "localize", ROOT / "scripts" / "localize.py"
)
localize = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(localize)

EXAMPLES = sorted(
    p for p in (ROOT / "examples").glob("*.yaml") if "secrets" not in p.name
)


@pytest.mark.parametrize("example", EXAMPLES, ids=lambda p: p.name)
def test_examples_fetch_from_github_and_localize(example):
    text = example.read_text(encoding="utf-8")
    assert "!include" not in text, "examples must work when pasted on their own"
    if example.name.startswith("bench-"):
        # Bench probes stand alone: no packages, nothing to point at the checkout.
        assert "packages:" not in text and "github://" not in text
        assert localize.localize(text, Path("/repo")).strip() == text.strip()
        return
    assert "url: https://github.com/raetha/esphome-freematics" in text
    out = localize.localize(text, Path("/repo"))
    assert "url:" not in out and "github://" not in out
    for line in out.splitlines():
        if "!include" in line:
            path = line.split("!include ")[1]
            assert (ROOT / Path(path).relative_to("/repo")).is_file(), path
    assert out.count("wifi:") == text.count("wifi:")


def test_local_components_replace_github_source():
    text = (
        "packages:\n  x:\n    url: https://github.com/raetha/esphome-freematics\n"
        "    files: [packages/a.yaml, packages/b.yaml]\n"
        "external_components:\n  - source: github://raetha/esphome-freematics@dev\n"
        "    components: [traccar_uplink]\nwifi:\n  ssid: x\n"
    )
    out = localize.localize(text, Path("/repo"))
    assert "  a: !include /repo/packages/a.yaml" in out
    assert "  b: !include /repo/packages/b.yaml" in out
    assert "      path: /repo/components" in out
    assert "    components: [traccar_uplink]" in out
    assert out.endswith("wifi:\n  ssid: x\n")


def test_other_packages_are_refused():
    with pytest.raises(ValueError):
        localize.localize("packages:\n  x: !include other.yaml\n")


def test_optional_packages():
    text = (
        "packages:\n  x:\n    url: https://github.com/raetha/esphome-freematics\n"
        "    files:\n      - packages/a.yaml\n      # Optional\n"
        "      # - packages/b.yaml\nwifi:\n  ssid: x\n"
    )
    assert localize.has_optional(text)
    plain = localize.localize(text, Path("/repo"))
    assert "b.yaml" not in plain and "  a: !include /repo/packages/a.yaml" in plain
    full = localize.localize(text, Path("/repo"), optional=True)
    assert "  b: !include /repo/packages/b.yaml" in full
