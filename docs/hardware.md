# Hardware: Freematics ONE+ Model B

Pins and interfaces as ESPHome uses them. Sources: the [ONE+ r14 schematic](https://freematics.com/dl/schematics_oneplus_r14_20190612.pdf) (June 2019) and `libraries/FreematicsPlus/FreematicsPlus.h` / `FreematicsPlus.cpp` in [stanleyhuangyc/Freematics](https://github.com/stanleyhuangyc/Freematics). Where they differ, the library wins: the stock firmware built from it runs on these dongles. The r14 board shows an MPU9250 IMU while the product page lists an ICM-20948, so the Model B is a later revision; anything marked "unconfirmed" needs checking on a real unit.

## Pin map

| Function | ESP32 pins | Notes |
| --- | --- | --- |
| Cellular module (xBee socket) | RX GPIO35, TX GPIO2, PWRKEY GPIO27 | 115200 baud, no RTS/CTS. The library pulses PWRKEY high for 200 ms (`xbTogglePower`). GPIO2 is a strapping pin |
| Co-processor link (STM32F103) | RX GPIO13, TX GPIO14, reset GPIO15 (active low) | 115200 baud (`LINK_UART_BAUDRATE`), ELM327-style AT commands |
| Internal GNSS | none | Wired to the STM32's PB10/PB11; read through the co-processor (below) |
| External GNSS (optional M10) | RX GPIO32 or 34, TX GPIO33 or 26, power GPIO15 or 12 | Which pair depends on the co-processor version (`gpsBeginExt`). GPIO12 is the flash-voltage strapping pin: leave it alone unless an external receiver is fitted |
| OBD-II | none | CAN (TJA1050) and K-line are on the STM32 |
| Battery voltage | GPIO36 (ADC) | 100k/10k divider: × 11, then calibrate. `ATRV` on the co-processor is the alternative |
| microSD (SPI) | CS GPIO5, SCK GPIO18, MISO GPIO19, MOSI GPIO23 | |
| IMU (I2C) | SDA GPIO21, SCL GPIO22 | MPU9250 on r14; ICM-20948 per the product page (unconfirmed) |
| Buzzer, LED | GPIO25, GPIO4 | The stock firmware drives the buzzer at 2 kHz (LEDC) and beeps 50 ms when it reaches its server over Wi-Fi or once cellular is in service (`telelogger.ino`); the board package does the same, plus 200 ms at start-up |
| USB serial | CP2102 on UART0 | Flashing and logs |
| Flash | 16 MB | PSRAM: unconfirmed |

## Co-processor commands

From `FreematicsPlus.cpp` and `FreematicsOBD.cpp`. Each ends in `\r`.

| Command | Reply | Used for |
| --- | --- | --- |
| `ATI` | Contains `OBD` and a version like `x.y`; the library reads it as the device type (`getDeviceType`) | Identify the co-processor; picks external GNSS pins |
| `ATR` | | Soft reset (versions 14 and later use the GPIO15 line instead) |
| `ATRV` | Battery voltage | Battery voltage |
| `ATGPSON` / `ATGPSOFF` | | Internal GNSS power |
| `ATGPS` | `$GNIFO,date,time,lat×1e6,lng×1e6,alt×100,speed×100,heading×100,sats,hdop` | Position; speed is in knots (`GPS_DATA.speed` is documented as knots; the telelogger multiplies by 1.852 for km/h) |
| `ATGRR` | Raw NMEA | Debugging GNSS |
| `ATGDS <hex bytes>` | | Send configuration bytes to the GNSS |
| `ATZ`, `ATE0`, `ATH0`, `ATSP <n>` | | ELM327-style OBD setup and protocol |
| `ATLP` | | Low-power mode |
| mode 01/09 PID requests (`0100`, `010D`, `0902`, ...) | Hex responses | OBD data and VIN: **active requests on the bus, not used** (see below) |

## Vehicle bus: passive only

The project sends nothing to the vehicle: no OBD requests (`ATZ`/`ATSP` set-up, mode 01/09 PIDs), no writes, no clearing codes. Requests and writes are the likely reason cars complained about the stock firmware. The commands the packages use (`ATI`, `ATRV`, `ATGPSON`, `ATGPS`) are answered by the co-processor itself (its version, its own voltage reading, its GNSS) and don't touch the bus. The `freematics_link` component can send only those four: its command set is a fixed enum, with no way to pass other text through.

What passive reading can get is open: the library has a CAN monitoring mode (`ATM1`, used by the `can_sniffer` example) that listens to broadcast frames, but many cars put a gateway between the OBD port and their internal buses, so there may be little to hear. Unknown too: whether the co-processor's own firmware probes the bus by itself at power-up. Phase 4 finds out on each car.

## Cellular modules

Freematics sells SIM7600E-H, SIM7600A-H and SIM7070G modules for the xBee socket. ESPHome's `modem` supports `SIM7600` and `SIM7070` (network type sensing only on SIM7600/SIM7670). The owner's dongles have the **SIM7070G** (LTE Cat-M1/NB-IoT with 2G fallback), on Hologram (APN `hologram`). ESPHome has no network type sensor for it.

## Observed on hardware

Replies from the probe buttons and anything learned on the bench go here, dated.

- (nothing yet)
