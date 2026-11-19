# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

First release: ESPHome on the Freematics ONE+ Model B in place of the stock telelogger firmware, with Home Assistant as the backend. This first cut proves the cellular link and live GPS; offline caching and OBD data follow.

### Added

- A board package for the Freematics ONE+ Model B (`packages/freematics-oneplus-b.yaml`): the cellular modem over LTE, the pins of the co-processor link, microSD and battery sense, and diagnostics in Home Assistant (cellular signal, network type, heap, uptime, reset reason, battery voltage).
- Live GPS from the dongle's internal receiver (read through the co-processor), as GNSS entities and a Home Assistant device tracker announced over MQTT.
- An example device config with Wi-Fi preferred over LTE, the native API and OTA on Wi-Fi, and MQTT over a WireGuard tunnel home so the broker stays private.
- Buttons that send diagnostic commands to the modem and the co-processor, with the replies in the device log.

### Internal

- Repository workflow: `dev`/`main` branches, changelog checks, `esphome config` and `esphome compile` of the example in CI, test builds as pre-release tags, Dependabot.

[Unreleased]: https://github.com/raetha/esphome-freematics/commits/dev
