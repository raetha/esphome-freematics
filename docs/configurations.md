# Configurations

The board package (`packages/freematics-oneplus-b.yaml`) is the same for every dongle; what differs is where the data goes. Each choice is an uplink package plus an example device config. Cellular data is the deciding cost, so each has an estimate (approximate; check against the carrier's usage page).

| | Traccar ([example](https://github.com/raetha/esphome-freematics/blob/main/examples/car-traccar.yaml)) | Home Assistant, native API ([example](https://github.com/raetha/esphome-freematics/blob/main/examples/car-home-assistant.yaml)) |
| --- | --- | --- |
| Package | `uplink-traccar.yaml` + the `traccar_uplink` component | `uplink-home-assistant.yaml` |
| Transport | UDP to Traccar's Teltonika port, no tunnel | Native API dialled from the device, through WireGuard |
| Wire format | Teltonika GH3000 (codec 7): 17 bytes per fix, batched, acknowledged | Protobuf state updates, batched every 2 s; HA pings every 20 s when idle |
| Driving, one hour (defaults) | about 12 KB | about 200 KB |
| Parked away from Wi-Fi | about 0.4 KB an hour (a heartbeat every 15 min) | about 55 KB an hour while connected over LTE (no parked mode yet) |
| A workday: one hour of driving, 8 hours parked at work, the rest at home on Wi-Fi | about 15 KB | about 650 KB |
| Offline (no signal) | Fixes queue in memory (300 by default, about 50 minutes of driving) and go out when Traccar answers; lost on reboot | Nothing kept: HA shows unavailable |
| Track timestamps | Each fix carries its GNSS time | HA's arrival time |
| In Home Assistant | Through Traccar (Traccar Server integration): device tracker and attributes | Native entities; a template device tracker over latitude/longitude ([home-assistant.md](home-assistant.md)) |
| Needs at home | Traccar reachable on UDP 5027 | Home Assistant 2026.10+, a WireGuard server |
| Diagnostics | USB log only; battery voltage with each start, stop and heartbeat | Cellular signal, heap, uptime, reset reason, battery voltage, probe buttons |

The stock telelogger used a few hundred KB on a busy day and 10 to 15 KB on a typical one, logging GPS only, so the Traccar configuration is in the same range with every fix delivered and acknowledged.

Both use `packages/wifi-first.yaml`: on a known Wi-Fi network LTE is switched off and everything goes over Wi-Fi.

## Traccar

Each GNSS fix goes through a tracking filter in the `traccar_uplink` component, and only some become records:

- **Moving** (speed at or above `traccar_moving_speed`, 6 km/h): a record every `traccar_moving_interval` (10 s), plus one early when the course turns by `traccar_turn_angle` (30°), so corners stay sharp without recording every second.
- **Trip start**: the first fast fix, with battery voltage and a motion flag.
- **Trip stop**: after `traccar_stop_delay` (180 s, the telelogger's) below moving speed; sent at once, with battery voltage.
- **Parked**: a heartbeat every `traccar_heartbeat_interval` (15 min, the telelogger's), with battery voltage; `0s` turns it off. Without a fix it carries no position and Traccar keeps the last one.

Records wait in memory and go out together every `traccar_send_interval` (60 s), sooner on a stop, a heartbeat, or a full batch of 40. Traccar answers each packet with the number of records it took; until it does, the records stay queued and are sent again (after 15 s; after three tries the server's name is looked up again). A lost answer can mean Traccar stores a fix twice.

Why Teltonika's GH3000 format: it's the densest location record Traccar decodes (4-byte time, 8 bytes of position, a byte each for course, speed and satellites), and Traccar acknowledges UDP packets, which the Freematics protocol it uses for the stock firmware doesn't do for positions. The Freematics protocol is text, about 60 bytes per fix, and unacknowledged UDP is how data went missing. In Traccar the device is the same: keep its identifier as `traccar_device_id`; it just arrives on port 5027 (Teltonika) instead of 5170 (Freematics). Open UDP 5027 on the server if it isn't.

All of these are substitutions in `packages/uplink-traccar.yaml`; a device config overrides any of them.

### Traccar server settings

- **Teltonika protocol on port 5027 with UDP**: in Traccar's protocol settings, transport UDP or All. Behind Docker or a firewall, publish `5027/udp` explicitly (a plain `-p 5027:5027` is TCP only).
- **The device**: keep its existing identifier; `traccar_device_id` sends it with every packet. Nothing else to set on the device.
- **`status.timeout`** (default 600 s): longer than the parked heartbeat, or a parked car flips to "unknown" between heartbeats. With the 15 minute heartbeat, 1200 or more.
- **Leave `filter.outdated` off** (its default): parked heartbeats without a fix carry no position and would be dropped.
- **`filter.duplicate`** only catches a repeat of the latest position, so it doesn't help with the rare batch Traccar stores twice (when its acknowledgement is lost). Leave it as you have it.
- **Trips**: the defaults suit this (motion from speed; a trip ends after `report.trip.minimalParkingDuration`, 300 s, parked).
- **Checking**: `tracker-server.log` shows the Teltonika packets and "Unknown device" if the identifier doesn't match.

## Home Assistant

The native API with `outgoing_connection`: the device dials Home Assistant, so HA never needs to reach it behind the carrier's NAT, and no MQTT broker is involved. Simple and live, but every update is its own small exchange and HA keeps the connection alive with pings, so it costs far more data than the Traccar configuration, and it has no timestamps, so nothing can be replayed. Fine on Wi-Fi or with a generous data plan; details in [networking.md](networking.md).

## Later

- **Traccar and Home Assistant together without extra data:** Home Assistant's Traccar Server integration already shows Traccar's devices, so the Traccar configuration reaches HA at no extra cellular cost.
- **A compact uplink straight into Home Assistant:** the same batched, timestamped records into a small HA custom integration, which would give the device tracker, live sensors and back-dated statistics without Traccar. Only worth building if Traccar is to go ([plan.md](plan.md)).
- **MQTT:** not built; it would sit between the two above in data use and adds a broker ([backlog.md](backlog.md)).
