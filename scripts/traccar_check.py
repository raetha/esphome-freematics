#!/usr/bin/env python3
"""Check that a Traccar server answers the dongle's packets, from any computer.

Sends one Teltonika GH3000 packet over UDP, as the traccar_uplink component
does, holding a single time-only record (no position, so nothing shows on the
map), and prints Traccar's acknowledgement:

    python3 scripts/traccar_check.py traccar.example.com test
    python3 scripts/traccar_check.py 203.0.113.7 test --port 5027
    python3 scripts/traccar_check.py traccar.example.com test --protocol freematics

With --protocol freematics it sends a Freematics login event to UDP 5170
instead, which Traccar answers the same way.

The device ID must exist in Traccar: Traccar ignores unknown devices and sends
no acknowledgement. Run it from outside the home network (a phone's hotspot)
to check the port forwarding the dongle will use on LTE.

Needs only Python 3.
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys
import time

GH3000_EPOCH = 1167609600  # 2007-01-01


def packet(device_id: str, packet_id: int = 1, location_id: int = 1) -> bytes:
    """A GH3000 (codec 7) UDP packet with one time-only record."""
    record = struct.pack(">I", int(time.time()) - GH3000_EPOCH)  # time, no flag bits
    record += bytes([0x00])  # global mask: no location, no IO
    ident = device_id.encode("ascii")
    body = struct.pack(">H", packet_id) + bytes([0x01])  # packet id, type: ack wanted
    body += bytes([location_id]) + struct.pack(">H", len(ident)) + ident
    body += bytes([0x07, 1]) + record + bytes([1])  # codec, count, record, count
    return struct.pack(">H", len(body)) + body


def freematics_login(device_id: str) -> bytes:
    """A Freematics login event; Traccar answers it with 1#EV=1,RX=1,..."""
    text = f"{device_id}#EV=1,TS={int(time.time()) % 100000},ID={device_id}"
    return f"{text}*{sum(text.encode()) % 256:02X}".encode()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("host")
    parser.add_argument("device_id")
    parser.add_argument(
        "--protocol", choices=("teltonika", "freematics"), default="teltonika"
    )
    parser.add_argument(
        "--port", type=int, help="default: 5027, or 5170 for freematics"
    )
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    try:
        address = socket.gethostbyname(args.host)
    except OSError as e:
        print(f"Can't resolve {args.host}: {e}")
        return 2
    print(f"{args.host} is {address}")
    freematics = args.protocol == "freematics"
    port = args.port or (5170 if freematics else 5027)
    data = freematics_login(args.device_id) if freematics else packet(args.device_id)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(args.timeout)
    for attempt in (1, 2, 3):
        sock.sendto(data, (address, port))
        print(f"Sent {len(data)} bytes to {address}:{port} (try {attempt})")
        try:
            reply, _ = sock.recvfrom(64)
        except TimeoutError:
            continue
        print(f"Answer: {reply.hex()}")
        if freematics:
            if reply.startswith(b"1#EV=1,RX=1"):
                print(f"Traccar answered the login: it's reachable. ({reply.decode()})")
                return 0
            print("An answer, but not Traccar's answer to a login.")
            return 1
        if len(reply) >= 7 and reply[:5] == bytes.fromhex("0005000001"):
            print(f"Traccar acknowledged {reply[6]} record(s): it's reachable.")
            return 0
        print("An answer, but not a Teltonika acknowledgement.")
        return 1
    print(
        "No answer. Check: the device ID exists in Traccar; in Traccar's "
        f"Settings > Protocols, {args.protocol} is on port {port} with UDP (or "
        f"All) transport; UDP {port} reaches Traccar (Docker publishes it only "
        f"as {port}:{port}/udp); the name isn't proxied (Cloudflare's proxy "
        "carries only web traffic)."
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
