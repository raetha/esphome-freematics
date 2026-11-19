"""scripts/release.py: changelog checks, test build numbering, release notes."""

from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location(
    "release", ROOT / "scripts" / "release.py"
)
release = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(release)

CHANGELOG = """# Changelog

## [Unreleased]

### Added

- Something new.

## [0.1.0] - 2026-12-01

### Added

- First release.

[Unreleased]: https://example.com/compare
"""


def test_sections_reads_unreleased_and_released():
    found = release.sections(CHANGELOG)
    assert found["Unreleased"] == "### Added\n\n- Something new."
    assert found["0.1.0"] == "### Added\n\n- First release."


def test_check_passes_on_dev_without_a_section_for_the_next_version():
    assert release.check(CHANGELOG, "0.2.0") == []


def test_check_needs_a_dated_section_for_a_release():
    assert release.check(CHANGELOG, "0.2.0", release=True)
    assert release.check(CHANGELOG, "0.1.0", release=True) == []


def test_check_needs_unreleased():
    assert release.check("# Changelog\n", "0.1.0")


def test_check_refuses_edits_to_released_sections():
    edited = CHANGELOG.replace("- First release.", "- First release, edited.")
    assert release.check(edited, "0.2.0", released_from=CHANGELOG)


@pytest.mark.parametrize(
    ("version", "last", "tagged", "expected"),
    [
        ("0.1.0", "", False, "0.1.0-dev.1"),
        ("0.1.0", "0.1.0-dev.4", False, "0.1.0-dev.5"),
        ("0.1.0", "v0.1.0-dev.4", False, "0.1.0-dev.5"),
        ("0.2.0", "0.1.0-dev.9", False, "0.2.0-dev.1"),
        ("0.1.0", "0.1.0-dev.9", True, "0.1.1-dev.1"),
    ],
)
def test_dev_version_counts_test_builds(version, last, tagged, expected):
    assert release.dev_version(version, last, tagged) == expected


def test_dev_version_rejects_a_suffixed_version():
    with pytest.raises(ValueError):
        release.dev_version("0.1.0-beta", "", False)


def test_notes_for_a_test_build_link_dev():
    changelog = CHANGELOG.replace(
        "- Something new.",
        "- See https://github.com/raetha/esphome-freematics/blob/main/docs/hardware.md",
    )
    text = release.notes(changelog, "Unreleased", test_build=3)
    assert text.startswith("**Test build 3**")
    assert "/blob/dev/docs/hardware.md" in text
    assert "ref: dev" in text


def test_notes_for_a_release_pin_the_version():
    text = release.notes(CHANGELOG, "0.1.0")
    assert "- First release." in text
    assert "ref: 0.1.0" in text


def test_package_version_reads_the_project_version():
    text = (ROOT / "packages" / "freematics-oneplus-b.yaml").read_text(encoding="utf-8")
    assert release._PLAIN_VERSION.match(release.package_version(text))
