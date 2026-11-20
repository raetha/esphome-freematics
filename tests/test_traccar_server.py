"""Optional: the encoder's packets against a real Traccar server.

Skipped unless TRACCAR_UDP (Teltonika) or TRACCAR_FREEMATICS_UDP is set to
host:port of a Traccar server with a device whose identifier is "test"
(docs/development.md shows how to run one locally). Checks that Traccar
acknowledges a time-only record, a single fix and a full 40-record batch, and
answers a Freematics login.
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
FREEMATICS = os.environ.get("TRACCAR_FREEMATICS_UDP")  # host:port, e.g. 127.0.0.1:5170

pytestmark = pytest.mark.skipif(CXX is None, reason="no C++ compiler")
needs_server = pytest.mark.skipif(not SERVER, reason="TRACCAR_UDP not set")


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


def send(data: bytes, server: str | None = None) -> bytes:
    host, port = (server or SERVER).rsplit(":", 1)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5)
    sock.sendto(data, (host, int(port)))
    return sock.recvfrom(64)[0]


@needs_server
def test_time_only_record(encode):
    t = int(time.time())
    assert send(encode(1, 11, [(t, 0, 0, 0, 0, 0, 0, 12600, 0)])) == bytes.fromhex(
        "00050000010b01"
    )


@needs_server
def test_fix_and_full_batch(encode):
    t = int(time.time()) - 400
    one = [(t, 1, 43.81835, -72.2788, 52.5, 270, 12, 0, -1)]
    assert send(encode(2, 12, one)) == bytes.fromhex("00050000010c01")
    batch = [
        (t + 10 * i, 1, 43.8 + i * 0.001, -72.27, 40, 90, 9, 0, -1) for i in range(40)
    ]
    assert send(encode(3, 13, batch)) == bytes.fromhex("00050000010d28")


@pytest.mark.skipif(not FREEMATICS, reason="TRACCAR_FREEMATICS_UDP not set")
def test_freematics_login_is_answered(tmp_path):
    exe = tmp_path / "codec_cli"
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
    login = subprocess.run(
        [str(exe)], input="login test 1234\n", capture_output=True, text=True
    ).stdout.strip()
    assert send(login.encode(), FREEMATICS).startswith(b"1#EV=1,RX=1,TS=1234*")
