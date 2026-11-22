# Backlog

Ideas evaluated and set aside for now, and why. Check here before proposing something.

| Idea | Why not now |
| --- | --- |
| Traccar's Freematics protocol (UDP 5170, what the stock firmware used) | Text, about 60 bytes per fix, and Traccar doesn't acknowledge position packets, so lost packets go unnoticed. GH3000 is about 17 bytes and acknowledged. |
| Traccar's OsmAnd protocol | HTTP: hundreds of bytes of headers per fix. |
| MQTT configuration | Not built: more data than Traccar, needs a broker, and ESPHome republishes discovery on every reconnect. Could be added as an example if wanted. |
| Back-dated statistics in the Home Assistant integration | Home Assistant keeps no back-dated states, but it imports long-term statistics with past times: values from late batches (battery voltage, distance per trip) could go in as statistics. Worth it once the integration has run a while (the integration itself was built 2026-11-21, raetha/ha-freematics). |
| Active OBD requests | Not until passive reading is understood; the stock firmware's requests are the likely cause of the dash alerts. |
| A `link_manager` component switching Wi-Fi and LTE | `packages/wifi-first.yaml` does it with two automations. |
| Adding `modem` to ESPHome's `network: priority` upstream | Only worth it if phase 1 shows route handover problems. |
| MQTT to Home Assistant | Replaced by the native API dialled from the device: smaller while driving, no broker, and discovery republishes tens of KB on every reconnect (networking.md). |
| Exposing anything at home to the internet, or a public broker | Rejected: WireGuard keeps everything private. |
| Writing back-dated location states into Home Assistant's recorder database | Unsupported and can break on schema upgrades; an open question in plan.md. |
| IMU (motion wake-up, harsh braking), deep sleep on low battery voltage | After OBD works. |
| Ideas from Car-Assistant (ShonP40's ESPHome car tracker on a LilyGO T-SIM7600, Home Assistant over WireGuard or MQTT) | For the Home Assistant configuration: a Bluetooth iBeacon so the HA app shows which phones are in the car; a location-accuracy entity for the template device tracker; buttons to restart the modem and the ESP32; restarting the modem when GPS stops recovering. Not data-saving, and nothing from the vehicle bus. |
| Support every Freematics ONE+ model (A, B, H) and, where practical, other Freematics devices | The owner's wish (2026-11-20), once the Model B is stable: more people could use the project, though only the Model B can be tested here. What differs, from Freematics' library (`FreematicsPlus.h`/`.cpp`): the co-processor link (UART on GPIO13/14 from version 1.4; SPI with chip select on GPIO2 on older boards), the cellular module's serial pins (GPIO32/33 for co-processor version 1.3), an external GNSS on some models (its pins and power pin vary by version: `gpsBeginExt`), and the motion sensor (MPU-9250, ICM-20948 or ICM-42627). Approach: detect at run time what can be (the co-processor's version, the motion sensor's ID: both already are), substitutions for what ESPHome fixes at build time (the modem's pins), a board package per model where the wiring differs, and testers with those models before claiming support. For users: a ready example config for each model (and each uplink) that needs its own values, with that board's pins and settings already filled in, so they pick the example for their device and only set their own server, names and secrets. |
