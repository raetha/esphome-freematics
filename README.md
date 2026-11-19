# ESPHome for the Freematics ONE+ Model B

Run [ESPHome](https://esphome.io) on the [Freematics ONE+ Model B](https://freematics.com/pages/products/freematics-one-plus) OBD-II dongle instead of the stock telelogger firmware, and report to Home Assistant: GPS as a device tracker, cellular and device diagnostics, and (coming) vehicle data and offline caching with replay.

**Status: early development.** Not released yet. What works and what's next: [docs/plan.md](https://github.com/raetha/esphome-freematics/blob/main/docs/plan.md).

## What you need

- A Freematics ONE+ Model B with a cellular module (SIM7600 or SIM7070) and a data SIM.
- ESPHome 2026.11 or later (the `modem` component).
- Home Assistant with an MQTT broker, and a WireGuard server at home so the dongle can reach the broker over LTE without exposing it ([docs/networking.md](https://github.com/raetha/esphome-freematics/blob/main/docs/networking.md)).

## Install

1. Back up the stock firmware first, so you can go back: `esptool.py --port <port> read_flash 0 0x1000000 stock.bin` (restore with `write_flash 0 stock.bin`).
2. Copy [examples/car.yaml](https://github.com/raetha/esphome-freematics/blob/main/examples/car.yaml) into your ESPHome config folder, rename it per car, and set its substitutions (name, APN, modem model).
3. Add the keys from [examples/secrets.example.yaml](https://github.com/raetha/esphome-freematics/blob/main/examples/secrets.example.yaml) to your `secrets.yaml` with real values.
4. Point its `packages:` at this repository (the comment at the top of the example shows how) or at a local copy of `packages/freematics-oneplus-b.yaml`.
5. Flash the first time over USB; later updates go over the air while the car is on your Wi-Fi.

This repository is private, so ESPHome needs credentials to fetch the package from it: either a local copy, or a `url` with a GitHub token (`https://<user>:<token>@github.com/raetha/esphome-freematics`).

## Test builds

Every change on `dev` to what a device config pulls in is published as a pre-release tagged `<version>-dev.<n>`. To try one, set the package's `ref:` to that tag (or to `dev` for the latest) and install. To go back, set `ref:` to the last release tag and install again.

## Development

See [docs/development.md](https://github.com/raetha/esphome-freematics/blob/main/docs/development.md) and [CONTRIBUTING.md](https://github.com/raetha/esphome-freematics/blob/main/CONTRIBUTING.md).

## Licence

MIT. Pin assignments and co-processor commands come from Freematics' published schematic and their BSD-licensed [Arduino library](https://github.com/stanleyhuangyc/Freematics).
