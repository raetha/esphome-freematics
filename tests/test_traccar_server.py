"""Optional: the encoder's packets against a real Traccar server.

Skipped unless TRACCAR_UDP is set to host:port of a Traccar server with a
device whose identifier is "test" (docs/development.md shows how to run one
locally). Checks that Traccar acknowledges a time-only record, a single fix and
a full 40-record batch, with the acknowledgement the uplink expects.
"""

from __future__ import annotations

import os
import shutil
import socket
import subprocess
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
CXX = shutil.which("g++") or shutil.which("c++")
SERVER = os.environ.get("TRACCAR_UDP")

pytestmark = pytest.mark.skipif(
    not SERVER or CXX is None, reason="TRACCAR_UDP not set (or no C++ compiler)"
)


@pytest.fixture(scope="module")
def encode(tmp_path_factory):
    exe = tmp_path_factory.mktemp("codec") / "codec_cli"
    subprocess.run(
        [
            CXX,
            "-std=c++17",
            "-I",
            str(ROOT / "components" / "traccar_uplink"),
            str(ROOT / "tests" / "cpp" / "codec_cli.cpp"),
            "-o",
            str(exe),
        ],
        check=True,
    )

    def run(packet_id, location_id, records):
        lines = [f"packet {packet_id} {location_id} test {len(records)}"]
        lines += [" ".join(str(v) for v in r) for r in records]
        out = subprocess.run(
            [str(exe)], input="\n".join(lines) + "\n", capture_output=True, text=True
        )
        return bytes.fromhex(out.stdout.strip())

    return run


def send(data: bytes) -> bytes:
    host, port = SERVER.rsplit(":", 1)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5)
    sock.sendto(data, (host, int(port)))
    return sock.recvfrom(64)[0]


def test_time_only_record(encode):
    t = int(time.time())
    assert send(encode(1, 11, [(t, 0, 0, 0, 0, 0, 0, 12600, 0)])) == bytes.fromhex(
        "00050000010b01"
    )


def test_fix_and_full_batch(encode):
    t = int(time.time()) - 400
    one = [(t, 1, 43.81835, -72.2788, 52.5, 270, 12, 0, -1)]
    assert send(encode(2, 12, one)) == bytes.fromhex("00050000010c01")
    batch = [
        (t + 10 * i, 1, 43.8 + i * 0.001, -72.27, 40, 90, 9, 0, -1) for i in range(40)
    ]
    assert send(encode(3, 13, batch)) == bytes.fromhex("00050000010d28")
