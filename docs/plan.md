# Plan and status

Where the project stands, what's decided, and what comes next. Keep this current with each step; the chat that produced it isn't the record.

## Status

**Phase 0 (cellular and diagnostics) is ready to flash** but not yet tried on hardware (as of 2026-11-19). The board package and example config validate with ESPHome 2026.11.0; CI compiles them.

## Goal and requirements

Replace the Freematics telelogger firmware on two ONE+ Model B dongles with ESPHome. The stock firmware loses data and has triggered warnings on the Volvo and the Subaru.

| # | Requirement | Priority |
| --- | --- | --- |
| R1 | Cache every sample on the device while offline, replay it later with the original timestamps | Must |
| R2 | Deliver to Home Assistant (Traccar later, maybe), with replayed values stored at their real time | Must |
| R3 | GPS position as a Home Assistant device_tracker | Must |
| R4 | Wi-Fi preferred over cellular, several known networks | Must |
| R5 | Read the vehicle without upsetting it: no bus flooding, no dash alerts | Must |
| R6 | VIN, battery voltage, fuel level and the PIDs the telelogger reported | Should |
| R7 | IMU data | Could |

## Decisions

- **Backend: Home Assistant only**, over MQTT. Traccar is on the backlog. (2026-11-19)
- **Private transport: WireGuard** from each dongle to home, so the MQTT broker isn't exposed. Only the broker's address goes through the tunnel. See [networking.md](networking.md). (2026-11-19)
- **Replayed sensor values become hourly long-term statistics** in HA, through a small HA integration in its own repo (HACS: one integration per repo). (2026-11-19)
- **One repo for ESPHome work** (this one, on `dev`); whether generic components move elsewhere is decided later. (2026-11-19)
- **Build one piece at a time**, YAML stopgap first, then a component that replaces it.

## Phases

1. **Phase 0: cellular and diagnostics** (YAML only, ready). Back up the stock flash (`esptool.py --port <port> read_flash 0 0x1000000 stock-car1.bin`), flash `examples/car.yaml` over USB, confirm PPP comes up and entities appear in HA over MQTT through WireGuard. Press the co-processor buttons (`ATI`, `ATRV`, `ATGPSON`) and record the replies in [hardware.md](hardware.md). *Exit:* a stable LTE link for a whole drive, with signal, network type, heap and battery voltage in HA.
2. **Phase 1: GPS device_tracker and Wi-Fi preference** (YAML, already in the package). The car shows on HA's map while driving; traffic moves to Wi-Fi at home and back to LTE when leaving, without a reboot. Check WireGuard from inside the home network (see [networking.md](networking.md)).
3. **Phase 2: `freematics_link` component.** Moves the co-processor UART, GNSS parsing and a GNSS time source into C++; adds `ATRV` and VIN. *Exit:* the same behavior as phase 1 without the lambda and the buttons.
4. **Phase 3: store and forward.** `telemetry_queue` (microSD ring buffer, deleted only when acknowledged) and `telemetry_uplink` (replays over MQTT with `ts` and `seq`), plus the HA integration that imports hourly statistics. *Exit:* three forced outages and two reboots with nothing lost, and the outage hours filled in HA's statistics graphs.
5. **Phase 4: OBD PIDs.** Fuel level first, then a short list, a week per car watching for dash alerts. *Exit:* a week on both cars with no warnings; retire the stock firmware.

## Components to build (one at a time)

| Component | Does | Replaces |
| --- | --- | --- |
| `freematics_link` | Owns the co-processor UART; GNSS sensors and time; later OBD PIDs, VIN, battery voltage, with the conservative polling rules | The package's `uart:` lambda and probe buttons |
| `telemetry_queue` | Durable queue on microSD, records with sequence number and UTC time, deleted on acknowledgement | nothing (new) |
| `telemetry_uplink` | Drains the queue oldest-first over MQTT (QoS 1, waits for PUBACK) | nothing (new) |
| `link_manager` | Probably not needed if Wi-Fi wins the route on its own (phase 1); at most, turn the modem off on home Wi-Fi to save data | — |

## Open questions

- [ ] Which cellular module is in each dongle (SIM7600 or SIM7070), and which carrier and APN?
- [ ] The stock telelogger's serial boot log from one dongle (`TYPE:` and `GNSS:OK(I)` or `OK(E)` lines) to confirm the co-processor version and GNSS path.
- [ ] Which OBD values matter beyond fuel level?
- [ ] Location history for replayed points: HA can't back-date states. Live map points plus GPX files on the SD card; an unsupported write into HA's recorder database from the HA integration; or Traccar later.
- [ ] WireGuard server: where it runs, and whether its endpoint works from inside the home network (NAT loopback or split DNS).

## Log

- 2026-11-19: Plan drafted; pins and co-processor commands taken from the ONE+ r14 schematic and the FreematicsPlus library; phase 0 config written and validated; repository set up on `dev`.
