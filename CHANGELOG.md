# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

First release: ESPHome on the Freematics ONE+ Model B in place of the stock telelogger firmware, reporting GPS tracks to Traccar in a compact, acknowledged format (about 12 KB per hour of driving), or live to Home Assistant. Vehicle data and SD card caching follow; the vehicle bus is only ever listened to.

### Added

- A board package for the Freematics ONE+ Model B (`packages/freematics-oneplus-b.yaml`): the cellular modem over LTE (SIM7070G by default; the power key is pressed until the module answers), the pins of the co-processor link, microSD and battery sense, and diagnostics in Home Assistant (cellular signal, heap, uptime, reset reason, battery voltage), each sent only on a real change.
- GPS from the dongle's internal receiver, read through the co-processor by the `freematics_link` component, which also resets the co-processor at boot, can report its version, the GNSS fix and the battery voltage it measures as sensors, and can set the clock from GPS when there's no network time. Bytes the co-processor sends while it starts are discarded, so they're no longer reported as a broken reply.
- The clock is set from GPS, which costs no data; network time is only the fallback, synced every 12 hours (`sntp_interval`) instead of every 15 minutes, which over LTE would have cost about as much data a day as the tracking itself.
- Traccar configuration (`examples/car-traccar.yaml`, the `traccar_uplink` component): fixes go to Traccar as Teltonika GH3000 records over UDP, about 17 bytes each. A record every 10 s while moving and on every sharp turn, one when a trip starts and stops (after 3 minutes below 6 km/h), and a heartbeat every 15 minutes while parked, with battery voltage on starts, stops and heartbeats; all adjustable. Records are batched every minute, Traccar acknowledges each batch, and unacknowledged records are kept (up to 300) and sent again, so coverage gaps lose nothing. A Traccar device that used the Freematics protocol keeps its identifier.
- Home Assistant configuration (`examples/car-home-assistant.yaml`): the native API dialled from the device (Home Assistant 2026.10 or later) through a WireGuard tunnel, with latitude, longitude, speed and diagnostics sent only on real changes, and a template device tracker in Home Assistant (docs/home-assistant.md). Uses far more data than the Traccar configuration (docs/configurations.md).
- The modem's serial pins are substitutions (`modem_rx_pin`, `modem_tx_pin`), for dongles that wire it differently.
- LTE works with the SIM7070: once the modem answers, it's switched to full functionality (`AT+CFUN=1`), because ESPHome's modem component leaves the SIM7070's SIM switched off after setting the APN.
- The cellular module is restarted the first time it answers after each start of the device (`AT+CFUN=1,1`), since it keeps power across updates and crashes and can be left stuck in its last session; `modem_restart_on_boot: "false"` turns this off.
- Beeps, like the stock firmware: a short one when Wi-Fi or cellular comes up (as the stock firmware does when it reaches its server), and a longer one once the firmware has started. `beep_on_network: "false"` and `beep_on_boot: "false"` turn them off.
- Wi-Fi first: while a known Wi-Fi network is connected LTE is switched off, and it comes back when Wi-Fi drops. For bench tests, `lte_on_wifi: "true"` keeps LTE on beside Wi-Fi so its logs reach Device Builder. While the car is moving with no Wi-Fi connected, the device stops searching for Wi-Fi (less radio noise beside the GPS antenna) and starts again once the car has been still for 30 seconds or has no GPS fix; a hotspot connected before driving off is kept (`wifi_pause_speed`, `wifi_resume_after`).
- The cellular module is started the way it reliably answers: a 200 ms power-key press, then 8 seconds for it to start before ESPHome's modem component talks to it (talking to it while it started left it silent until the power was cut). If it doesn't answer, every third try switches it off first with a long press, so a stuck module recovers without unplugging the dongle. Its serial speed is fixed at 115200 at its first answer. Adjustable with `modem_pwrkey_press`, `modem_pwrkey_tries`, `modem_boot_wait` and `modem_pwrkey_wait`.
- The Traccar uplink logs any reply that isn't the acknowledgement it waits for.
- Opening the device's logs shows how it's doing right away: uptime and why it last restarted, the co-processor's version, GNSS replies and fix, and how many records Traccar has acknowledged and when it last answered.
- Nothing is sent to the vehicle bus: only commands the co-processor answers itself.
- The example configs are complete on their own: paste one into ESPHome Device Builder, set a few substitutions and secrets, and build; the packages and the component come from GitHub.
- The Traccar server settings the configuration needs (docs/configurations.md).
- Optional SD card packages: `sd-card.yaml` mounts the card; `sd-traccar.yaml` keeps every record on it until Traccar acknowledges it (up to 50,000, about 140 hours of driving), so a restart or a long time without coverage loses nothing; `sd-logs.yaml` writes the device's log to the card (two files of 1 MB at most), for finding out what happened away from Wi-Fi. The `sd_card`, `file_queue` and `file_logger` components behind them work on any ESP32 board. Writing to the card no longer holds up the device: the log is written by a task of its own, and the stored records are synced to the card every 30 seconds (`sync_interval`) instead of on every change, in a file made full size when it's created.
- `scripts/traccar_check.py`: checks from any computer that a Traccar server answers the dongle's packets (port forwarding, device ID). `--protocol freematics` checks the Freematics port instead.
- The Traccar uplink can use Traccar's Freematics protocol, the one the stock firmware used (`traccar_protocol: freematics`, `traccar_port: "5170"`), to check a server with a protocol known to work: no acknowledgements and about four times the data, so not for everyday use.
- A bench probe for the cellular module (`examples/bench-modem-probe.yaml`): presses the power key and sends `AT` as Freematics' own library does, and logs everything the module answers, without ESPHome's modem component and without using data.
- A bench test of ESPHome's modem component (`examples/bench-modem-esphome.yaml`): the board package's power-key presses and modem settings with only the co-processor link beside them, to find out what in the full configuration keeps the modem from answering.
- The Traccar configuration shows its logs in ESPHome Device Builder over Wi-Fi, next to over-the-air updates, without connecting to Home Assistant or using cellular data. It needs a `freematics_api_key` secret.
- Over-the-air updates are encrypted with the device's API key; the separate OTA password secret is gone.
- Sleep while parked (`packages/standby.yaml`, optional in the Traccar configuration): once the car has been off and still for 10 minutes, the device sends what it has, switches the cellular module, GPS and co-processor off and sleeps, waking for a moment every minute to check the 12 V voltage. It stays awake again as soon as the car is on (its 12 V system charging, 13.2 V or more), and wakes fully every 6 hours to report one record to Traccar. It never sleeps when it isn't on a car (powered over USB). All adjustable (`standby_voltage`, `standby_after`, `standby_check`, `standby_heartbeat`).
- The SD card's record queue and log are written out before a planned restart or sleep.
- A bench test that listens to the OBD port for one minute without sending anything to the car (`examples/bench-obd-listen.yaml`): the first step towards vehicle data, kept out of the configurations.

### Internal

- A simulated day through the tracking filter and encoder checks the data estimates (`tests/test_simulation.py`).
- CI also builds each example with its optional packages switched on.
- The Teltonika encoder is checked against a real Traccar server (`tests/test_traccar_server.py`, run when `TRACCAR_UDP` is set).
- Components follow ESPHome's own rules for components (clang-format, clang-tidy, no memory allocation while running, a component test config), checked in CI.
- Repository workflow: `dev`/`main` branches, changelog checks, `esphome config` and `esphome compile` of the example in CI, test builds as pre-release tags, Dependabot.

[Unreleased]: https://github.com/raetha/esphome-freematics/commits/dev
