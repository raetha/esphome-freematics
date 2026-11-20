# Backlog

Ideas evaluated and set aside for now, and why. Check here before proposing something.

| Idea | Why not now |
| --- | --- |
| Traccar's Freematics protocol (UDP 5170, what the stock firmware used) | Text, about 60 bytes per fix, and Traccar doesn't acknowledge position packets, so lost packets go unnoticed. GH3000 is about 17 bytes and acknowledged. |
| Traccar's OsmAnd protocol | HTTP: hundreds of bytes of headers per fix. |
| MQTT configuration | Not built: more data than Traccar, needs a broker, and ESPHome republishes discovery on every reconnect. Could be added as an example if wanted. |
| A compact uplink straight into a Home Assistant custom integration, through a webhook | The owner's idea (2026-11-20): a small integration with a webhook, reachable through Nabu Casa's cloud without exposing HA, taking batches in the GH3000 or a similar binary layout. The catch is the transport: a webhook is HTTPS, and a TLS handshake (a few KB) plus HTTP headers (a few hundred bytes) dwarf the 17 bytes a fix, so it only pays with large batches (several minutes of driving per request) and TLS session resumption. Probably still several times Traccar's UDP data, far below the native API's. Worth a design once Traccar works end to end. |
| Active OBD requests | Not until passive reading is understood; the stock firmware's requests are the likely cause of the dash alerts. |
| A `link_manager` component switching Wi-Fi and LTE | `packages/wifi-first.yaml` does it with two automations. |
| Adding `modem` to ESPHome's `network: priority` upstream | Only worth it if phase 1 shows route handover problems. |
| MQTT to Home Assistant | Replaced by the native API dialled from the device: smaller while driving, no broker, and discovery republishes tens of KB on every reconnect (networking.md). |
| Exposing anything at home to the internet, or a public broker | Rejected: WireGuard keeps everything private. |
| Writing back-dated location states into Home Assistant's recorder database | Unsupported and can break on schema upgrades; an open question in plan.md. |
| IMU (motion wake-up, harsh braking), deep sleep on low battery voltage | After OBD works. |
| Ideas from Car-Assistant (ShonP40's ESPHome car tracker on a LilyGO T-SIM7600, Home Assistant over WireGuard or MQTT) | For the Home Assistant configuration: a Bluetooth iBeacon so the HA app shows which phones are in the car; a location-accuracy entity for the template device tracker; buttons to restart the modem and the ESP32; restarting the modem when GPS stops recovering. Not data-saving, and nothing from the vehicle bus. |
