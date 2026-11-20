"""components/file_queue: the ring file survives reopening, wraps around, and
falls back to the previous header when the newest one was torn.

Builds tests/cpp/ring_file_cli.cpp with the host's C++ compiler.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
COMPONENT = ROOT / "components" / "file_queue"
CXX = shutil.which("g++") or shutil.which("c++")

pytestmark = pytest.mark.skipif(CXX is None, reason="no C++ compiler")


@pytest.fixture(scope="module")
def exe(tmp_path_factory):
    out = tmp_path_factory.mktemp("cli") / "ring_file_cli"
    subprocess.run(
        [
            CXX,
            "-std=c++17",
            "-Wall",
            "-Werror",
            "-I",
            str(COMPONENT),
            str(ROOT / "tests" / "cpp" / "ring_file_cli.cpp"),
            "-o",
            str(out),
        ],
        check=True,
    )
    return out


@pytest.fixture
def run(exe, tmp_path):
    path = tmp_path / "queue.bin"

    def run(*ops, size=4, fmt=1, capacity=5):
        result = subprocess.run(
            [str(exe), str(path), str(size), str(fmt), str(capacity), *ops],
            check=True,
            capture_output=True,
            text=True,
        )
        return result.stdout.splitlines()

    run.path = path
    return run


def test_new_file_is_empty(run):
    assert run("size") == ["created", "size 0"]
    assert run("size") == ["loaded", "size 0"]


def test_order_and_reopen(run):
    assert run("push:1", "push:2", "push:3")[0] == "created"
    assert run("size", "read:0:3", "read:1:2") == [
        "loaded",
        "size 3",
        "read 1 2 3",
        "read 2 3",
    ]
    assert run("pop:2", "read:0:1")[1:] == ["pop ok", "read 3"]
    assert run("size", "read:0:1") == ["loaded", "size 1", "read 3"]


def test_full_refuses_and_wraps(run):
    pushes = [f"push:{v}" for v in range(1, 7)]
    assert run(*pushes)[1:] == ["push ok"] * 5 + ["push no"]
    assert run("pop:3", "push:6", "push:7", "push:8", "push:9", "read:0:5")[1:] == [
        "pop ok",
        "push ok",
        "push ok",
        "push ok",
        "push no",
        "read 4 5 6 7 8",
    ]
    assert run("read:0:5", "read:3:2", "read:4:2") == [
        "loaded",
        "read 4 5 6 7 8",
        "read 7 8",
        "read no",
    ]


def test_pop_more_than_held(run):
    assert run("push:1", "pop:9", "size")[1:] == ["push ok", "pop ok", "size 0"]


def test_other_layout_replaces_the_file(run):
    run("push:1", "push:2")
    assert run("size", size=8) == ["replaced", "size 0"]
    run("push:1", size=8)
    assert run("size", size=8, fmt=2) == ["replaced", "size 0"]
    run("push:1", size=8, fmt=2)
    assert run("size", size=8, fmt=2, capacity=6) == ["replaced", "size 0"]


def test_torn_header_falls_back_to_the_previous_one(run):
    run("push:1", "push:2", "push:3")
    data = bytearray(run.path.read_bytes())
    # Three pushes after the first header: sequence 4 is the newest, in slot 0.
    assert int.from_bytes(data[4:8], "little") == 4
    data[20] ^= 0xFF  # damage its count
    run.path.write_bytes(bytes(data))
    assert run("size", "read:0:2") == ["loaded", "size 2", "read 1 2"]


def test_garbage_file_is_replaced(run):
    run.path.write_bytes(b"not a queue" * 10)
    assert run("size") == ["replaced", "size 0"]


def test_unwritable_path_fails(exe, tmp_path):
    result = subprocess.run(
        [str(exe), str(tmp_path / "missing" / "q.bin"), "4", "1", "5", "push:1"],
        check=True,
        capture_output=True,
        text=True,
    )
    assert result.stdout.splitlines() == ["failed", "push no"]
