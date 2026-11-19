# Networking: Wi-Fi, LTE and WireGuard

## Wi-Fi first, LTE otherwise

The example config has both `wifi:` and `modem:`. They validate together in ESPHome 2026.11. ESPHome's `network: priority` list can't include the modem yet (only Wi-Fi and Ethernet), so the default route is left to ESP-IDF, which picks the connected interface with the highest route priority: Wi-Fi station 100, PPP 20. So while a known Wi-Fi network is connected, traffic goes over Wi-Fi; otherwise over LTE. Phase 1 confirms this on hardware, including that MQTT reconnects cleanly when the route moves (a TCP connection opened over one interface breaks when the route changes, and MQTT reconnects on the other).

The modem stays attached while on Wi-Fi. If that turns out to cost data or power, an automation can disable it while home Wi-Fi is up (`modem.disable` / `modem.enable`).

## WireGuard

On LTE the dongle sits behind the carrier's NAT, so nothing at home can connect to it. Instead the dongle opens a WireGuard tunnel home and connects to the MQTT broker through it, so the broker is never exposed to the internet.

Device side (`examples/car.yaml`):

- `address`: the dongle's tunnel address, one per dongle (for example `10.66.0.11` and `10.66.0.12`).
- `peer_endpoint`: the home WireGuard server's public name or address; `peer_public_key` and an optional `peer_preshared_key`.
- `peer_allowed_ips`: only the broker's address (`192.168.1.10/32` in the example). Everything else, SNTP included, goes direct, so the tunnel doesn't depend on itself for the time it needs to start.
- `peer_persistent_keepalive: 0s`: the MQTT connection's 60 s keepalive already keeps the carrier's NAT mapping open.

Server side: a WireGuard peer per dongle, its `AllowedIPs` the dongle's `/32`, and a route or NAT from the tunnel to the broker. Any WireGuard server at home works (router, a container, or Home Assistant's WireGuard app).

At home on Wi-Fi the broker's address is still routed into the tunnel, so the endpoint must also work from inside the home network: either the router supports NAT loopback (hairpin), or the endpoint's name resolves to the server's LAN address at home (split DNS). Phase 1 checks this.

## Data use (approximate)

WireGuard adds 32 bytes per packet, plus the outer UDP/IP header (28 bytes), and a handshake (about 300 bytes both ways) every 2 minutes while traffic flows.

| What | Without WireGuard | With WireGuard |
| --- | --- | --- |
| MQTT keepalive every 60 s and its TCP acknowledgements | about 0.2 MB/day | about 0.65 MB/day (including handshakes) |
| The diagnostic sensors as configured (about 10 small publishes a minute, sent even when unchanged) | about 2 MB/day | about 3 MB/day |
| Driving: a location update every 5 s | about 0.15 MB/hour | about 0.2 MB/hour |

So WireGuard adds roughly a third on top of small messages and about 0.2 MB a day for handshakes: a few MB a month. What dominates is how often the device publishes at all: as configured for diagnostics, a dongle that is powered and on LTE all day uses about 100 MB a month, nearly all of it sensor updates. Once phase 0 is done, longer update intervals and `delta`/`throttle` filters bring that down a lot, and none of it counts while the car is on home Wi-Fi. The MQTT debug log topic at `DEBUG` level streams every co-processor exchange, so keep `mqtt_log_level` at `INFO` on LTE. These are estimates; phase 1 should read the real numbers from the carrier's usage page.
