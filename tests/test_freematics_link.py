"""components/freematics_link: parsing the co-processor's replies (link_parse.h).

Builds tests/cpp/link_cli.cpp with the host's C++ compiler. Reply formats follow
the FreematicsPlus library; real replies captured on the bench belong here too.
"""

from __future__ import annotations

import shutil
import subprocess
from datetime import UTC, datetime
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
CXX = shutil.which("g++") or shutil.which("c++")

pytestmark = pytest.mark.skipif(CXX is None, reason="no C++ compiler")


@pytest.fixture(scope="module")
def cli(tmp_path_factory):
    exe = tmp_path_factory.mktemp("cli") / "link_cli"
    subprocess.run(
        [
            CXX,
            "-std=c++17",
            "-Wall",
            "-Werror",
            "-I",
            str(ROOT / "components" / "freematics_link"),
            str(ROOT / "tests" / "cpp" / "link_cli.cpp"),
            "-o",
            str(exe),
        ],
        check=True,
    )

    def run(*lines: str) -> list[str]:
        text = "\n".join(lines) + "\n"
        out = subprocess.run(
            [str(exe)], input=text, capture_output=True, text=True, check=True
        )
        return out.stdout.splitlines()

    return run


def test_gnifo_reply(cli):
    reply = "ATGPS\\r$GNIFO,191126,18300500,40712776,-74005974,1234,2830,27100,9,12\\r"
    fields = cli(f"gnifo {reply}")[0].split()
    assert fields[:2] == ["191126", "18300500"]
    assert int(fields[2]) == int(
        datetime(2026, 11, 19, 18, 30, 5, tzinfo=UTC).timestamp()
    )
    assert float(fields[3]) == pytest.approx(40.712776, abs=1e-5)
    assert float(fields[4]) == pytest.approx(-74.005974, abs=1e-5)
    assert float(fields[5]) == pytest.approx(12.34)
    assert float(fields[6]) == pytest.approx(28.30 * 1.852, abs=0.01)  # knots to km/h
    assert float(fields[7]) == pytest.approx(271)
    assert fields[8:] == ["9", "12"]


@pytest.mark.parametrize(
    "reply",
    [
        "$GNIFO,0,0,0,0,0,0,0,0,0",  # no fix yet
        "NO DATA",
        "$GNIFO,191126",  # cut short
        "$GNIFO,191126,18300500,95000000,10,0,0,0,0,0",  # latitude out of range
    ],
)
def test_gnifo_without_a_position(cli, reply):
    assert cli(f"gnifo {reply}") == ["none"]


def test_gnifo_short_reply_still_gives_position(cli):
    fields = cli("gnifo $GNIFO,191126,18300500,40712776,-74005974")[0].split()
    assert float(fields[3]) == pytest.approx(40.712776, abs=1e-5)
    assert fields[5] == "nan"


def test_battery_voltage(cli):
    assert cli("number ATRV\\r12.6V", "number 13.84", "number OK", "number ") == [
        "12.60",
        "13.84",
        "nan",
        "nan",
    ]


def test_version(cli):
    assert cli(
        "version Freematics OBD v1.4", "version Freematics OBD ?1.6", "version ?"
    ) == [
        "14",
        "16",
        "0",
    ]


def test_reply_end(cli):
    assert cli("end OK\\r>", "end OK\\r", "end 12.6V\\r>junk") == ["2", "-1", "5"]
