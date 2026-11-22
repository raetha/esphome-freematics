# Configurations

The board package (`packages/freematics-oneplus-b.yaml`) is the same for every dongle; what differs is where the data goes. Each choice is an uplink package plus an example device config. Cellular data is the deciding cost, so each has an estimate (approximate; check against the carrier's usage page).

| | Traccar ([example](https://github.com/raetha/esphome-freematics/blob/main/examples/car-traccar.yaml)) | Home Assistant, native API ([example](https://github.com/raetha/esphome-freematics/blob/main/examples/car-home-assistant.yaml)) | Home Assistant integration ([example](https://github.com/raetha/esphome-freematics/blob/main/examples/car-ha-integration.yaml)) |
| --- | --- | --- | --- |
| Package | `uplink-traccar.yaml` + the `tracker_uplink` component | `uplink-home-assistant.yaml` | `uplink-ha-webhook.yaml` + the `tracker_uplink` component, and the [Freematics integration](https://github.com/raetha/ha-freematics) |
| Transport | UDP to Traccar's Teltonika port, no tunnel | Native API dialled from the device, through WireGuard | HTTPS POST to a Home Assistant webhook (Nabu Casa's cloud or HA's own address), no tunnel |
| Wire format | Teltonika GH3000 (codec 7): 19 bytes per fix with altitude (17 without), batched, acknowledged | Protobuf state updates, batched every 2 s; HA pings every 20 s when idle | Compact batches: about 11 bytes a record while driving, sent every 2 minutes, acknowledged |
| Driving, one hour (defaults) | about 12 KB | about 200 KB | estimated 25 to 50 KB, mostly HTTPS overhead per batch (to be measured) |
| Parked away from Wi-Fi | about 0.4 KB an hour (a heartbeat every 15 min); with `packages/standby.yaml` asleep after 13 minutes, a few hundred bytes per 6-hourly report (estimated, not yet measured) | about 55 KB an hour while connected over LTE (no parked mode yet) | a batch per heartbeat (15 min), about 1 to 2 KB each (estimated); with `standby.yaml` as for Traccar |
| A workday: one hour of driving, 8 hours parked at work, the rest at home on Wi-Fi | about 15 KB | about 650 KB | estimated 60 to 110 KB |
| Offline (no signal) | Fixes queue in memory (300 by default, about 50 minutes of driving) and go out when Traccar answers; lost on reboot unless `sd-traccar.yaml` keeps them on the SD card (50,000) | Nothing kept: HA shows unavailable | Records queue in memory (300) and go out when Home Assistant answers |
| Track timestamps | Each fix carries its GNSS time | HA's arrival time | Each record carries its GNSS time; Home Assistant shows the newest as the current state (it keeps no back-dated history) |
| In Home Assistant | Through Traccar (Traccar Server integration): device tracker and attributes | Native entities; a template device tracker over latitude/longitude ([home-assistant.md](home-assistant.md)) | The integration's device tracker, sensors, a moving sensor and trip events |
| Needs at home | Traccar reachable on UDP 5027 | Home Assistant 2026.10+, a WireGuard server | Home Assistant reachable over HTTPS (Nabu Casa, or its own external address) |
| Diagnostics | Logs in Device Builder over Wi-Fi, or on the SD card (`sd-logs.yaml`); battery voltage with each start, stop and heartbeat | Cellular signal, heap, uptime, reset reason, battery voltage, probe buttons | As Traccar's |

The stock telelogger used a few hundred KB on a busy day and 10 to 15 KB on a typical one, logging GPS only, so the Traccar configuration is in the same range with every fix delivered and acknowledged.

The Traccar figures come from a simulation through the real tracking filter and encoder (`tests/test_simulation.py`; IP and UDP headers and Traccar's answers included): an hour of town and highway driving makes about 385 records in 64 packets, 12.3 KB with altitude (11.5 KB without, `traccar_altitude: "false"`); 8 hours parked on LTE makes 32 heartbeats, 3.3 KB. Most of a packet's cost is fixed (about 80 bytes of headers and answer), so fewer, fuller packets save the most:

| Setting (substitution) | Driving, one hour | Parked on LTE, 8 hours |
| --- | --- | --- | --- |
| Defaults: send every 60 s, heartbeat every 15 min | 12.3 KB | 3.3 KB |
| `traccar_send_interval: 120s` | 9.9 KB | |
| `traccar_send_interval: 300s` | 8.4 KB | |
| `traccar_heartbeat_interval: 30min` | | 1.6 KB |
| `traccar_heartbeat_interval: 1h` | | 0.8 KB |

A longer send interval delays what Traccar shows by that much while driving; nothing is lost, and stops and heartbeats still go out at once.

Both use `packages/wifi-first.yaml`: on a known Wi-Fi network LTE is switched off and everything goes over Wi-Fi.

## Traccar

The board package's trip tracker (`trip_tracker`) decides once per car when a trip starts and stops, so Traccar and Home Assistant always agree; each uplink (`tracker_uplink`) then decides which fixes become its records:

- **Moving** (speed at or above `trip_moving_speed`, 6 km/h): a record every `traccar_moving_interval` (10 s), plus one early when the course turns by `traccar_turn_angle` (30°), so corners stay sharp without recording every second.
- **Trip start**: once `trip_start_fixes` (3) fixes in a row are fast and the car is `trip_start_distance` (50 m) from where it was parked, with battery voltage, temperature, cellular signal and a motion flag. A still receiver with a weak fix (indoors, few satellites) reports a fast fix now and then and drifts tens of metres, which would otherwise start trips while parked and keep the device from sleeping. And only within `trip_start_motion` (5 minutes) of the motion sensor feeling the car move (getting in, a door, the road; `motion_threshold`, 100 mg between samples at 100 Hz), so a garage, a house or a desk never starts one however far the fix wanders, whether the dongle is powered from the OBD port or over USB. Without a supported motion sensor (the Model B's ICM-42627 is), the GPS rule decides alone. The stock telelogger avoids the same drift differently: it switches GPS off 3 minutes after it last saw 2 km/h or a jolt, and wakes only on a 0.4 g jolt, so a parked car sends no positions at all; awake, it sends every fix, and Traccar makes trips from their speed.
- **Trip stop**: after `trip_stop_delay` (180 s, the telelogger's) below moving speed; sent at once, with the same extras as a start.
- **Parked**: a heartbeat every `traccar_heartbeat_interval` (15 min, the telelogger's), with the same extras; `0s` turns it off. Without a fix it carries no position and Traccar keeps the last one.
- **Impact**: when the motion sensor measures more than `impact_threshold` (4 g beyond gravity; hard braking is under 1 g), a record with the impact's peak goes out at once, wherever the car is in its trip.

The extras are a few bytes on records that are rare anyway (starts, stops, heartbeats). Traccar's GH3000 decoder names only battery and motion; the rest arrive as `io` attributes, with Traccar's unsigned values:

| Attribute | Value |
| --- | --- |
| `io72` | The dongle's temperature in °C (its motion sensor's chip); above 127 is below zero: subtract 256 |
| `io205` | The cellular signal as −dBm (85 is −85 dBm); absent on Wi-Fi |
| `io247` | An impact's peak in tenths of a g (64 is 6.4 g) |

A Traccar computed attribute can turn them into named ones, or raise an alarm on `io247` (Settings → Computed attributes: `io247 ? "accident" : null` for `alarm`). With `traccar_protocol: freematics`, Traccar names the temperature and signal itself (`deviceTemp`, `rssi`); impacts aren't sent in that protocol.

Records wait in memory and go out together every `traccar_send_interval` (60 s), sooner on a stop, a heartbeat, or a full batch of 40. Traccar answers each packet with the number of records it took; until it does, the records stay queued and are sent again (after 15 s; after three tries the server's name is looked up again). A lost answer can mean Traccar stores a fix twice.

Why Teltonika's GH3000 format: it's the densest location record Traccar decodes (4-byte time, 8 bytes of position, a byte each for course, speed and satellites), and Traccar acknowledges UDP packets, which the Freematics protocol it uses for the stock firmware doesn't do for positions. The Freematics protocol is text, about 60 bytes per fix, and unacknowledged UDP is how data went missing. In Traccar the device is the same: keep its identifier as `traccar_device_id`; it just arrives on port 5027 (Teltonika) instead of 5170 (Freematics). Open UDP 5027 on the server if it isn't.

The `traccar_*` settings are substitutions in `packages/uplink-traccar.yaml`, the `trip_*`, `motion_threshold` and `impact_threshold` ones in the board package; a device config overrides any of them.

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

## Home Assistant integration

The Traccar configuration's tracking, sent to Home Assistant instead (or as well: `uplink-ha-webhook.yaml` works beside `uplink-traccar.yaml`). Add the device in Home Assistant's [Freematics integration](https://github.com/raetha/ha-freematics): it creates a webhook and shows its URL (Nabu Casa's cloud address when Home Assistant Cloud is on, otherwise Home Assistant's own external address), which goes into Device Builder's secrets as `ha_webhook_url`. Nothing at home is opened to the internet beyond what Home Assistant already serves.

Each batch is one HTTPS request: the records in a compact binary form (`components/tracker_uplink/compact.h`: the first record in full, then only what changed, about 11 bytes a record while driving against GH3000's 19, sent as base64 text, about 15, because Home Assistant Cloud relays a webhook's body as a UTF-8 string that binary wouldn't survive), posted every `ha_send_interval` (2 minutes) and at once on a trip start, stop or heartbeat. Home Assistant's answer acknowledges the batch; until then the records stay queued. Most of the cost is the request itself (TCP, TLS and HTTP headers, roughly 1 to 2 KB): the client keeps the connection open between batches while the server allows, and resumes the TLS session with a ticket when it doesn't. The posting runs in a task of its own, so the main loop never waits for the network. The webhook URL is the only key, so it never appears in the logs.

The same extras ride along (temperature, cellular signal, an impact's peak), in a block of tagged values a reader skips when it doesn't know them, so newer dongles keep working with older versions of the integration. Before sleeping, the dongle adds when it will report again (its next heartbeat wake), so the integration's Reporting sensor knows a quiet night from a dongle that's gone.

Home Assistant can't store states with past timestamps: records sent late (after a coverage gap) update the device to the newest one, and their trip starts and stops still arrive as events with their real times, but there's no track history as in Traccar. The integration adds up each trip from its points (distance, duration, top and average speed) and reports it with the trip's stop. For a track history, run both.

## Home Assistant

The native API with `outgoing_connection`: the device dials Home Assistant, so HA never needs to reach it behind the carrier's NAT, and no MQTT broker is involved. Simple and live, but every update is its own small exchange and HA keeps the connection alive with pings, so it costs far more data than the Traccar configuration, and it has no timestamps, so nothing can be replayed. Fine on Wi-Fi or with a generous data plan; details in [networking.md](networking.md).

## Later

- **Traccar and Home Assistant together without extra data:** Home Assistant's Traccar Server integration already shows Traccar's devices, so the Traccar configuration reaches HA at no extra cellular cost.
- **Back-dated statistics in the Home Assistant integration:** values from late batches (battery voltage, distance) as long-term statistics ([backlog.md](backlog.md)).
- **MQTT:** not built; it would sit between the Traccar and native API configurations in data use and adds a broker ([backlog.md](backlog.md)).
