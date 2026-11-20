"""A simulated day through the real tracking filter and encoder
(tests/cpp/sim_cli.cpp): keeps the data estimates in docs/configurations.md
honest. Bytes include IP and UDP headers and Traccar's answers.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
CXX = shutil.which("g++") or shutil.which("c++")

pytestmark = pytest.mark.skipif(CXX is None, reason="no C++ compiler")


@pytest.fixture(scope="module")
def sim(tmp_path_factory):
    exe = tmp_path_factory.mktemp("sim") / "sim_cli"
    subprocess.run(
        [
            CXX,
            "-std=c++20",
            "-Wall",
            "-Werror",
            "-I",
            str(ROOT / "components" / "traccar_uplink"),
            str(ROOT / "tests" / "cpp" / "sim_cli.cpp"),
            "-o",
            str(exe),
        ],
        check=True,
    )

    def run(*args):
        out = subprocess.run(
            [str(exe), *map(str, args)], capture_output=True, text=True, check=True
        )
        result = {}
        for line in out.stdout.splitlines():
            part = line.split()
            result[part[0]] = {"records": int(part[1]), "bytes": int(part[5])}
        return result

    return run


def test_an_hour_of_driving_is_about_12_kb(sim):
    hour = sim(60, 0)["drive"]
    assert 300 <= hour["records"] <= 450  # a record about every 10 s
    assert 9_000 <= hour["bytes"] <= 14_000


def test_a_workday_stays_near_the_stock_firmware(sim):
    day = sim(60, 8)  # an hour of driving, 8 hours parked on LTE
    assert day["parked"]["records"] == 32  # a heartbeat every 15 minutes
    assert day["drive"]["bytes"] + day["parked"]["bytes"] <= 16_000


def test_longer_send_interval_saves_data(sim):
    assert sim(60, 0, 120)["drive"]["bytes"] < sim(60, 0, 60)["drive"]["bytes"]
