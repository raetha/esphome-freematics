# ESPHome for the Freematics ONE+ Model B

Run [ESPHome](https://esphome.io) on the [Freematics ONE+ Model B](https://freematics.com/pages/products/freematics-one-plus) OBD-II dongle instead of the stock telelogger firmware, and report GPS tracks with as little cellular data as possible: to Traccar in a compact, acknowledged format (about 12 KB per hour of driving), or live to Home Assistant. The vehicle bus is only ever listened to, never written to.

**Status: early development.** Not released yet. What works and what's next: [docs/plan.md](https://github.com/raetha/esphome-freematics/blob/main/docs/plan.md).

## What you need

- A Freematics ONE+ Model B with a cellular module (SIM7070G or SIM7600) and a data SIM.
- ESPHome 2026.11 or later (the `modem` component).
- Either a Traccar server with UDP port 5027 (Teltonika) open, or Home Assistant 2026.10 or later with a WireGuard server at home. [docs/configurations.md](https://github.com/raetha/esphome-freematics/blob/main/docs/configurations.md) compares the two, including data use.

## Install

1. Back up the stock firmware first, so you can go back: `esptool.py --port <port> read_flash 0 0x1000000 stock.bin` (restore with `write_flash 0 stock.bin`).
2. Copy [examples/car-traccar.yaml](https://github.com/raetha/esphome-freematics/blob/main/examples/car-traccar.yaml) or [examples/car-home-assistant.yaml](https://github.com/raetha/esphome-freematics/blob/main/examples/car-home-assistant.yaml) into your ESPHome config folder, rename it per car, and set its substitutions (name, APN; for Traccar, the server and the device's identifier).
3. Add the keys from [examples/secrets.example.yaml](https://github.com/raetha/esphome-freematics/blob/main/examples/secrets.example.yaml) to your `secrets.yaml` with real values.
4. Point its `packages:` (and, for Traccar, `external_components:`) at this repository, as the comment at the top of the example shows, or at local copies of `packages/` and `components/`.
5. Flash the first time over USB (for Home Assistant, then add the device while it's on your Wi-Fi). Later updates go over the air, best on Wi-Fi.

This repository is private, so ESPHome needs credentials to fetch the package from it: either a local copy, or a `url` with a GitHub token (`https://<user>:<token>@github.com/raetha/esphome-freematics`).

## Test builds

Changes on `dev` to what a device config pulls in are published as pre-releases tagged `<version>-dev.<n>`. To try one, set the package's `ref:` to that tag (or to `dev` for the latest) and install. To go back, set `ref:` to the last release tag and install again.

## Development

See [docs/development.md](https://github.com/raetha/esphome-freematics/blob/main/docs/development.md) and [CONTRIBUTING.md](https://github.com/raetha/esphome-freematics/blob/main/CONTRIBUTING.md).

## Licence

MIT. Pin assignments and co-processor commands come from Freematics' published schematic and their BSD-licensed [Arduino library](https://github.com/stanleyhuangyc/Freematics).
