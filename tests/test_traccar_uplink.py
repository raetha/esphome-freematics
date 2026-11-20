"""components/traccar_uplink: the Teltonika encoder against a Python mirror of
Traccar's decoder, the GNSS time conversion, and the tracking filter.

Builds tests/cpp/codec_cli.cpp with the host's C++ compiler.
"""

from __future__ import annotations

import shutil
import struct
import subprocess
from datetime import UTC, datetime
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
COMPONENT = ROOT / "components" / "traccar_uplink"
CXX = shutil.which("g++") or shutil.which("c++")

pytestmark = pytest.mark.skipif(CXX is None, reason="no C++ compiler")


@pytest.fixture(scope="module")
def cli(tmp_path_factory):
    exe = tmp_path_factory.mktemp("cli") / "codec_cli"
    subprocess.run(
        [
            CXX,
            "-std=c++17",
            "-Wall",
            "-Werror",
            "-I",
            str(COMPONENT),
            str(ROOT / "tests" / "cpp" / "codec_cli.cpp"),
            "-o",
            str(exe),
        ],
        check=True,
    )

    def run(text: str) -> list[str]:
        out = subprocess.run(
            [str(exe)], input=text, capture_output=True, text=True, check=True
        )
        return out.stdout.split()

    return run


def decode(packet: bytes) -> dict:
    """What Traccar's TeltonikaProtocolDecoder reads from a GH3000 UDP packet."""
    pos = 0

    def take(n):
        nonlocal pos
        chunk = packet[pos : pos + n]
        assert len(chunk) == n, "packet ends early"
        pos += n
        return chunk

    def u8():
        return take(1)[0]

    def u16():
        return struct.unpack(">H", take(2))[0]

    length, packet_id, packet_type, location_packet_id = u16(), u16(), u8(), u8()
    assert length == len(packet) - 2
    imei = take(u16()).decode("ascii")
    codec, count = u8(), u8()
    assert codec == 0x07
    records = []
    for _ in range(count):
        t = (struct.unpack(">I", take(4))[0] & 0x3FFFFFFF) + 1167609600
        rec = {"time": t}
        mask = u8()
        if mask & 0x01:
            loc = u8()
            if loc & 0x01:
                rec["lat"], rec["lon"] = struct.unpack(">ff", take(8))
            if loc & 0x02:
                rec["alt"] = u16()
            if loc & 0x04:
                rec["course"] = u8() * 360.0 / 256
            if loc & 0x08:
                rec["speed"] = u8()
            if loc & 0x10:
                rec["sats"] = u8()
        for bit, size in ((1, 1), (2, 2), (3, 4)):
            if mask & (1 << bit):
                for _ in range(u8()):
                    io = u8()
                    rec[f"io{io}"] = int.from_bytes(take(size), "big")
        records.append(rec)
    trailing = u8()
    assert pos == len(packet), "bytes left over"
    return {
        "packet_id": packet_id,
        "type": packet_type,
        "location_packet_id": location_packet_id,
        "imei": imei,
        "records": records,
        "trailing_count": trailing,
    }


def test_packet_decodes_as_traccar_reads_it(cli):
    t = int(datetime(2026, 11, 19, 18, 30, 5, tzinfo=UTC).timestamp())
    lines = [
        "packet 7 3 BL1YRTCF 3",
        f"{t} 1 40.712776 -74.005974 52.4 271 9 12650 1",
        f"{t + 10} 1 40.713500 -74.007100 0 90 8 0 -1",
        f"{t + 900} 0 0 0 0 0 0 12400 0",
    ]
    packet = bytes.fromhex(cli("\n".join(lines) + "\n")[0])
    got = decode(packet)
    assert got["packet_id"] == 7 and got["type"] == 1 and got["location_packet_id"] == 3
    assert got["imei"] == "BL1YRTCF" and got["trailing_count"] == 3
    a, b, c = got["records"]
    assert (
        a["time"] == t
        and abs(a["lat"] - 40.712776) < 1e-5
        and abs(a["lon"] + 74.005974) < 1e-5
    )
    assert a["speed"] == 52 and a["sats"] == 9 and abs(a["course"] - 271) < 1.5
    assert a["io240"] == 1 and a["io67"] == 12650
    assert b["time"] == t + 10 and "io240" not in b and "io67" not in b
    assert c == {"time": t + 900, "io240": 0, "io67": 12400}


def test_record_sizes(cli):
    one = "packet 0 0 ABCDEFGH 1\n1767225600 1 1 1 1 1 1 0 -1\n"
    # 8 bytes of header, an 8-byte ID, codec and count, one 17-byte record, a count
    assert len(bytes.fromhex(cli(one)[0])) == 8 + 8 + 2 + 17 + 1


def test_ack(cli):
    assert cli("ack 00050000010303 3\n") == ["3"]
    assert cli("ack 00050000010403 3\n") == ["-1"]
    assert cli("ack 000500 3\n") == ["-1"]


@pytest.mark.parametrize(
    ("date", "time", "expected"),
    [
        (191126, 18300500, datetime(2026, 11, 19, 18, 30, 5, tzinfo=UTC)),
        (290228, 23595999, datetime(2028, 2, 29, 23, 59, 59, tzinfo=UTC)),
        (10130, 0, datetime(2030, 1, 1, tzinfo=UTC)),
    ],
)
def test_gnss_time(cli, date, time, expected):
    assert int(cli(f"unix {date} {time}\n")[0]) == int(expected.timestamp())


def test_gnss_time_rejects_nonsense(cli):
    assert cli("unix 0 0\n") == ["0"]
    assert cli("unix 321326 0\n") == ["0"]


def track(
    cli, events, moving=6, stop=180, interval=10, turn=30, min_turn=3, heartbeat=900
):
    lines = [
        f"track {moving} {stop} {interval} {turn} {min_turn} {heartbeat} {len(events)}"
    ]
    lines += [" ".join(str(x) for x in e) for e in events]
    return cli("\n".join(lines) + "\n")


def test_trip(cli):
    events = [
        ("fix", 0, 0, 0),  # parked: first heartbeat
        ("fix", 5, 0, 0),
        ("fix", 10, 30, 90),  # start
        ("fix", 15, 40, 90),
        ("fix", 20, 40, 92),  # interval
        ("fix", 24, 40, 140),  # turn
        ("fix", 25, 40, 180),  # turn, but too soon
        ("fix", 30, 0, 180),  # slow, not yet stopped
        ("fix", 34, 0, 180),  # interval while slowing
        ("fix", 205, 0, 180),  # 180 s since the last fast fix (25): stop
        ("fix", 210, 0, 180),
    ]
    assert track(cli, events) == [
        "HEARTBEAT",
        "NONE",
        "START",
        "NONE",
        "RECORD",
        "RECORD",
        "NONE",
        "NONE",
        "RECORD",
        "STOP",
        "NONE",
    ]


def test_heartbeats_while_parked(cli):
    events = [
        ("fix", 0, 0, 0),
        ("fix", 899, 0, 0),
        ("fix", 900, 0, 0),
        ("tick", 950),
        ("tick", 1800),
    ]
    assert track(cli, events) == ["HEARTBEAT", "NONE", "HEARTBEAT", "NONE", "HEARTBEAT"]


def test_no_heartbeats_when_off(cli):
    assert track(cli, [("fix", 0, 0, 0), ("tick", 5000)], heartbeat=0) == [
        "NONE",
        "NONE",
    ]


def queue(cli, capacity, ops):
    lines = [f"queue {capacity} {len(ops)}"] + [
        " ".join(str(x) for x in op) for op in ops
    ]
    return cli("\n".join(lines) + "\n")


def test_queue_fills_wraps_and_keeps_order(cli):
    ops = [
        ("push", 1),
        ("push", 2),
        ("push", 3),
        ("push", 4),
        ("pop", 2),
        ("push", 5),
        ("push", 6),
    ]
    # cli() splits on whitespace, so "full" and "rejected" come out as separate items
    assert queue(cli, 3, ops) == [
        "1",
        "1,2",
        "1,2,3",
        "full",
        "rejected",
        "1,2,3",
        "full",
        "3",
        "3,5",
        "3,5,6",
        "full",
    ]


def test_queue_erase_keeps_the_rest_in_order(cli):
    ops = [
        ("push", 1),
        ("push", 2),
        ("pop", 1),
        ("push", 3),
        ("push", 4),
        ("erase", 1),
        ("erase", 9),
        ("pop", 9),
    ]
    assert queue(cli, 3, ops) == [
        "1",
        "1,2",
        "2",
        "2,3",
        "2,3,4",
        "full",
        "2,4",
        "2,4",
        "-",
    ]


# The backlog: records in memory, and with a store (a ring file here) every
# waiting record on file with memory holding the oldest of them.


@pytest.fixture(scope="module")
def backlog_exe(tmp_path_factory):
    exe = tmp_path_factory.mktemp("backlog") / "backlog_cli"
    subprocess.run(
        [
            CXX,
            "-std=c++17",
            "-Wall",
            "-Werror",
            "-I",
            str(COMPONENT),
            "-I",
            str(ROOT / "components" / "file_queue"),
            str(ROOT / "tests" / "cpp" / "backlog_cli.cpp"),
            "-o",
            str(exe),
        ],
        check=True,
    )
    return exe


@pytest.fixture
def backlog(backlog_exe, tmp_path):
    def run(*ops, memory=3, store=True, capacity=6):
        path = str(tmp_path / "q.bin") if store else "-"
        result = subprocess.run(
            [str(backlog_exe), str(memory), path, str(capacity), *ops],
            check=True,
            capture_output=True,
            text=True,
        )
        return result.stdout.splitlines()

    return run


def test_backlog_memory_only_drops_oldest_not_in_flight(backlog):
    adds = [f"add:{t}:0" for t in (1, 2, 3)]
    assert backlog(*adds, "add:4:1", "show", store=False) == [
        "added",
        "added",
        "added",
        "dropped oldest",
        "size 3 memory 1 3 4",
    ]
    assert backlog(*adds, "add:4:3", "show", store=False)[-2:] == [
        "dropped",
        "size 3 memory 1 2 3",
    ]


def test_backlog_spills_to_the_store_and_refills(backlog):
    adds = [f"add:{t}:0" for t in range(1, 6)]
    assert backlog(*adds, "show", "ack:2", "show", "ack:3", "show")[5:] == [
        "size 5 memory 1 2 3",
        "size 3 memory 3 4 5",
        "size 0 memory",
    ]


def test_backlog_survives_a_restart(backlog):
    adds = [f"add:{t}:0" for t in range(1, 6)]
    assert backlog(*adds, "ack:1", "restart", "show")[-1] == "size 4 memory 2 3 4"
    assert backlog("show", "ack:3", "show") == [
        "size 4 memory 2 3 4",
        "size 1 memory 5",
    ]


def test_backlog_full_store(backlog):
    adds = [f"add:{t}:0" for t in range(1, 7)]
    assert backlog(*adds, "add:7:0", "show")[-2:] == [
        "dropped oldest",
        "size 6 memory 2 3 4",
    ]
    assert backlog("add:8:2", "show")[-2:] == ["dropped", "size 6 memory 2 3 4"]


def test_backlog_carries_on_in_memory_when_the_store_fails(backlog):
    adds = [f"add:{t}:0" for t in range(1, 5)]
    assert backlog(*adds, "fail", "add:5:0", "show", "ack:1", "show")[-3:] == [
        "dropped oldest",
        "size 3 memory 2 3 5",
        "size 2 memory 3 5",
    ]


# Traccar's Freematics text protocol, read the way FreematicsProtocolDecoder does.


def decode_freematics(sentence: str) -> tuple[str, list[dict]]:
    start, end = sentence.index("#"), sentence.index("*")
    body = sentence[start + 1 : end]
    assert f"{int(sentence[end + 1 :], 16):02X}" == sentence[end + 1 :]
    assert sum(sentence[:end].encode()) % 256 == int(sentence[end + 1 :], 16)
    positions: list[dict] = []
    for pair in body.split(","):
        key, value = pair.split(":", 1)
        key = int(key, 16)
        if key == 0:
            positions.append({})
            continue
        positions[-1][key] = value
    return sentence[:start], positions


FIX = "1800000000 1 43.81835 -72.2788 52.5 270 12 12600 -1"
TIME_ONLY = "1800000010 0 0 0 0 0 0 0 0"


def test_freematics_positions(cli):
    (out,) = cli(f"freematics car1 1024 2\n{FIX}\n{TIME_ONLY}\n")
    device, positions = decode_freematics(out)
    assert device == "car1"
    first, second = positions
    # 1800000000 is 2027-01-15 08:00:00 UTC
    assert first[0x11] == "150127" and first[0x10] == "08000000"
    assert float(first[0xA]) == pytest.approx(43.81835, abs=1e-5)
    assert float(first[0xB]) == pytest.approx(-72.2788, abs=1e-5)
    assert first[0xD] == "53" and first[0xE] == "270" and first[0xF] == "12"
    assert first[0x24] == "1260"  # V x 100: Traccar shows 12.6
    # A time-only record carries no position: Traccar keeps the last one.
    assert second == {0x11: "150127", 0x10: "08001000"}


def test_freematics_doesnt_fit(cli):
    assert cli(f"freematics car1 60 2\n{FIX}\n{TIME_ONLY}\n") == ["-"]
    (one,) = cli(f"freematics car1 120 1\n{FIX}\n")
    assert one != "-" and len(one) < 120


def test_freematics_login(cli):
    assert cli("login car1 1234\n") == ["car1#EV=1,TS=1234,ID=car1*CA"]
    assert cli("loginreply 1#EV=1,RX=1,TS=1234*7B\n") == ["1"]
    assert cli("loginreply 1#EV=2,RX=1\n") == ["0"]
