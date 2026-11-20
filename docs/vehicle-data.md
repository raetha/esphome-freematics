# Vehicle data: what passive listening would take

Research for phase 4, done before writing any code because the owner's rule is that nothing is sent to the vehicle bus. Conclusion first: **nothing is built for phase 4 yet**, because the one listening mode the co-processor offers can't be shown to be silent, and a CAN node that isn't silent does put bits on the bus.

## What the co-processor offers

The CAN and K-line transceivers sit behind the STM32 co-processor, whose firmware is closed. Freematics' library (`FreematicsOBD.cpp`) drives it with ELM327-style commands:

| Command | Library function | What it does |
| --- | --- | --- |
| `ATZ`, `ATE0`, `ATH0`... | `COBD::init()` | Reset and set up the co-processor's OBD side (local) |
| `ATSP <n>` | `COBD::init(protocol)` | Pick the protocol and bit rate (local, until something is sent) |
| `ATCF <id>`, `ATCM <mask>` | `setHeaderFilter()`, `setHeaderMask()` | Which CAN IDs to pass on (local) |
| `ATM1` / `ATM0` | `COBD::sniff()` | Start and stop "sniffing": every matching frame comes back as a line of hex |
| `ATSH <id>` | `setCANID()` | Header for frames it sends: **transmits** with the next request |
| PID requests (`010D`...), `sendCANMessage()` | `readPID()` etc. | **Transmit** |

`COBD::init()` without a fixed protocol detects it by sending a PID request (speed), so auto-detection is out: a passive probe would have to fix the protocol with `ATSP` and never call `init()`'s detection.

## Why `ATM1` isn't enough on its own

A CAN controller in its normal mode is an active node even when it never sends a frame of its own:

- It **acknowledges** every frame it receives correctly (a dominant bit in the ACK slot). Harmless on a healthy bus, but it is a transmission.
- At the **wrong bit rate** it sees every frame as an error and sends **error frames**, which corrupt other nodes' traffic and can raise fault codes and dash warnings. Picking the bit rate by trial on a live bus is exactly how a "listening" device upsets a car.

Listen-only (silent, bus-monitoring) mode avoids both: the controller receives but drives no bit at all. The STM32F103's bxCAN has such a mode, and ELM327 v1.4b and later expose it as `AT CSM1`, but Freematics doesn't document whether its firmware uses it for `ATM1` or accepts `ATCSM`. Until that's known, sniffing doesn't meet the rule.

## Would there be anything to hear?

Probably little on these two cars. Many recent cars connect the OBD port's CAN pins only to a gateway that answers diagnostic requests and passes no broadcast traffic; Volvo's central gateway module works that way, and newer Subarus have a security gateway on the diagnostic port. A silent listener there hears nothing until something asks, and asking is what's ruled out. This needs checking on each car, which itself needs a silent listener.

## What would make phase 4 possible

1. **Confirm a silent mode**: ask Freematics whether the co-processor's `ATM1` sniffing runs the CAN controller in silent (listen-only) mode, or whether `ATCSM1` is supported. This is the owner's call to make.
2. **Know each car's OBD CAN bit rate** (almost always 500 kbit/s, 11-bit IDs, for 2008-and-later cars) and fix it with `ATSP 6`, never auto-detect.
3. Only then a bench probe: `ATSP 6`, `ATCSM1` if supported, `ATM1`, log what arrives for a few minutes with the engine running, `ATM0`.

Meanwhile the battery voltage (the ESP32's own ADC) is vehicle data that needs no bus at all, and a voltage above about 13.2 V is a good sign the engine is running.

## The owner's Volvo (2023 XC60 Recharge, plug-in hybrid)

The owner's wish list (2026-11-20): the device tracker first, then VIN, fuel level, the hybrid battery's charge, and as much of what Volvo's cloud integration in Home Assistant shows as the car gives directly, without upsetting the bus. A listening test is allowed if it's careful.

What's realistic, in order:

1. **Listen** (`examples/bench-obd-listen.yaml`, built, not in any configuration): one minute with the bus fixed to 500 kbit/s, 11-bit IDs (`ATSP6`, no detection), silent mode tried (`ATCSM1`), car parked and on. Expected: little or nothing behind Volvo's gateway. If frames do arrive, they can be decoded without ever sending.
2. **Ask with standard OBD-II requests**, only if the owner decides to allow sending (not built): while the car is on (12 V at 13.2 V or more) and only at the start and end of a trip, one request at a time, a second apart, to the engine controller's physical address rather than the broadcast one. Once, the supported-PID lists (`0100`, `0120`... `01A0`); then VIN (`0902`), fuel level (`012F`), hybrid battery remaining (`015B`, if supported), odometer (`01A6`, if supported). The stock firmware asked every second while driving and detected the protocol on the live bus, the likely causes of its dash alerts.
3. **Volvo's own data** (doors, locks, windows, tyre pressures, charging state, service) uses manufacturer diagnostic identifiers that aren't publicly documented, and newer Volvos secure some of them: out of reach for now. The cloud integration stays the source for those.

