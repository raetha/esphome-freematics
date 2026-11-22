"""components/icm426xx: what the accelerometer's samples mean (motion.h).

Builds tests/cpp/motion_cli.cpp with the host's C++ compiler.
"""

from __future__ import annotations

import math
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
CXX = shutil.which("g++") or shutil.which("c++")

pytestmark = pytest.mark.skipif(CXX is None, reason="no C++ compiler")


@pytest.fixture(scope="module")
def analyze(tmp_path_factory):
    exe = tmp_path_factory.mktemp("motion") / "motion_cli"
    subprocess.run(
        [
            CXX,
            "-std=c++20",
            "-Wall",
            "-Werror",
            "-I",
            str(ROOT / "components" / "icm426xx"),
            str(ROOT / "tests" / "cpp" / "motion_cli.cpp"),
            "-o",
            str(exe),
        ],
        check=True,
    )

    def run(samples, rate=100, motion_g=0.1, impact_g=4.0) -> list[str]:
        text = f"{rate} {motion_g} {impact_g}\n"
        text += "\n".join(f"{x} {y} {z}" for x, y, z in samples) + "\n"
        out = subprocess.run(
            [str(exe)], input=text, capture_output=True, text=True, check=True
        )
        return out.stdout.splitlines()

    return run


# A dongle mounted at an angle: gravity split over two axes
STILL = (0.0, 0.5, math.sqrt(0.75))


def test_still_is_quiet(analyze):
    assert set(analyze([STILL] * 300)) == {"-"}


def test_a_nudge_is_movement(analyze):
    x, y, z = STILL
    out = analyze([STILL] * 50 + [(x + 0.2, y, z)] + [STILL] * 50)
    assert out[50] == "moved" and out[51] == "moved"
    assert "impact" not in " ".join(out)


def test_an_impact_reports_its_peak_once(analyze):
    x, y, z = STILL
    # 150 ms crash pulse peaking at 9 g along x, then ringing
    pulse = [(x + g, y, z) for g in (3, 6, 9, 7, 5, 2, 1, 0.5)]
    ringing = [(x + (0.5 if i % 2 else -0.5), y, z) for i in range(100)]
    out = analyze([STILL] * 200 + pulse + ringing + [STILL] * 600)
    impacts = [line for line in out if "impact" in line]
    assert len(impacts) == 1
    assert impacts[0].endswith("impact 9.0")
    # It starts at the first sample over 4 g (the second of the pulse) and is
    # reported half a second (50 samples) later, with the peak in between.
    assert out.index(impacts[0]) == 201 + 50


def test_gravity_follows_a_new_orientation(analyze):
    # The dongle is turned (plugged in at another angle): no impact
    turned = (0.0, 0.0, 1.0)
    steps = [
        tuple(a + (b - a) * i / 100 for a, b in zip(STILL, turned, strict=True))
        for i in range(100)
    ]
    out = analyze([STILL] * 100 + steps + [turned] * 300)
    assert "impact" not in " ".join(out)


def test_below_the_threshold_isnt_an_impact(analyze):
    x, y, z = STILL
    pothole = [(x, y, z + g) for g in (1.0, 2.5, 3.0, 1.5)]
    out = analyze([STILL] * 200 + pothole + [STILL] * 100)
    assert "impact" not in " ".join(out)
    # With the threshold off, a big one isn't either
    crash = [(x + 9, y, z)] * 5
    out = analyze([STILL] * 200 + crash + [STILL] * 100, impact_g=0)
    assert "impact" not in " ".join(out)
