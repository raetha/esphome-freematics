# Plan and status

Where the project stands, what's decided, and what comes next. Keep this current with each step; the chat that produced it isn't the record.

## Status

**Phase 1 (Traccar uplink, GPS, Wi-Fi first) is built and ready to flash**, not yet tried on hardware (as of 2026-11-19). Two configurations: Traccar (the low-data one, the owner's choice) and Home Assistant ([configurations.md](configurations.md)). The packages validate with ESPHome 2026.11.0, the encoder and tracking filter pass their host tests, and CI compiles both examples. The dongles have SIM7070G modules on Hologram (APN `hologram`), which is pay as you go: the stock telelogger's 10 to 15 KB on a typical day is the benchmark.

## Goal and requirements

Replace the Freematics telelogger firmware on two ONE+ Model B dongles with ESPHome. The stock firmware loses data and has triggered warnings on the Volvo and the Subaru.

| # | Requirement | Priority |
| --- | --- | --- |
| R1 | Cache every sample on the device while offline, replay it later with the original timestamps | Must |
| R0 | Keep cellular data to the bare minimum: every design choice is weighed by its bytes on LTE | Must |
| R2 | Deliver to Traccar (the owner's choice) or Home Assistant, with delayed fixes stored at their real time | Must |
| R3 | GPS position as a Home Assistant device_tracker | Must |
| R4 | Wi-Fi preferred over cellular, several known networks | Must |
| R5 | Read the vehicle without upsetting it: passive only, nothing sent on the bus | Must |
| R6 | VIN, battery voltage, fuel level and the PIDs the telelogger reported | Should |
| R7 | IMU data | Could |

## Decisions

- **Traccar first, over Teltonika GH3000 UDP.** The native API costs far too much data for a pay-as-you-go plan (about 650 KB against 15 KB on a workday). The device sends compact, timestamped, acknowledged records straight to the owner's Traccar; no tunnel, no Home Assistant traffic from the car. GH3000 because it's the densest format Traccar decodes and Traccar acknowledges each packet. (2026-11-19, replacing the earlier native-API-first decision)
- **The repo keeps every configuration as an example** with its pros, cons and data use ([configurations.md](configurations.md)): Traccar and Home Assistant now, MQTT and a direct compact uplink into HA maybe later. (2026-11-19)
- **Home Assistant configuration:** the native API dialled from the device (`api: outgoing_connection:`, HA 2026.10+) through WireGuard, not MQTT. (2026-11-19)
- **Wi-Fi first like the telelogger:** LTE is switched off while a known Wi-Fi network is connected. (2026-11-19)
- **Data is the top constraint.** Update frequency and triggers follow the telelogger's where they make sense and are all substitutions. Position only while moving; vehicle values (later) once per stop. (2026-11-19)
- **The vehicle bus is read passively only:** no OBD requests, no writes. Active polling may be tested later, carefully. (2026-11-19)
- **Replayed sensor values in HA** would be hourly long-term statistics through an HA integration in its own repo (HACS: one integration per repo), if HA ever receives the compact uplink directly. (2026-11-19)
- **One repo for ESPHome work** (this one, on `dev`); whether generic components move elsewhere is decided later. (2026-11-19)
- **The examples are paste-and-build**: a device config pasted into ESPHome Device Builder fetches the packages and components from GitHub. (2026-11-19)
- **Build one piece at a time**, YAML stopgap first, then a component that replaces it.

## Phases

1. **Phases 0 and 1: cellular, GPS to Traccar, Wi-Fi first** (built). Back up the stock flash (`esptool.py --port <port> read_flash 0 0x1000000 stock-car1.bin`). Paste `examples/car-traccar.yaml` into a new device in ESPHome Device Builder, set `traccar_host` and `traccar_device_id` (the device's existing Traccar identifier works), add the secrets, open UDP 5027 on the Traccar server, and install over USB from the browser. On the bench, watch the USB log: PPP up, `ATGPS` fixes, records queued, "Traccar acknowledged". Then drive: the trip in Traccar with sharp corners, a stop record, heartbeats while parked; Wi-Fi at home switching LTE off and back on when leaving. Read the real data use from Hologram after a few days. *Exit:* a week of commutes in one car with every trip complete in Traccar and data use near the estimate in [configurations.md](configurations.md).
2. **Phase 2: `freematics_link` component.** Moves the co-processor UART, the GNSS parsing and a GNSS time source into C++, adds battery voltage from `ATRV` and the co-processor version from `ATI`. *Exit:* the same behavior without the lambda.
3. **Phase 3: store and forward on microSD.** The Traccar uplink's in-memory queue (300 fixes) survives coverage gaps but not a reboot or a long outage; move it to the SD card. *Exit:* three forced outages and two reboots with nothing lost.
4. **Phase 4: vehicle data, passively.** Find out what the co-processor can hear without sending anything (`ATM1`-style monitoring, broadcast CAN frames); report it once per stop. Many cars put a gateway between the OBD port and their internal buses, so there may be little to hear. Active OBD requests only later, as a separate, carefully tested step.

## Components (one at a time)

| Component | Does | Status |
| --- | --- | --- |
| `traccar_uplink` | Tracking filter (moving, turns, start, stop, heartbeat), in-memory queue, Teltonika GH3000 over UDP with acknowledgements, motion triggers | Built (phase 1) |
| `freematics_link` | Owns the co-processor UART: reset, `ATI` version, `ATGPSON`, `ATGPS` polling into fixes, sensors and a time source, `ATRV` battery voltage; later passive vehicle data | On `dev`, being tested on the bench |
| SD-backed queue | Records survive reboots and long outages | Phase 3 |

## Open questions

- [ ] The stock telelogger's serial boot log from one dongle (`TYPE:` and `GNSS:OK(I)` or `OK(E)` lines) to confirm the co-processor version and GNSS path.
- [ ] Which OBD values matter beyond fuel level?
- [ ] Does the Traccar server have UDP 5027 (Teltonika) open, and does the device keep its identifier?
- [ ] Home Assistant configuration only: where the WireGuard server runs, and whether its endpoint works from inside the home network.
- [ ] What the co-processor can hear passively on each car's OBD port.

## Log

- 2026-11-19: Third bench test (0.1.0-dev.4, charger, logs over Wi-Fi): the device had restarted shortly before the log began (its clock had run 263 s without a sync, and safe mode marked the boot good 4 s into the log), and the API dropped right after the uplink re-sent a record Traccar hadn't acknowledged within 15 s, as in the second test. The uplink sent a location record, so the co-processor most likely answered `ATGPS`, but its replies are logged only at VERBOSE and its start-up before the log began. The board package now has `debug:` (reset reason and uptime) and both components report their state when a log viewer connects.

- 2026-11-19: The clock now comes from GPS first; SNTP (ESPHome's default every 15 minutes, about 300 bytes a sync, 15 to 30 KB a day over LTE) became a fallback every 12 hours. Cellular network time was set aside: it needs AT commands alongside PPP and not every carrier sends it.

- 2026-11-19: Second bench test on a 2 A charger, logs over Wi-Fi: Wi-Fi connected, SNTP synced and the uplink sent a heartbeat (27 bytes), so the first test's Wi-Fi failure was power. The co-processor still answered no `ATGPS` and the modem no AT command. Merged `freematics_link` (phase 2) into `dev` in place of the UART lambda: it resets the co-processor, identifies it with `ATI` and logs what came back when a reply is missing. The modem pins became substitutions, since the library moves them to GPIO32/33 for co-processor version 13.
- 2026-11-19: First bench test (0.1.0-dev.3, powered from a computer's USB port): Wi-Fi failed with "Auth Expired" on every access point, the modem didn't answer its first two PWRKEY presses, and the co-processor answered no `ATGPS` in the 20 seconds logged. The Traccar example gained the native API (no Home Assistant, `reboot_timeout: 0s`) so Device Builder shows logs over Wi-Fi.

- 2026-11-19: Adopted ESPHome's component rules as a checklist (no quality scale exists); `traccar_uplink` now allocates its queue and buffers once at startup, uses loop time, and has a component test config; clang-format and clang-tidy in CI.
- 2026-11-19: Repository public; CI automatic again. Examples fetch everything from GitHub so they can be pasted into ESPHome Device Builder; CI builds them from the checkout (`scripts/localize.py`).
- 2026-11-19: Traccar first: the native API would use about 40 times the data of the stock firmware on a pay-as-you-go plan. Built the `traccar_uplink` component (Teltonika GH3000 over UDP, tracking filter, acknowledged batches), split the package into board, Wi-Fi-first and uplink packages with an example per configuration, and made the vehicle bus read-only.
- 2026-11-19: Switched phases 0 to 2 from MQTT to the native API dialled from the device, over WireGuard; data use is now the top requirement. SIM7070G modules on Hologram. CI made manual-only.
- 2026-11-19: Plan drafted; pins and co-processor commands taken from the ONE+ r14 schematic and the FreematicsPlus library; phase 0 config written and validated; repository set up on `dev`.
