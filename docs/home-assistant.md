# Home Assistant side

What Home Assistant needs for the Home Assistant configuration ([configurations.md](configurations.md)). With the Traccar configuration, Home Assistant sees the cars through its Traccar Server integration instead, and nothing here applies.

## Adding the dongle

Home Assistant 2026.10 or later. With the dongle on home Wi-Fi, add it as usual (Settings → Devices & services → ESPHome, or the discovered device) with its API encryption key. From then on the dongle dials Home Assistant itself (`api: outgoing_connection:`), from Wi-Fi or over LTE through the WireGuard tunnel ([networking.md](networking.md)). Nothing else to configure on the HA side.

## Device tracker

ESPHome has no device tracker entity, so the dongle sends latitude and longitude as sensors and a template device tracker (Home Assistant 2026.6 and later) turns them into a tracker for the map, zones and people. Either add one as a helper (Settings → Devices & services → Helpers → Create helper → Template → Device tracker) with the two sensors, or in YAML:

```yaml
template:
  - device_tracker:
      - name: Car 1
        unique_id: freematics_car1_tracker
        latitude: "{{ states('sensor.car_1_gnss_latitude') | float(None) }}"
        longitude: "{{ states('sensor.car_1_gnss_longitude') | float(None) }}"
        location_accuracy: 10
```

Check the sensor entity IDs in your install. While the dongle is offline the sensors go unavailable and the tracker keeps no position: this configuration doesn't replay.
