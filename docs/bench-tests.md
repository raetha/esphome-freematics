# Bench tests

One thing per test, in order: each one either passes or narrows down the next. For each, the config change, what to look for, and what to send back. Logs come from Device Builder over Wi-Fi (open the logs, wait two minutes, save them); where a test needs logs from away from Wi-Fi, the SD card holds them (test 6).

Results so far are in [plan.md](plan.md) → Log.

## 0. Bring the pasted config up to date

In Device Builder, edit the device and make these changes (everything else stays):

```yaml
ota:
  - platform: esphome
    encryption:                       # uses the API key; the OTA password secret can go

external_components:
  - source: github://raetha/esphome-freematics@dev
    refresh: 0s
    components: [freematics_link, traccar_uplink, sd_card, file_queue, file_logger]
```

Under `packages: freematics: files:`, add these lines (commented out for now; later tests switch them on):

```yaml
      # - packages/sd-card.yaml
      # - packages/sd-traccar.yaml
      # - packages/sd-logs.yaml
```

Install. In the log's opening lines, check: `Co-processor version: 1.4`, a GNSS fix, `Reset Reason`.

## 1. Can Traccar be reached from outside?

No firmware involved. Traccar ignores devices it doesn't know, and Cloudflare's proxy (orange cloud) and Cloudflare Tunnel carry no UDP, so:

1. In Cloudflare, the name the dongle uses must be **DNS only** (grey cloud) and point at the home address. If the web interface needs the proxy, give the dongle its own DNS-only name, for example `traccar-udp.<domain>`, and put that in `traccar_host`.
2. From a computer on a phone's hotspot (outside the home network): `python3 scripts/traccar_check.py <traccar_host> test` (the script needs only Python 3).

**Pass:** "Traccar acknowledged 1 record(s)". **Send back:** the script's output, and whether the name was proxied.

## 2. Does the device get answers from Traccar over Wi-Fi?

The normal config, after test 1 passes (with the new `traccar_host` if it changed).

**Look for:** `Sent 1 records in ... bytes`, then within a second `Traccar acknowledged 1 records`. The opening log lines show `acknowledged since boot` and `Last answer from Traccar`. A `Reply that isn't the acknowledgement expected` warning means something answered, with its bytes in the line.

**Send back:** two minutes of log from boot. In Traccar, the device should show a position.

**If nothing answers**, check Traccar's **Settings → Protocols**: Teltonika on port 5027 with transport UDP or All (Traccar 6 keeps these in its database; the config file only sets them the first time). Then, to tell Traccar from the network, switch to the protocol the stock firmware used:

```yaml
  traccar_protocol: freematics
  traccar_port: "5170"
```

and look for `Traccar answered the login: the server is reachable`. From a computer, `python3 scripts/traccar_check.py <host> test --protocol freematics` does the same. Switch back once Teltonika answers: Freematics positions get no acknowledgement and use about four times the data.

## 3. Does the cellular module answer at all?

Paste [examples/bench-modem-probe.yaml](https://github.com/raetha/esphome-freematics/blob/dev/examples/bench-modem-probe.yaml) over the config (it uses the same name and secrets), install, and leave the logs open for three minutes. It presses the power key and sends `AT` the way Freematics' library does: tries 1 to 12 with a 200 ms press, 13 to 24 with 1 s, 25 to 36 with 1.5 s. Uses no cellular data.

**Look for:** lines starting `<<<` (the module talking) and "The module answered after N tries", followed by its answers to `ATI` (model), `AT+CPIN?` (SIM ready), `AT+CSQ` (signal), `AT+CPSI?` (network).

**Send back:** the whole log. Then paste the normal config back.

If nothing answers at all, the next step is hardware: run test 3 again with the dongle in the car (powered from the OBD port), and if still nothing, test 8.

## 3b. ESPHome's modem component, adding the board's pieces back

Done on 2026-11-20: on its own, ESPHome's modem component synced in 2 s, and once the SIM was powered back up (`AT+CFUN=1`, now in the board package) LTE came up in 9 s. With the co-processor link beside it, it still synced. The full config never hears the modem, so [examples/bench-modem-esphome.yaml](https://github.com/raetha/esphome-freematics/blob/dev/examples/bench-modem-esphome.yaml) now adds the buzzer and the battery voltage reading too.

1. Install it, then **unplug the dongle for 10 seconds** and plug it back in (installing over USB restarts the ESP32 but not the modem, which then starts in its last session). Leave the logs open for two minutes.
2. Install the same config again over Wi-Fi (a restart with the modem still powered, as after any update) and leave the logs open for two minutes.

**Look for:** a 200 ms beep at start; "Cellular: restarting the module (AT+CFUN=1,1)" at the first sync, then a second "modem synced" about 10 s later; "Cellular: PPP up", and whether "Failed to enter PPP. Resetting modem." follows (after step 2 especially); "Modem not responding at 115200 baud" means the buzzer or the voltage reading gets in the way.

**Send back:** both logs. Then paste the normal config back.

## 4. Does ESPHome's modem component bring LTE up?

Only after test 3 got answers. In the normal config, under `substitutions:` add:

```yaml
  lte_on_wifi: "true"           # LTE stays on beside Wi-Fi, so its logs reach Device Builder
  modem_pwrkey_press: 200ms     # the press that worked in test 3 (200ms, 1s or 1500ms)
  modem_pwrkey_tries: "10"
  modem_pwrkey_wait: 20s        # with a 1 s or longer press: give the module time to start before pressing again
```

**Look for:** `Cellular: PPP up` and the modem's own lines. Traffic still goes over Wi-Fi (it's preferred whenever both are up), so this uses almost no cellular data.

**Send back:** three minutes of log. Then remove the three lines.

## 5. Everything over LTE

After test 4. Take the device out of Wi-Fi range (or switch its Wi-Fi network off for a few minutes), with the SD card logging (test 6) so there's a log to read afterwards. After `lte_back_after` (30 s), LTE comes up and records go to Traccar over it.

**Look for:** new positions in Traccar while the device is off Wi-Fi. In the SD card's log: `Cellular: PPP up`, `Sent`, `Traccar acknowledged`.

**Send back:** `esphome.log` from the card, and Hologram's usage for the day.

## 6. The SD card

Needs a microSD card formatted FAT32 (any size; the files stay small). Switch the three package lines from test 0 on (remove the `#`):

```yaml
      - packages/sd-card.yaml
      - packages/sd-traccar.yaml
      - packages/sd-logs.yaml
```

**Look for** in the opening log lines: `SD card:` with the card's name and size; `File queue: ... Path: /sdcard/traccar.bin`; `Records kept on file until acknowledged (ready)`; `File logger: ... (open)`. Then restart the device (unplug it) while a record is waiting (right after a `Sent` line, before the acknowledgement): the next boot should log `records from before the restart to send`.

**Send back:** the opening log lines, and after a while, `esphome.log` from the card (the computer can read it directly).

## 7. A drive

With tests 2 to 6 passing: the dongle in the car on OBD power, with the SD packages on. Drive somewhere, park for a while, drive back.

**Look for:** in Traccar, the trip with its corners, a stop, heartbeats while parked; at home, LTE off once Wi-Fi is back. Afterwards, Hologram's usage (compare with [configurations.md](configurations.md): about 11.5 KB per hour of driving and 3.3 KB per 8 hours parked) and the card's log.

**Send back:** what Traccar showed, the data used, the card's log.

## 8. The stock firmware, for comparison (optional)

If the cellular module never answers in test 3: plug a computer into the other dongle (still on the stock firmware), open a serial monitor at 115200 baud, and power it up. The first lines show `TYPE:` (the co-processor version), `GNSS:` and the cellular module's start. That shows whether the same hardware's cellular works on that power, and how the stock firmware starts it.

**Send back:** the first minute of that log.
