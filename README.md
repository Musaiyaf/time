# ESP32-S3 WiFi Grid Clock

A desk clock for an **ESP32-S3-N16R8** driving a **1.9" ST7789 320x170 IPS
display**, styled after a "grid clock" theme: a row of colored info badges
(date / weekday / day-of-year / WiFi signal) above a large grid of digit
cells showing `HH:MM:SS`.

WiFi is configured entirely from a phone or laptop — no hardcoded SSID in
the firmware. On first boot (or whenever it can't connect) the clock opens
a setup Access Point with a captive web page: scan for nearby networks (or
type one in manually), enter the password, optionally adjust the time zone
and NTP servers, and save. The clock reboots, connects, and syncs the time.

> The reference photo this is styled after uses Chinese weekday/lunar-date
> labels rendered with a custom LVGL build. This firmware reproduces the
> same layout and colors with an English weekday + day-of-year badge
> instead, using TFT_eSPI, to keep the firmware small and avoid embedding a
> full CJK font. See [Customizing](#customizing) if you want to swap that in.

## Hardware

- ESP32-S3-N16R8 (16MB flash / 8MB octal PSRAM)
- 1.9" ST7789 IPS display, 320x170, SPI interface

### Wiring

| Display pin | ESP32-S3 GPIO |
|---|---|
| SCLK / SCL | 12 |
| MOSI / SDA | 11 |
| CS | 10 |
| DC | 13 |
| RST | 14 |
| BLK / LED (backlight) | 2 |
| VCC | 3V3 |
| GND | GND |

These are just free, safe GPIOs on an N16R8 module (they avoid the strapping
pins and the pins used internally for flash/octal PSRAM). If you've already
wired the panel to different pins, change them in **both**:

- `firmware/ESP32_WiFi_Clock/config.h`
- `TFT_eSPI_Setup/User_Setup.h`

A pushbutton from GPIO0 (BOOT) to GND is optional but useful: hold it for 3
seconds right after power-up to erase the saved WiFi credentials and force
the clock back into setup mode. Most ESP32-S3 dev boards already have a
BOOT button wired to GPIO0, so you may not need extra hardware.

## Firmware layout

```
firmware/ESP32_WiFi_Clock/
  ESP32_WiFi_Clock.ino   - setup()/loop(), ties everything together
  config.h               - pins, AP name, default time zone/NTP
  wifi_manager.h/.cpp     - NVS-backed WiFi credential storage + STA/AP control
  web_portal.h/.cpp       - the setup webserver (scan/save/reset routes)
  webpage_html.h          - the self-contained HTML/CSS/JS setup page
  clock_display.h/.cpp    - TFT_eSPI rendering of the clock theme
TFT_eSPI_Setup/User_Setup.h - TFT_eSPI display driver configuration
.github/workflows/build-firmware.yml - CI build producing a flashable .bin
```

## Arduino IDE setup

1. Install the **ESP32 board package** (Espressif) via Boards Manager:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Install the **TFT_eSPI** library via Library Manager.
3. **Apply the display configuration** — TFT_eSPI is configured by editing a
   file inside the library itself, so copy this repo's
   `TFT_eSPI_Setup/User_Setup.h` over the library's own copy, overwriting it:
   - Find your libraries folder (`Documents/Arduino/libraries/TFT_eSPI` on
     most systems, or the path shown in Arduino IDE's Preferences).
   - Copy `TFT_eSPI_Setup/User_Setup.h` from this repo to
     `<libraries>/TFT_eSPI/User_Setup.h`, replacing the existing file.
4. Open `firmware/ESP32_WiFi_Clock/ESP32_WiFi_Clock.ino` in the Arduino IDE.
5. Board settings (Tools menu):
   - Board: **ESP32S3 Dev Module**
   - PSRAM: **OPI PSRAM**
   - Flash Size: **16MB**
   - Partition Scheme: **Default 4MB with spiffs** (or any non-OTA scheme)
   - USB CDC On Boot: On (if you want Serial output over the native USB port)
6. Select the correct port and upload.

## Troubleshooting

**Boot loop / `Guru Meditation Error: ... panic'ed (StoreProhibited)` with
`EXCVADDR: 0x00000010`, crashing inside `TFT_eSPI::begin_tft_write()` right
after `tft.init()`:** this is a known ESP32 Arduino core vs TFT_eSPI SPI-port
numbering mismatch, not a wiring problem. On newer Arduino ESP32 cores
(3.x), `esp32-hal-spi.h` redefines the `FSPI` macro to `0` (a *driver enum*
value) for S2/S3/etc, but TFT_eSPI's default `#define SPI_PORT FSPI` (used
when neither `USE_HSPI_PORT` nor `USE_FSPI_PORT` is set) expects the old
*hardware peripheral index* convention, where only `2`/`3` are valid and `0`
means "no SPI peripheral" (that range is reserved for the internal
flash/PSRAM controllers) — so its raw register macro computes a base address
of `0`, and the next write to `*_spi_user` (register offset `0x10`) faults.
`TFT_eSPI_Setup/User_Setup.h` in this repo already works around it with
`#define USE_HSPI_PORT`, which forces a valid literal port index; if you
maintain your own `User_Setup.h` copy (e.g. you skipped step 3 above or
merged in changes), make sure that line is still present.

**Stuck on "Waiting for NTP... Ns" with an IP shown (e.g.
`192.168.1.x`):** WiFi connected fine, but the clock can't reach an NTP
server. This is almost always the local network, not the clock - most
commonly broken/unreachable DNS (the clock's default NTP servers,
`pool.ntp.org` and `time.nist.gov`, are hostnames) or a router/firewall
blocking outbound NTP (UDP port 123). The firmware always also tries a
fixed IP-based fallback server (Cloudflare's `162.159.200.1`) that doesn't
need DNS, so it should still sync even if DNS is the problem - give it a
couple of minutes. If it never syncs, check whether other devices on the
same network have working DNS/internet access, or try a different network
(e.g. a phone hotspot) to confirm the clock itself is fine.

## Using the clock

**First boot / no saved WiFi:** the display shows "WiFi Setup" with an
Access Point name and IP address. Connect a phone or laptop to that
`ESP32-Clock-Setup-XXXX` network; a setup page should pop up automatically
(captive portal), or open `http://192.168.4.1` manually. Tap a network from
the scanned list (or type one under "SSID" for hidden networks), enter the
password, optionally expand **Advanced** to set a POSIX time zone string
and NTP servers, then **Save & Connect**. The clock reboots and connects.

**Changing WiFi later:** while connected, the same web page is served from
the clock's own IP address (shown on its display isn't included in the
normal clock face — check your router's client list, or hold the reset
button to see the AP screen). Reflash or hold the BOOT button 3s to force
setup mode again, or use the "Forget saved WiFi" button on the page.

**Time zone:** the time zone field takes a POSIX `TZ` string, e.g.:
- `UTC0` (UTC, default)
- `EST5EDT,M3.2.0,M11.1.0` (US Eastern, with DST)
- `PST8PDT,M3.2.0,M11.1.0` (US Pacific, with DST)
- `CST-8` (China Standard Time)
- `GMT0BST,M3.5.0/1,M10.5.0` (UK, with DST)

A longer reference list is here:
https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

## Building via GitHub Actions

`.github/workflows/build-firmware.yml` compiles the sketch with `arduino-cli`
on every push that touches `firmware/**` (or via the **Run workflow** button
under the Actions tab). It installs the ESP32 core and TFT_eSPI, applies
`TFT_eSPI_Setup/User_Setup.h`, and compiles for an ESP32-S3 with 16MB flash
and octal PSRAM. The finished `ESP32_WiFi_Clock.bin` (plus bootloader/
partition binaries) is uploaded as a workflow artifact — download it from
the workflow run's **Artifacts** section and flash with `esptool.py` or the
Arduino IDE's "Upload Using Programmer" / esptool GUI tools.

## Customizing

- **Colors / layout**: all badge positions and colors are constants near the
  top of `clock_display.cpp` (`B_DATE`, `B_WEEK`, `B_DOY`, `B_WIFI`, and the
  `COL_*` values).
- **12-hour clock**: change the `snprintf` format in
  `ClockDisplay::update()` (`clock_display.cpp`) and adjust `timeinfo.tm_hour`.
- **Chinese weekday/lunar labels**: replace the `WD[]` table and the
  day-of-year badge with a lunar-calendar conversion, and load a CJK
  TFT_eSPI smooth font (`.vlw`) covering the characters you need — TFT_eSPI's
  `Smooth_font` example shows how to generate and load one.
- **Rotation**: if the image appears upside-down or mirrored, change
  `tft.setRotation(3)` to `1` in `ClockDisplay::begin()` (`clock_display.cpp`).
