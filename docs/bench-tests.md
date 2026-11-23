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
    components: [freematics_link, tracker_uplink, sd_card, file_queue, file_logger]
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

## 3b. ESPHome's modem component on its own (done)

Done on 2026-11-20 with a bench config since removed: on its own ESPHome's modem component reached the module, which showed the two problems the board package now handles (the SIM left off after the APN is set, and a module that goes silent if the component talks to it while it boots). Test 4 covers the full config.

## 4. Does ESPHome's modem component bring LTE up?

Only after test 3 got answers. In the normal config (the car config from this repository, with test build 0.1.0-dev.16 or later), under `substitutions:` add:

```yaml
  lte_on_wifi: "true"           # LTE stays on beside Wi-Fi, so its logs reach Device Builder
```

Then two starts, with the logs open over Wi-Fi in Device Builder ("Wirelessly"; they reconnect a few seconds after a start):

1. Cold: unplug the dongle for 10 seconds and plug it back in.
2. Warm: install the same config again over Wi-Fi.

**Look for:** "Cellular: pressing PWRKEY", the modem component starting 8 s later, "restarting the module (AT+CFUN=1,1)" at its first answer only after an update or a restart that kept the module on (not after plugging in or a wake), a second sync about 10 s later in that case, then `Cellular: PPP up`, after both starts. "holding PWRKEY 2.5 s" means the module didn't answer and is being switched off and on again. Traffic still goes over Wi-Fi (it's preferred whenever both are up), so this uses almost no cellular data.

**Send back:** both logs. Then remove the line.

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

## 7b. Sleep while parked

With `packages/standby.yaml` uncommented in the car config, in the car (on USB power it sleeps once still for 10 minutes too; `standby_on_usb: "false"` keeps a bench device awake). Drive, park, and leave it: 3 minutes after stopping Traccar gets the stop record; 10 minutes after that the device logs "Standby: parked; sending what's waiting, then sleeping" and "Standby: sleeping", and goes quiet (the logs disconnect). Start the car: within a minute it wakes ("Woke: 14.x V, the car is on"), presses the modem's power key and tracks again. Left parked, it reports one record every 6 hours.

**Look for:** the standby lines in the SD card's log, including the motion sensor's "Model: ICM-42627" in the config dump at start and "Motion wake set (150 mg)" before sleeping; in Traccar, the 6-hourly records while parked and the first record of the next trip. To try motion wake: once it's asleep, open and close a door, or rock the car: within a minute the log says "Woke: ... the car moved".

**Send back:** the SD card's log after a night or a weekend, and the 12 V battery's behaviour if anything seems off. To update it while it sleeps, start the car (or unplug and plug it back in): it stays awake for 10 minutes.

## 7d. Impact, temperature and GNSS restart (on the bench)

On USB power with the logs open, after updating (the trips now come from the board package's trip tracker, so check a short drive still makes one trip in each server).

- **Impact**: set `impact_threshold: 1.5g` for the test, then give the dongle a sharp knock on the desk (not a drop). The log says "Impact: x.x g" and "Impact of x.x g: reporting it now"; in Home Assistant the Impact event fires with the peak, in Traccar the record has `io247`. A plain shake or turning the dongle over shouldn't count. Set it back to `4g` afterwards.
- **Temperature**: the Temperature sensor in Home Assistant (or `io72` in Traccar) after the next heartbeat; roughly the room's, a few degrees warmer.
- **GNSS restart**: indoors without a fix, every 10 minutes the log says "No GNSS fix for 10 min: switching the GNSS off and on again"; outdoors the fix comes back as before.
- **Motion**: "Moved" in the log when the dongle is handled, and nothing at start-up.

**Send back:** the log around the knock, and the peaks of a few knocks and of a normal drive's bumps, to choose the threshold.

## 7e. Downloading the log over Wi-Fi

Add `packages/sd-logs-download.yaml` (after `sd-logs.yaml`) and `sd_log_password: !secret <car>_log_password` to the device config, with the password in your secrets.

- While the dongle is awake on Wi-Fi, open `http://freematics-eli.local/log` in a browser: it asks for the username (`logs`) and password, then downloads the log. `/log.1` is the file before it (404 until the log has rotated once). The log says "Sent /sdcard/esphome.log over HTTP (N bytes)".
- In Home Assistant, turn on the dongle's "Stay awake" switch: it stays awake on USB or in a parked car past the usual 10 minutes, and the switch turns itself off after 30 minutes (the log says "Standby: staying awake").
- Queued update: with the dongle asleep, install to it from Device Builder ("Update queued"). After the next wake on Wi-Fi (move or tap it, or wait for a heartbeat) the update installs; the log says "Standby: an update is under way; not sleeping until it ends", and the dongle doesn't sleep in the middle of it.

**Send back:** how long a 1 MB log takes to download, and whether the dongle kept logging during it.

## 7c. Listening to the OBD port (vehicle data, step one)

Not part of any configuration. With the car parked and on (or ignition on), paste [examples/bench-obd-listen.yaml](https://github.com/raetha/esphome-freematics/blob/dev/examples/bench-obd-listen.yaml) over the config, install, and watch the logs: 30 s after it starts, it sets the co-processor up (fixed 500 kbit/s, silent mode if supported), listens for 60 s, stops, and resets the co-processor. Nothing is sent to the car.

**Look for:** the co-processor's answers to `ATCSM1` (`OK` or `?`) and `ATSP6`, then any lines starting `<<<` while listening, and "done, N lines received while listening". Any dash message on the car during or after the test.

**Send back:** the whole log, and whether the car showed anything. Then paste the normal config back.

## 8. The stock firmware, for comparison (optional)

If the cellular module never answers in test 3: plug a computer into the other dongle (still on the stock firmware), open a serial monitor at 115200 baud, and power it up. The first lines show `TYPE:` (the co-processor version), `GNSS:` and the cellular module's start. That shows whether the same hardware's cellular works on that power, and how the stock firmware starts it.

**Send back:** the first minute of that log.
