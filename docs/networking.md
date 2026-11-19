# Networking: Wi-Fi, LTE, WireGuard and data use

Cellular data is the scarcest resource in this project: every design choice is weighed by the bytes it puts on LTE.

## Wi-Fi first, LTE otherwise

The example config has both `wifi:` and `modem:`. They validate together in ESPHome 2026.11. ESPHome's `network: priority` list can't include the modem yet (only Wi-Fi and Ethernet), so the default route is left to ESP-IDF, which picks the connected interface with the highest route priority: Wi-Fi station 100, PPP 20. So while a known Wi-Fi network is connected, traffic goes over Wi-Fi; otherwise over LTE. Phase 1 confirms this on hardware, including that the API connection comes back when the route moves (a TCP connection opened over one interface breaks when the route changes).

The modem stays attached while on Wi-Fi. If that turns out to cost data or power, an automation can disable it while home Wi-Fi is up (`modem.disable` / `modem.enable`).

## Home Assistant connection: native API, dialled from the device

ESPHome 2026.11's native API can dial Home Assistant itself (`api: outgoing_connection:`, Home Assistant 2026.10 or later, encryption required). So HA never has to reach the dongle behind the carrier's NAT: add the device to HA once while it's on home Wi-Fi, and from then on it remembers HA's address and connects to it, from Wi-Fi or LTE. No MQTT broker is involved. MQTT was considered and dropped for now (below).

## WireGuard

The dongle opens a WireGuard tunnel home and reaches Home Assistant through it, so nothing at home is exposed to the internet.

Device side (`examples/car.yaml`):

- `address`: the dongle's tunnel address, one per dongle (for example `10.66.0.11` and `10.66.0.12`).
- `peer_endpoint`: the home WireGuard server's public name or address; `peer_public_key` and an optional `peer_preshared_key`.
- `peer_allowed_ips`: only Home Assistant's address (`192.168.1.10/32` in the example). Everything else, SNTP included, goes direct, so the tunnel doesn't depend on itself for the time it needs to start.
- `peer_persistent_keepalive: 25s`: WireGuard sends it only when nothing else went out in that time, so it costs nothing while the API is busy; it keeps the carrier's NAT mapping open so HA's replies get through.

Server side: a WireGuard peer per dongle, its `AllowedIPs` the dongle's `/32`, and a route or NAT from the tunnel to Home Assistant. Any WireGuard server at home works (router, a container, or Home Assistant's WireGuard app).

At home on Wi-Fi, HA's address is still routed into the tunnel, so the endpoint must also work from inside the home network: either the router supports NAT loopback (hairpin), or the endpoint's name resolves to the server's LAN address at home (split DNS). Phase 1 checks this.

## Data use

Approximate, per dongle; phase 1 should check them against Hologram's usage page.

| What | Bytes |
| --- | --- |
| WireGuard per packet | +60 (32 of WireGuard, 28 of outer UDP/IP) on top of the inner packet's own 40 or so of TCP/IP |
| WireGuard handshake | about 300, every 2 minutes while traffic flows |
| One native API state update | about 10 to 20 inside the encrypted frame; updates within `batch_delay` (2 s here) share one packet |
| Home Assistant's keepalive ping | about 250 per exchange with WireGuard, every 20 s, but only when nothing else was received in that time |
| (Re)connecting the API | a few KB: HA reads the entity list again |

So:

- **Driving:** position updates (only when the car has moved about 10 m) plus a few diagnostics, batched, come to roughly 250 bytes on the wire (the packet and HA's acknowledgement) every 5 seconds at most: about 0.2 MB per hour of driving.
- **Parked, powered and connected over LTE:** HA's pings dominate, about 1.3 MB a day (about 40 MB a month). That is the next thing to cut: phase 1 adds a parked mode that turns the modem off (or the device sleeps) once the car has been off for a few minutes. On home Wi-Fi it costs no cellular data.
- **Every entity costs data.** The package keeps only what testing needs, sends each value only on a real change (with a 15 minute ceiling), and has no altitude or heading entities. Buttons and switches cost nothing until pressed, apart from the entity list on reconnect.

### Native API or MQTT

| | Native API (chosen) | MQTT |
| --- | --- | --- |
| Per update | Binary, batched into one packet per `batch_delay` | A topic string per value (about 40 bytes), one packet each |
| Connecting | Entity list, a few KB | Discovery config for every entity, tens of KB, on every reconnect |
| Idle | HA pings every 20 s (fixed in HA) | Keepalive is configurable, 60 s or longer |
| Needs | HA 2026.10+, the device dials out | A broker reachable through the tunnel |

While driving the native API is smaller; parked and idle, MQTT's longer keepalive would win, but the answer to parked is to switch the radio off. Neither is the end state.

## The end state: a compact batched uplink

Neither the native API nor MQTT can carry timestamps, so offline caching and replay (phase 3) need their own uplink anyway. That's where data use can be cut to the bone: records packed as binary with delta-encoded positions, many fixes per packet, sent every minute or so (or when the queue fills), received by a small Home Assistant custom integration that turns them into the device tracker, live sensors and back-dated statistics. Traccar's binary protocols (Teltonika's, for example) are the alternative receiver: just as dense, but they put the data in Traccar rather than HA's statistics. Decided at phase 3 ([plan.md](plan.md)).
