# Configurations

The board package (`packages/freematics-oneplus-b.yaml`) is the same for every dongle; what differs is where the data goes. Each choice is an uplink package plus an example device config. Cellular data is the deciding cost, so each has an estimate (approximate; check against the carrier's usage page).

| | Traccar ([example](https://github.com/raetha/esphome-freematics/blob/main/examples/car-traccar.yaml)) | Home Assistant, native API ([example](https://github.com/raetha/esphome-freematics/blob/main/examples/car-home-assistant.yaml)) |
| --- | --- | --- |
| Package | `uplink-traccar.yaml` + the `traccar_uplink` component | `uplink-home-assistant.yaml` |
| Transport | UDP to Traccar's Teltonika port, no tunnel | Native API dialled from the device, through WireGuard |
| Wire format | Teltonika GH3000 (codec 7): 19 bytes per fix with altitude (17 without), batched, acknowledged | Protobuf state updates, batched every 2 s; HA pings every 20 s when idle |
| Driving, one hour (defaults) | about 12 KB | about 200 KB |
| Parked away from Wi-Fi | about 0.4 KB an hour (a heartbeat every 15 min); with `packages/standby.yaml` asleep after 13 minutes, a few hundred bytes per 6-hourly report (estimated, not yet measured) | about 55 KB an hour while connected over LTE (no parked mode yet) |
| A workday: one hour of driving, 8 hours parked at work, the rest at home on Wi-Fi | about 15 KB | about 650 KB |
| Offline (no signal) | Fixes queue in memory (300 by default, about 50 minutes of driving) and go out when Traccar answers; lost on reboot unless `sd-traccar.yaml` keeps them on the SD card (50,000) | Nothing kept: HA shows unavailable |
| Track timestamps | Each fix carries its GNSS time | HA's arrival time |
| In Home Assistant | Through Traccar (Traccar Server integration): device tracker and attributes | Native entities; a template device tracker over latitude/longitude ([home-assistant.md](home-assistant.md)) |
| Needs at home | Traccar reachable on UDP 5027 | Home Assistant 2026.10+, a WireGuard server |
| Diagnostics | Logs in Device Builder over Wi-Fi, or on the SD card (`sd-logs.yaml`); battery voltage with each start, stop and heartbeat | Cellular signal, heap, uptime, reset reason, battery voltage, probe buttons |

The stock telelogger used a few hundred KB on a busy day and 10 to 15 KB on a typical one, logging GPS only, so the Traccar configuration is in the same range with every fix delivered and acknowledged.

The Traccar figures come from a simulation through the real tracking filter and encoder (`tests/test_simulation.py`; IP and UDP headers and Traccar's answers included): an hour of town and highway driving makes about 385 records in 64 packets, 12.3 KB with altitude (11.5 KB without, `traccar_altitude: "false"`); 8 hours parked on LTE makes 32 heartbeats, 3.3 KB. Most of a packet's cost is fixed (about 80 bytes of headers and answer), so fewer, fuller packets save the most:

| Setting (substitution) | Driving, one hour | Parked on LTE, 8 hours |
| --- | --- | --- |
| Defaults: send every 60 s, heartbeat every 15 min | 12.3 KB | 3.3 KB |
| `traccar_send_interval: 120s` | 9.9 KB | |
| `traccar_send_interval: 300s` | 8.4 KB | |
| `traccar_heartbeat_interval: 30min` | | 1.6 KB |
| `traccar_heartbeat_interval: 1h` | | 0.8 KB |

A longer send interval delays what Traccar shows by that much while driving; nothing is lost, and stops and heartbeats still go out at once.

Both use `packages/wifi-first.yaml`: on a known Wi-Fi network LTE is switched off and everything goes over Wi-Fi.

## Traccar

Each GNSS fix goes through a tracking filter in the `traccar_uplink` component, and only some become records:

- **Moving** (speed at or above `traccar_moving_speed`, 6 km/h): a record every `traccar_moving_interval` (10 s), plus one early when the course turns by `traccar_turn_angle` (30°), so corners stay sharp without recording every second.
- **Trip start**: once `traccar_start_fixes` (3) fixes in a row are fast and the car is `traccar_start_distance` (50 m) from where it was parked, with battery voltage and a motion flag. A still receiver with a weak fix (indoors, few satellites) reports a fast fix now and then and drifts tens of metres, which would otherwise start trips while parked and keep the device from sleeping. And only while the car is on (`traccar_start_voltage`, 13.2 V: its 12 V system charging), so a garage, a house or a bench on USB never starts one, however far the fix wanders; a trip under way goes on when the voltage drops (stop-start at a light). **Powered over USB** instead of the OBD port, the device can't see the car's 12 V, so set `traccar_start_voltage: "0"`: trips then start on the GPS rule alone, and a USB socket that stays on with the car off brings back the odd drift trip while parked (one switched off with the car avoids it). `packages/standby.yaml` doesn't sleep on USB power either. The stock telelogger avoids the same drift differently: it switches GPS off 3 minutes after it last saw 2 km/h or a jolt, and wakes only on a 0.4 g jolt, so a parked car sends no positions at all; awake, it sends every fix, and Traccar makes trips from their speed.
- **Trip stop**: after `traccar_stop_delay` (180 s, the telelogger's) below moving speed; sent at once, with battery voltage.
- **Parked**: a heartbeat every `traccar_heartbeat_interval` (15 min, the telelogger's), with battery voltage; `0s` turns it off. Without a fix it carries no position and Traccar keeps the last one.

Records wait in memory and go out together every `traccar_send_interval` (60 s), sooner on a stop, a heartbeat, or a full batch of 40. Traccar answers each packet with the number of records it took; until it does, the records stay queued and are sent again (after 15 s; after three tries the server's name is looked up again). A lost answer can mean Traccar stores a fix twice.

Why Teltonika's GH3000 format: it's the densest location record Traccar decodes (4-byte time, 8 bytes of position, a byte each for course, speed and satellites), and Traccar acknowledges UDP packets, which the Freematics protocol it uses for the stock firmware doesn't do for positions. The Freematics protocol is text, about 60 bytes per fix, and unacknowledged UDP is how data went missing. In Traccar the device is the same: keep its identifier as `traccar_device_id`; it just arrives on port 5027 (Teltonika) instead of 5170 (Freematics). Open UDP 5027 on the server if it isn't.

All of these are substitutions in `packages/uplink-traccar.yaml`; a device config overrides any of them.

### Traccar server settings

- **Teltonika protocol on port 5027 with UDP**: in Traccar 6, **Settings → Protocols** decides each protocol's port and transport (Disabled, TCP, UDP or All), stored in Traccar's database; `traccar.xml` (`protocols.enable`, `teltonika.port`) only seeds those settings the first time Traccar starts, so editing it later changes nothing. Set Teltonika to UDP or All. Behind Docker or a firewall, publish `5027/udp` explicitly (a plain `-p 5027:5027` is TCP only).
- **Or the Freematics protocol** (what the stock firmware used, port 5170): `traccar_protocol: freematics` and `traccar_port: "5170"` switch the uplink to it, for checking a server with a protocol known to work. Traccar doesn't answer Freematics positions, so they count as delivered once sent (nothing is resent, and stored records leave the card), and each fix is about 90 bytes of text against 17; the uplink sends a login event, which Traccar does answer, and logs "Traccar answered the login".
- **The device**: keep its existing identifier; `traccar_device_id` sends it with every packet. Nothing else to set on the device.
- **`status.timeout`** (default 600 s): longer than the parked heartbeat, or a parked car flips to "unknown" between heartbeats. With the 15 minute heartbeat, 1200 or more; with `packages/standby.yaml`, the device reports every 6 hours while asleep, so expect "offline" between reports (Traccar keeps the last position) or raise it past 21600.
- **Leave `filter.outdated` off** (its default): parked heartbeats without a fix carry no position and would be dropped.
- **`filter.duplicate`** only catches a repeat of the latest position, so it doesn't help with the rare batch Traccar stores twice (when its acknowledgement is lost). Leave it as you have it.
- **Trips**: the defaults suit this (motion from speed; a trip ends after `report.trip.minimalParkingDuration`, 300 s, parked).
- **A name that isn't proxied**: Cloudflare's proxy (the orange cloud) and Cloudflare Tunnel carry web traffic only, not UDP, so a proxied name gives the dongle Cloudflare's address and its packets go nowhere. Use a DNS-only record (grey cloud) for the dongle, pointing at the home address, even if the web interface stays proxied under another name.
- **Checking**: `tracker-server.log` shows the Teltonika packets and "Unknown device" if the identifier doesn't match (Traccar doesn't answer unknown devices). From any computer outside the home network (a phone's hotspot), `python3 scripts/traccar_check.py <host> <device id>` sends one time-only record the way the dongle does and says whether Traccar answered.
- **Verified**: the encoder's packets (a time-only record, a fix, a full batch of 40) were checked against Traccar 6.17.0 itself, which decoded them and answered with the acknowledgement the uplink expects (`tests/test_traccar_server.py`).

## Home Assistant

The native API with `outgoing_connection`: the device dials Home Assistant, so HA never needs to reach it behind the carrier's NAT, and no MQTT broker is involved. Simple and live, but every update is its own small exchange and HA keeps the connection alive with pings, so it costs far more data than the Traccar configuration, and it has no timestamps, so nothing can be replayed. Fine on Wi-Fi or with a generous data plan; details in [networking.md](networking.md).

## Later

- **Traccar and Home Assistant together without extra data:** Home Assistant's Traccar Server integration already shows Traccar's devices, so the Traccar configuration reaches HA at no extra cellular cost.
- **A compact uplink straight into Home Assistant:** the same batched, timestamped records into a small HA custom integration, which would give the device tracker, live sensors and back-dated statistics without Traccar. Only worth building if Traccar is to go ([plan.md](plan.md)).
- **MQTT:** not built; it would sit between the two above in data use and adds a broker ([backlog.md](backlog.md)).
