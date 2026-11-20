"""components/file_logger: log lines lose their colour codes and secrets, and
the byte ring keeps order across wrap-around.

Builds tests/cpp/log_line_cli.cpp with the host's C++ compiler.
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
def cli(tmp_path_factory):
    exe = tmp_path_factory.mktemp("cli") / "log_line_cli"
    subprocess.run(
        [
            CXX,
            "-std=c++20",
            "-Wall",
            "-Werror",
            "-I",
            str(ROOT / "components" / "file_logger"),
            str(ROOT / "tests" / "cpp" / "log_line_cli.cpp"),
            "-o",
            str(exe),
        ],
        check=True,
    )

    def run(*args):
        out = subprocess.run([str(exe), *args], capture_output=True, text=True)
        assert out.returncode == 0
        return out.stdout.splitlines()

    return run


def test_colours_removed(cli):
    assert cli("clean", r"\e[0;36m[D][wifi:123]: up\e[0m") == ["17|[D][wifi:123]: up"]


def test_secrets_redacted(cli):
    assert cli("clean", r"key \e[8mhunter2\e[28m set") == ["18|key [redacted] set"]
    # A secret that never ends hides the rest of the line.
    assert cli("clean", r"key \e[8mhunter2") == ["14|key [redacted]"]


def test_long_line_truncated(cli):
    out = cli("clean", "x" * 100)[0]
    assert out == "63|" + "x" * 63


def test_ring_wraps_and_refuses_what_doesnt_fit(cli):
    ops = ["push:abcdef", "take:4", "push:ghij", "push:xyz", "take:9", "take:9"]
    assert cli("ring", "8", *ops) == [
        "ok [6] abcdef",
        "took abcd [2] ef",
        "ok [6] efgh",  # wrapped: the first run ends at the buffer's end
        "full [6] efgh",
        "took efgh [2] ij",
        "took ij [0] ",
    ]
