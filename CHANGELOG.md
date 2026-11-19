# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

First release: ESPHome on the Freematics ONE+ Model B in place of the stock telelogger firmware, with Home Assistant as the backend and cellular data kept to a minimum. This first cut proves the cellular link and live GPS; offline caching and OBD data follow.

### Added

- A board package for the Freematics ONE+ Model B (`packages/freematics-oneplus-b.yaml`): the cellular modem over LTE (SIM7070G by default; the power key is pressed until the module answers), the pins of the co-processor link, microSD and battery sense, and diagnostics in Home Assistant (cellular signal, heap, uptime, reset reason, battery voltage), each sent only on a real change.
- Live GPS from the dongle's internal receiver (read through the co-processor) as latitude, longitude, speed, satellites and fix entities, sent only when the car has moved about 10 m; a template device tracker in Home Assistant turns them into a tracker (docs/home-assistant.md).
- The native API dials Home Assistant from the device (Home Assistant 2026.10 or later), so it keeps working from LTE behind the carrier's NAT, with state updates batched to save data.
- An example device config with Wi-Fi preferred over LTE and a WireGuard tunnel home, so nothing at home is exposed to the internet.
- Buttons that send diagnostic commands to the modem and the co-processor, with the replies in the device log.

### Internal

- Repository workflow: `dev`/`main` branches, changelog checks, `esphome config` and `esphome compile` of the example in CI (run by hand while the repository is private), test builds as pre-release tags, Dependabot.

[Unreleased]: https://github.com/raetha/esphome-freematics/commits/dev
