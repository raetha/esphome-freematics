# ESPHome for the Freematics ONE+ Model B

Run [ESPHome](https://esphome.io) on the [Freematics ONE+ Model B](https://freematics.com/pages/products/freematics-one-plus) OBD-II dongle instead of the stock telelogger firmware, and report GPS tracks with as little cellular data as possible: to Traccar in a compact, acknowledged format (about 12 KB per hour of driving), or live to Home Assistant. The vehicle bus is only ever listened to, never written to.

**Status: early development.** Not released yet. What works and what's next: [docs/plan.md](https://github.com/raetha/esphome-freematics/blob/main/docs/plan.md).

## What you need

- A Freematics ONE+ Model B with a cellular module (SIM7070G or SIM7600) and a data SIM.
- ESPHome 2026.11 or later (the `modem` component).
- Either a Traccar server with UDP port 5027 (Teltonika) open, or Home Assistant 2026.10 or later with a WireGuard server at home. [docs/configurations.md](https://github.com/raetha/esphome-freematics/blob/main/docs/configurations.md) compares the two, including data use.

## Install

The examples are complete device configs that fetch everything else from this repository, so nothing needs copying into your ESPHome folder. With ESPHome Device Builder (including the Home Assistant app):

1. Back up the stock firmware if you may want to go back: `esptool.py --port <port> read_flash 0 0x1000000 stock.bin` on a computer with the dongle on USB (restore with `write_flash 0 stock.bin`).
2. **New device**, then **Edit** it and replace everything with [examples/car-traccar.yaml](https://github.com/raetha/esphome-freematics/blob/main/examples/car-traccar.yaml) or [examples/car-home-assistant.yaml](https://github.com/raetha/esphome-freematics/blob/main/examples/car-home-assistant.yaml) ([which one](https://github.com/raetha/esphome-freematics/blob/main/docs/configurations.md)).
3. Change the substitutions at the top: the name, the APN, and for Traccar the server and the device's identifier in Traccar. Optional packages (the SD card, sleep while parked) are commented out under `packages:`; uncomment the ones you want.
4. Under **Secrets**, add the secrets the example lists at its top (Wi-Fi and an API key, which also encrypts over-the-air updates; for Home Assistant also the WireGuard keys). [examples/secrets.example.yaml](https://github.com/raetha/esphome-freematics/blob/main/examples/secrets.example.yaml) shows their form.
5. **Install**, with the dongle plugged into the computer you're browsing from: **Plug into this computer**. Later updates go over the air while the car is on your Wi-Fi.

For Home Assistant, add the device to Home Assistant once while it's on your Wi-Fi.

Each build fetches the packages again (`refresh: 0s`), so a new build picks up changes here. Settings in the packages (update intervals, thresholds) are substitutions: set them in the device config's `substitutions:` to override them.

## Test builds

Changes on `dev` to what a device config pulls in are published as pre-releases tagged `<version>-dev.<n>`. To try one, set the package's `ref:` and the external component's `@...` to that tag (or to `dev` for the latest) and install. To go back, set them to `main` (the latest release) and install again. Until the first release, the examples use `dev`.

## Development

See [docs/development.md](https://github.com/raetha/esphome-freematics/blob/main/docs/development.md) and [CONTRIBUTING.md](https://github.com/raetha/esphome-freematics/blob/main/CONTRIBUTING.md).

## Licence

MIT. Pin assignments and co-processor commands come from Freematics' published schematic and their BSD-licensed [Arduino library](https://github.com/stanleyhuangyc/Freematics).
