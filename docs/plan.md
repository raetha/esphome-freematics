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
4. **Phase 4: vehicle data, passively.** Researched in [vehicle-data.md](vehicle-data.md): the co-processor's only listening mode (`ATM1`) isn't documented as silent, and a CAN controller that isn't silent acknowledges frames and, at a wrong bit rate, sends error frames. Nothing is built until Freematics confirms a listen-only mode; both cars' gateways may leave little to hear anyway. Active OBD requests only later, as a separate, carefully tested step.

## Components (one at a time)

| Component | Does | Status |
| --- | --- | --- |
| `traccar_uplink` | Tracking filter (moving, turns, start, stop, heartbeat), in-memory queue, Teltonika GH3000 over UDP with acknowledgements, motion triggers | Built (phase 1) |
| `freematics_link` | Owns the co-processor UART: reset, `ATI` version, `ATGPSON`, `ATGPS` polling into fixes, sensors and a time source, `ATRV` battery voltage; later passive vehicle data | On `dev`, being tested on the bench |
| `sd_card`, `file_queue` | Generic: mount a microSD card over an ESPHome SPI bus; a first-in, first-out file of fixed-size records that survives resets (two CRC-checked headers). The Traccar uplink keeps every unacknowledged record in one (`packages/sd-traccar.yaml`) | On `dev`, opt-in; to test on the bench (phase 3) |
| `file_logger` | Generic: the device's log written to a file on the card, in small chunks from the loop, rotated at a size (`packages/sd-logs.yaml`) | On `dev`, opt-in; to test on the bench |

## Open questions

- [ ] The stock telelogger's serial boot log from one dongle (`TYPE:` and `GNSS:OK(I)` or `OK(E)` lines) to confirm the co-processor version and GNSS path.
- [ ] Which OBD values matter beyond fuel level?
- [ ] Does the Traccar server have UDP 5027 (Teltonika) open, and does the device keep its identifier?
- [ ] Home Assistant configuration only: where the WireGuard server runs, and whether its endpoint works from inside the home network.
- [ ] Does the co-processor's `ATM1` sniffing run CAN in listen-only (silent) mode, or does it accept `ATCSM1`? Only Freematics can say ([vehicle-data.md](vehicle-data.md)).
- [ ] What the co-processor can hear passively on each car's OBD port.

## Log

- 2026-11-20: Phase 4, step one built: `examples/bench-obd-listen.yaml`, a raw-UART bench file (not `freematics_link`, whose command set stays fixed) that sends `ATI`, `ATE0`, `ATH1`, `ATCSM1`, `ATSP6`, `ATM1`, listens 60 s, `ATM0`, and resets the co-processor. The plan for the XC60 is in vehicle-data.md; standard OBD-II requests stay unbuilt until the owner decides to allow sending.

- 2026-11-20: Sleep while parked built (`packages/standby.yaml`, opt-in), for a weekend in the car. Stock telelogger standby for reference: GNSS off, `ATLP` (co-processor low power), the ESP32 stays awake polling the IMU, with 14 V as the wake voltage when there's no IMU and a ping every 900 s. Here the ESP32 deep-sleeps instead: after 10 minutes with the car off (below 13.2 V, above 9 V so never on USB) and Traccar's tracker not moving, it flushes the queue, sends `ATGPSOFF` and `ATLP`, disables the modem and sends `AT+CPOWD=1`, holds GPIO27/15/25 and sleeps 60 s; each timer wake reads the ADC in `on_boot` at priority 599.5 (after the sensors, before `freematics_link`, now at DATA - 1, and before Wi-Fi and the modem) and sleeps again unless the car is on or the 6 h heartbeat is due (a counter in RTC memory). A heartbeat wake sleeps again once Traccar has acknowledged, or after 180 s. `file_queue` and `file_logger` now sync in `on_shutdown`. Motion wake isn't built: the IMU model (MPU-9250 or ICM-20948) and its interrupt pin aren't confirmed, so the package logs the IMU's WHO_AM_I at start. Car-Assistant (ShonP40) and its issue #22957 read; ideas noted in backlog.md.

- 2026-11-20: ESPHome issue drafted for the owner to post (the session can't open issues on esphome/esphome): the SIM7070's SIM stays off after the modem component's `AT+CFUN=0` / `AT+CFUN=4`, likely since #22817; no existing issue for the SIM7070, and #22957 (SIM7600, "won't read my SIM", opened 2026-11-19) may be the same cause. Owner's priorities: the device tracker first; vehicle data (VIN, fuel level, PHEV battery level, as much of the Volvo cloud integration's data as the car gives directly) on the 2023 XC60 Recharge, with a listening test allowed if it's careful.

- 2026-11-20: **LTE works in the full Traccar config** (test 4, `lte_on_wifi: "true"`), after an install and after a cold start: one 200 ms press, the modem component 8 s later, PPP up about 20 s after boot (Hologram, 10.184.x.x), Traccar acknowledging; the 111 drive records on the SD card were delivered over Wi-Fi first. `AT+IPR=115200` is accepted; `AT&W` answers ERROR on the SIM7070 (removed; the SIM7070 stores IPR itself). After `AT+CFUN=1,1` the SIM was READY within 3 s and the component went on without a second sync, so the restart also covers the CFUN=1 SIM fix. Next: test 5 (LTE only, away from Wi-Fi), a drive with the Wi-Fi search pause, the second dongle, and reporting the CFUN=4 SIM issue to ESPHome.

- 2026-11-20: Raw probe, warm after the silent run: nothing, even with 1 s presses, without the buzzer or ADC in the config. After unplugging: answered 6 s after the first 200 ms press (CSQ 9, LTE-M online, APN `hologram` kept from ESPHome's earlier write). So the press works from cold, and the module was stuck, not off. Every success so far had the module already on before the modem component started; every failure had the component's recovery bytes (`+++`, CMUX close frames) arrive while the module was booting. Likely the SIM7070 sets its serial speed from the first bytes it hears (auto-bauding), though not proven. The board package now presses, waits 8 s (`modem_boot_wait`), then enables the component; fixes the speed with `AT+IPR=115200` and `AT&W` at the first sync, before the `AT+CFUN=1,1` restart (which would otherwise reboot it under the component's probes); and every third failed try holds PWRKEY 2.5 s to switch a stuck module off.

- 2026-11-20: Bench 3b with the buzzer and battery ADC (warm restart after a cold start): the modem never answered, as in the full config. Two explanations fit every run so far. (1) The buzzer or the ADC gets in the way. (2) A 200 ms press doesn't switch the SIM7070 on from off: every run where the modem answered followed one that had left it on (the first probe run of the morning reached its 1 s presses), and every run after a cold start (the drive, this one) got nothing. The library presses 200 ms every 2.2 s, up to 30 times; ours every 10 s, 5 times. The raw probe now tells them apart: an answer at try 1 means the module was on (explanation 1), an answer only from the 1 s presses means explanation 2.

- 2026-11-20: The owner asked for a modem power cycle at every ESP32 boot (a car dongle rarely loses power). The board can't cut the module's power, so the first sync after boot sends `AT+CFUN=1,1` (the SIM7070's full restart); the next sync gets the CFUN=1 SIM fix. Substitution `modem_restart_on_boot`. The battery ADC reads near 0 V on USB, which can't affect the modem; the bench test confirms.

- 2026-11-20: Bench 3b with the co-processor link: the modem still synced, so the link isn't the blocker (left: buzzer, battery ADC, SD card, file logger, uplink, Wi-Fi first). That run started with the modem still in the previous run's CMUX/PPP session (the ESP32 restarted over USB, the modem kept power: `Autodetect mode: 5`, "Modem warm reboot. Exiting CMUX"); after that, each try reached `starting PPP` and failed to enter CMUX ("Failed to enter PPP. Resetting modem.", PPP error 5), every 20 s. The next run power-cycles the dongle first, then restarts over Wi-Fi to see whether a warm restart always breaks PPP. If it does, the board package needs a way to restart the module at boot (an update or crash restarts only the ESP32).

- 2026-11-20: Confirmed: after `AT+CFUN=1` the SIM was READY within a second and PPP came up 5 s later (Hologram IP, DNS 8.8.8.8). The board package now sends `AT+CFUN=1` on every modem sync. Upstream: ESPHome's modem component should power the SIM7070 up with CFUN=1 (its CFUN=4 step leaves the SIM off); to report once the full config connects. Bench 3b now adds the co-processor link beside the modem.

- 2026-11-20: Test 3b: alone, ESPHome's modem component synced with the module in 2 s (`Autodetect mode: 1`), so in the full config something else gets in its way (next: add the co-processor link, the buzzer and the SD card back one at a time). The SIM then answered `+CME ERROR: SIM failure` for as long as the log ran, where the raw probe had seen `READY`. The component sends `AT+CFUN=0` (SIM powered down, to write the APN) and then `AT+CFUN=4`, which on the SIM7070 seems not to power the SIM back up; the bench config now sends `AT+CFUN=1` after syncing to test that. If it's confirmed, it's an upstream ESPHome issue, and the board package gets the same workaround.

- 2026-11-20: Bench probe (test 3) on good power: the module answered the first 200 ms press within a second (`SIM7070`, revision 1951.04), SIM ready, CSQ 11, registered on LTE-M (`+CPSI: LTE CAT-M1,Online,310-410`), before Wi-Fi had connected. So the hardware, pins, press and SIM are fine, and the Wi-Fi handoff isn't what blocks it; ESPHome's modem component in the full config still hears nothing (its mode detection, then `Modem not responding at 115200 baud`). Ruled out by reading the code: esp_modem doesn't route its default RTS pin (GPIO27, our PWRKEY) without flow control, and the UARTs don't collide (logger 0, co-processor 1, modem 2). `examples/bench-modem-esphome.yaml` runs the modem component alone to split the two remaining causes: the rest of the config, or the component's start-up.

- 2026-11-20: Wi-Fi search paused while moving with no Wi-Fi connected (owner's call: a hotspot connected before moving stays), resumed after 30 s still or without a fix. The next drive shows whether the scanning hurt GNSS.

- 2026-11-20: First drive (0.1.0-dev.11, OBD power, SD log at VERBOSE, about 27 minutes): the modem never answered on the car's power either (47 PWRKEY presses over the morning, no byte back), so power isn't the cause. No GNSS fix for the first 9 minutes of driving (the device had sat in the garage, and unplugging it from the bench means a cold start), then 4 to 5 satellites, flickering for 4 minutes, then steady; 111 records went to the card for Traccar. Wi-Fi scanned every few seconds the whole drive. The stock firmware's CSV files on the card show cellular signal readings (key 81, -51 to -111 dBm), so this dongle's module has worked under the stock firmware, and 5 to 10 satellites on the same roads; it also polled OBD speed, RPM, throttle, load and coolant at about 1 Hz. The stock firmware sends the GNSS one more command after `ATGPSON`: `ATGDS` with a u-blox M10 `CFG-VALSET` that only sets NMEA message rates.

- 2026-11-20: Beeps as the stock firmware (`telelogger.ino`): 50 ms at 2 kHz on GPIO25 when Wi-Fi or cellular comes up, plus 200 ms at start-up so a running device can be heard in the car; `beep_on_boot` and `beep_on_network` turn them off.

- 2026-11-20: Traccar answers: the cause was a firewall rule on the owner's side. 0.1.0-dev.11 on the bench sent a record over Wi-Fi and Traccar acknowledged it within 90 ms, with the record kept on the SD card until then. Still open: the modem never answers (on the car's power next), and ESPHome still warns about the occasional 55 to 60 ms SD write (`traccar_uplink` queuing at boot, `file_queue`'s 30-second sync); a sync on its own task would remove that if it matters.

- 2026-11-20: At home the device reaches Traccar through local DNS on the internal address, with no port forwarding, and still gets no answer, so the problem is Traccar's side. Found while testing locally: Traccar 6 keeps protocol ports and transports in its database (Settings → Protocols), and `traccar.xml` only seeds them on first start. Added `traccar_protocol: freematics` (port 5170, the stock firmware's protocol) as a check, verified against Traccar 6.17.0: positions decoded, login answered.

- 2026-11-20: USB install from a computer with every SD package on: the record file was created and records queued "on file"; ESPHome warned that `file_logger` (52 to 88 ms) and, through the record queue, `freematics_link` (93 ms) blocked the loop, so the log writer now runs in a task of its own and the record file is made full size at creation and synced every 30 s instead of on every change. On the computer's USB port Wi-Fi again failed with "Auth Expired" right after a PWRKEY press, which fits too little power for the cellular module to start; the modem tests need the charger or the car.

- 2026-11-20: Bench, 0.1.0-dev.9 with `sd-card.yaml` and `sd-logs.yaml`: the card mounted (SD04G, 3.7 GB, 20 MHz) and the log file grew about 300 bytes a second, most of it the configuration dump repeated on every log-viewer connection. Free heap 178 KB. Traccar still unanswered (test 1 pending).

- 2026-11-20: Overnight: the encoder's packets checked against a real Traccar 6.17.0 (acknowledged, decoded right), so the missing acknowledgements are on the network path; `scripts/traccar_check.py` checks a server from anywhere. Bench switches: `lte_on_wifi`, `modem_pwrkey_press`, `modem_pwrkey_tries`. SD storage rebuilt on `dev` as opt-in packages, plus `file_logger` for logs on the card. The bench test plan is `docs/bench-tests.md`.

- 2026-11-20: The modem probe (200 ms presses, GPIO35/GPIO2) got no byte back in a minute. With co-processor 1.4 those are the library's pins, so the probe now lengthens the press (200 ms, 1 s, 1.5 s) in one run. The API log connection still dropped every minute or so with none of this project's components running and Wi-Fi power saving off, so the drops are not caused by them. OTA in the examples is now encrypted with the API key (ESPHome's recommended form) instead of a separate password.

- 2026-11-19: Fourth bench test (0.1.0-dev.6): the co-processor works (version 1.4, 12 satellites, a fix at home). The API disconnects are not restarts: uptime ran on across them (reset reason: the OTA restart). The modem still answered nothing to ESPHome's modem component, so `examples/bench-modem-probe.yaml` talks to it the library's way, with every byte logged. Traccar never acknowledged, though the device resolved the server and sent to UDP 5027; the server's name resolves publicly (the owner checked; a check from the session wrongly said otherwise, as the session's own DNS doesn't reach outside names).

- 2026-11-19: Third bench test (0.1.0-dev.4, charger, logs over Wi-Fi): the device had restarted shortly before the log began (its clock had run 263 s without a sync, and safe mode marked the boot good 4 s into the log), and the API dropped right after the uplink re-sent a record Traccar hadn't acknowledged within 15 s, as in the second test. The uplink sent a location record, so the co-processor most likely answered `ATGPS`, but its replies are logged only at VERBOSE and its start-up before the log began. The board package now has `debug:` (reset reason and uptime) and both components report their state when a log viewer connects.

- 2026-11-19: The clock now comes from GPS first; SNTP (ESPHome's default every 15 minutes, about 300 bytes a sync, 15 to 30 KB a day over LTE) became a fallback every 12 hours. Cellular network time was set aside: it needs AT commands alongside PPP and not every carrier sends it.

- 2026-11-19: Second bench test on a 2 A charger, logs over Wi-Fi: Wi-Fi connected, SNTP synced and the uplink sent a heartbeat (27 bytes), so the first test's Wi-Fi failure was power. The co-processor still answered no `ATGPS` and the modem no AT command. Merged `freematics_link` (phase 2) into `dev` in place of the UART lambda: it resets the co-processor, identifies it with `ATI` and logs what came back when a reply is missing. The modem pins became substitutions, since the library moves them to GPIO32/33 for co-processor version 13.
- 2026-11-19: First bench test (0.1.0-dev.3, powered from a computer's USB port): Wi-Fi failed with "Auth Expired" on every access point, the modem didn't answer its first two PWRKEY presses, and the co-processor answered no `ATGPS` in the 20 seconds logged. The Traccar example gained the native API (no Home Assistant, `reboot_timeout: 0s`) so Device Builder shows logs over Wi-Fi.

- 2026-11-19: Adopted ESPHome's component rules as a checklist (no quality scale exists); `traccar_uplink` now allocates its queue and buffers once at startup, uses loop time, and has a component test config; clang-format and clang-tidy in CI.
- 2026-11-19: Repository public; CI automatic again. Examples fetch everything from GitHub so they can be pasted into ESPHome Device Builder; CI builds them from the checkout (`scripts/localize.py`).
- 2026-11-19: Traccar first: the native API would use about 40 times the data of the stock firmware on a pay-as-you-go plan. Built the `traccar_uplink` component (Teltonika GH3000 over UDP, tracking filter, acknowledged batches), split the package into board, Wi-Fi-first and uplink packages with an example per configuration, and made the vehicle bus read-only.
- 2026-11-19: Switched phases 0 to 2 from MQTT to the native API dialled from the device, over WireGuard; data use is now the top requirement. SIM7070G modules on Hologram. CI made manual-only.
- 2026-11-19: Plan drafted; pins and co-processor commands taken from the ONE+ r14 schematic and the FreematicsPlus library; phase 0 config written and validated; repository set up on `dev`.
