# Backlog

Ideas evaluated and set aside for now, and why. Check here before proposing something.

| Idea | Why not now |
| --- | --- |
| Traccar as a backend (OsmAnd HTTP with per-point timestamps) | The owner is moving off Traccar; Home Assistant is the only backend for now. Traccar is the easiest way to get replayed location history, so it may come back. |
| A `link_manager` component switching Wi-Fi and LTE | ESP-IDF's route priorities may already put Wi-Fi first; decide after phase 1 tests it. |
| Adding `modem` to ESPHome's `network: priority` upstream | Only worth it if phase 1 shows route handover problems. |
| MQTT to Home Assistant | Replaced by the native API dialled from the device: smaller while driving, no broker, and discovery republishes tens of KB on every reconnect (networking.md). |
| Exposing anything at home to the internet, or a public broker | Rejected: WireGuard keeps everything private. |
| Writing back-dated location states into Home Assistant's recorder database | Unsupported and can break on schema upgrades; an open question in plan.md. |
| IMU (motion wake-up, harsh braking), deep sleep on low battery voltage | After OBD works. |
