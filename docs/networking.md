# Networking: Wi-Fi, LTE, WireGuard and data use

Cellular data is the scarcest resource in this project: every design choice is weighed by the bytes it puts on LTE. Which configuration costs what: [configurations.md](configurations.md). This page covers Wi-Fi and LTE for all of them, and the Home Assistant configuration's tunnel and data use.

## Wi-Fi first, LTE otherwise

`packages/wifi-first.yaml` does what the telelogger does: ESPHome keeps looking for the configured Wi-Fi networks, and when one connects, LTE is switched off (`modem.disable`); when Wi-Fi drops for `lte_back_after` (30 s), LTE comes back (`modem.enable`). So nothing goes over LTE while a known network is in range, and the modem isn't attached there either.

While the car is moving (a fresh GNSS fix at `wifi_pause_speed`, 10 km/h, or more) and Wi-Fi isn't connected, the package turns Wi-Fi off (`wifi.disable`), since away from home ESPHome otherwise scans every few seconds for the whole drive (seen on the first drive), which is radio noise and power beside the GNSS antenna. Wi-Fi comes back on (`wifi.enable`) once the car has been below that speed for `wifi_resume_after` (30 s) or GNSS has had no fix for that long, as in a garage. A connection made before driving off (a phone hotspot) is never dropped. `wifi_pause_speed: "1000"` turns the pause off.

Parked, a search that finds no known network within `wifi_search_time` (2 minutes) stops until the car has moved again (at `wifi_pause_speed`), since a stopped car won't come into range of one: ESPHome would otherwise scan, try each missing network as a hidden one and reset the radio about every 8 seconds for as long as the dongle is awake (a day parked at work logged about 400 failed attempts an hour). Each start or wake of the dongle searches again. A network that was connected where the car is and dropped is different (an access point restarting or updating, a power cut): the search goes on in windows of `wifi_search_time` spaced 1, 2, 4 and 8 minutes, then every 15, until it's back or the car moves; an access point that disconnects the dongle and takes it back within seconds never reaches the first pause. `wifi_search_time: "0"` searches on.

`wifi:` and `modem:` validate together in ESPHome 2026.11. ESPHome's `network: priority` can't include the modem yet, so in the moment both are up the default route is ESP-IDF's: the connected interface with the highest route priority (Wi-Fi station 100, PPP 20), which also favours Wi-Fi. Phase 1 confirms the handover on hardware: the Traccar uplink just sends its next packet over whichever link is up; the API connection reconnects.

## Home Assistant configuration: native API, dialled from the device

ESPHome 2026.11's native API can dial Home Assistant itself (`api: outgoing_connection:`, Home Assistant 2026.10 or later, encryption required). So HA never has to reach the dongle behind the carrier's NAT: add the device to HA once while it's on home Wi-Fi, and from then on it remembers HA's address and connects to it, from Wi-Fi or LTE. No MQTT broker is involved. MQTT was considered and dropped for now (below).

## WireGuard

The dongle opens a WireGuard tunnel home and reaches Home Assistant through it, so nothing at home is exposed to the internet.

Device side (`examples/car-home-assistant.yaml`):

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
| An SNTP sync | about 300: a DNS lookup and one NTP exchange, each about 150 with their IP and UDP headers. ESPHome's default of every 15 minutes would cost 15 to 30 KB a day over LTE, as much as the whole budget, so the board package syncs every 12 hours and takes the time from GPS, which is free |

So:

- **Driving:** position updates (only when the car has moved about 10 m) plus a few diagnostics, batched, come to roughly 250 bytes on the wire (the packet and HA's acknowledgement) every 5 seconds at most: about 0.2 MB per hour of driving.
- **Parked, powered and connected over LTE:** HA's pings dominate, about 1.3 MB a day (about 40 MB a month). Sleep while parked (`packages/standby.yaml`) cuts it in the Traccar configuration; it doesn't support the Home Assistant configuration yet. On home Wi-Fi it costs no cellular data.
- **Every entity costs data.** The package keeps only what testing needs, sends each value only on a real change (with a 15 minute ceiling), and has no altitude or heading entities. Buttons and switches cost nothing until pressed, apart from the entity list on reconnect.

### Native API or MQTT

| | Native API (chosen) | MQTT |
| --- | --- | --- |
| Per update | Binary, batched into one packet per `batch_delay` | A topic string per value (about 40 bytes), one packet each |
| Connecting | Entity list, a few KB | Discovery config for every entity, tens of KB, on every reconnect |
| Idle | HA pings every 20 s (fixed in HA) | Keepalive is configurable, 60 s or longer |
| Needs | HA 2026.10+, the device dials out | A broker reachable through the tunnel |

While driving the native API is smaller; parked and idle, MQTT's longer keepalive would win, but the answer to parked is to switch the radio off. Neither is the end state.

## The low-data option

Neither the native API nor MQTT carries timestamps or batches fixes, so the low-data configuration is the Traccar one: Teltonika GH3000 records over UDP, about 19 bytes per fix with altitude, batched and acknowledged, around 12 KB per hour of driving ([configurations.md](configurations.md)).
