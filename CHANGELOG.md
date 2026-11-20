# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

First release: ESPHome on the Freematics ONE+ Model B in place of the stock telelogger firmware, reporting GPS tracks to Traccar in a compact, acknowledged format (about 12 KB per hour of driving), or live to Home Assistant. Vehicle data and SD card caching follow; the vehicle bus is only ever listened to.

### Added

- A board package for the Freematics ONE+ Model B (`packages/freematics-oneplus-b.yaml`): the cellular modem over LTE (SIM7070G by default; the power key is pressed until the module answers), the pins of the co-processor link, microSD and battery sense, and diagnostics in Home Assistant (cellular signal, heap, uptime, reset reason, battery voltage), each sent only on a real change.
- GPS from the dongle's internal receiver, read through the co-processor by the `freematics_link` component, which also resets the co-processor at boot, can report its version, the GNSS fix and the battery voltage it measures as sensors, and can set the clock from GPS when there's no network time.
- The clock is set from GPS, which costs no data; network time is only the fallback, synced every 12 hours (`sntp_interval`) instead of every 15 minutes, which over LTE would have cost about as much data a day as the tracking itself.
- Traccar configuration (`examples/car-traccar.yaml`, the `traccar_uplink` component): fixes go to Traccar as Teltonika GH3000 records over UDP, about 17 bytes each. A record every 10 s while moving and on every sharp turn, one when a trip starts and stops (after 3 minutes below 6 km/h), and a heartbeat every 15 minutes while parked, with battery voltage on starts, stops and heartbeats; all adjustable. Records are batched every minute, Traccar acknowledges each batch, and unacknowledged records are kept (up to 300) and sent again, so coverage gaps lose nothing. A Traccar device that used the Freematics protocol keeps its identifier.
- Home Assistant configuration (`examples/car-home-assistant.yaml`): the native API dialled from the device (Home Assistant 2026.10 or later) through a WireGuard tunnel, with latitude, longitude, speed and diagnostics sent only on real changes, and a template device tracker in Home Assistant (docs/home-assistant.md). Uses far more data than the Traccar configuration (docs/configurations.md).
- The modem's serial pins are substitutions (`modem_rx_pin`, `modem_tx_pin`), for dongles that wire it differently.
- Wi-Fi first: while a known Wi-Fi network is connected LTE is switched off, and it comes back when Wi-Fi drops.
- Opening the device's logs shows how it's doing right away: uptime and why it last restarted, the co-processor's version, GNSS replies and fix, and how many records Traccar has acknowledged and when it last answered.
- Nothing is sent to the vehicle bus: only commands the co-processor answers itself.
- The example configs are complete on their own: paste one into ESPHome Device Builder, set a few substitutions and secrets, and build; the packages and the component come from GitHub.
- The Traccar server settings the configuration needs (docs/configurations.md).
- `scripts/traccar_check.py`: checks from any computer that a Traccar server answers the dongle's packets (port forwarding, device ID).
- A bench probe for the cellular module (`examples/bench-modem-probe.yaml`): presses the power key and sends `AT` as Freematics' own library does, and logs everything the module answers, without ESPHome's modem component and without using data.
- The Traccar configuration shows its logs in ESPHome Device Builder over Wi-Fi, next to over-the-air updates, without connecting to Home Assistant or using cellular data. It needs a `freematics_api_key` secret.
- Over-the-air updates are encrypted with the device's API key; the separate OTA password secret is gone.

### Internal

- The Teltonika encoder is checked against a real Traccar server (`tests/test_traccar_server.py`, run when `TRACCAR_UDP` is set).
- Components follow ESPHome's own rules for components (clang-format, clang-tidy, no memory allocation while running, a component test config), checked in CI.
- Repository workflow: `dev`/`main` branches, changelog checks, `esphome config` and `esphome compile` of the example in CI, test builds as pre-release tags, Dependabot.

[Unreleased]: https://github.com/raetha/esphome-freematics/commits/dev
