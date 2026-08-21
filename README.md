# ESP32-S3 WiFi Grid Clock

A desk clock for an **ESP32-S3-N16R8** driving a **1.9" ST7789 320x170 IPS
display**, styled after a "grid clock" theme: a row of colored info badges
(date / weekday / day-of-year / WiFi signal) above a large grid of digit
cells showing `HH:MM:SS`.

WiFi is configured entirely on the device — no hardcoded SSID in the
firmware, no phone or laptop required. On first boot (or whenever it can't
connect) the clock scans for nearby networks and lets you pick one and type
its password right on the display. If there's no WiFi to connect to, you
can keep retrying or drop into **Manual Mode**: a fully offline clock,
starting at `00:00:00` on 1 January, that you set by hand from the
on-device menu.

> The reference photo this is styled after uses Chinese weekday/lunar-date
> labels rendered with a custom LVGL build. This firmware reproduces the
> same layout and colors with an English weekday + day-of-year badge
> instead, using TFT_eSPI, to keep the firmware small and avoid embedding a
> full CJK font. See [Customizing](#customizing) if you want to swap that in.

## Hardware

- ESP32-S3-N16R8 (16MB flash / 8MB octal PSRAM)
- 1.9" ST7789 IPS display, 320x170, SPI interface
- *Optional:* DS3231 battery-backed RTC module, I2C - keeps time through
  power loss (see [Manual Mode](#using-the-clock)). Needs a CR2032 coin
  cell in the module's holder; the firmware works fine without one, it
  just falls back to its Jan-1 placeholder when offline instead.
- *Optional:* SD card module, SPI - browse its files from the on-device
  menu. Works fine without one; the SD Card menu item just says there's no
  card.

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

### Optional: DS3231 RTC backup

| DS3231 pin | ESP32-S3 GPIO |
|---|---|
| SDA | 8 |
| SCL | 9 |
| VCC | 3V3 |
| GND | GND |

Not wiring one up is fine — the firmware probes for it once at boot and
just skips every RTC-related step if nothing answers. Change
`RTC_SDA_PIN`/`RTC_SCL_PIN` in `config.h` if you wire it to different GPIOs.

### Optional: SD card browser

| SD module pin | ESP32-S3 GPIO |
|---|---|
| SCK | 15 |
| MISO | 16 |
| MOSI | 17 |
| CS | 18 |
| VCC | 3V3 or 5V (check your module — some need 5V) |
| GND | GND |

Runs on its own dedicated SPI bus, entirely separate from the display's, so
there's no bus-sharing to get right. Not wiring one up is fine — the
firmware probes for a card once at boot and the SD Card menu item just
says there's no card if nothing answers. Change `SD_SCLK_PIN`/
`SD_MISO_PIN`/`SD_MOSI_PIN`/`SD_CS_PIN` in `config.h` if you wire it to
different GPIOs.

### Optional: piezo buzzer (button clicks)

| Buzzer pin | ESP32-S3 GPIO |
|---|---|
| Signal | 6 |
| VCC | 3V3 |
| GND | GND |

A short click on every button press - LEFT/RIGHT/OK, anywhere in the
firmware, not just the settings menu. Not wiring one up is harmless (an
unwired GPIO output does nothing) but there's no probe for it like the
RTC/SD card get, since a passive buzzer has no way to answer one -
instead just leave it off from **Settings → Button Sound**, which
persists across a power cycle. Change `BUZZER_PIN` in `config.h` if you
wire it to a different GPIO.

### Buttons

Three momentary pushbuttons, each wired between a GPIO and GND (the
firmware uses `INPUT_PULLUP`, so no external resistor is needed):

| Button | ESP32-S3 GPIO | Notes |
|---|---|---|
| LEFT | 4 | |
| RIGHT | 5 | |
| OK | 0 | The BOOT button - most ESP32-S3 dev boards already have this wired, so OK usually needs no extra hardware. |

LEFT/RIGHT cycle clock faces; holding LEFT opens the
[weather screen](#weather) and holding RIGHT the
[calendar](#calendar); holding OK opens the on-device settings menu
(see [Using the clock](#using-the-clock)). Holding OK for 3
seconds right after power-up wipes any saved WiFi credentials, so the
next boot starts fresh with the "no WiFi" try-again-or-Manual-Mode
prompt. Change
`BTN_LEFT_PIN`/`BTN_RIGHT_PIN`/`BTN_OK_PIN` in
`firmware/ESP32_WiFi_Clock/config.h` if you wire them to different GPIOs.

## Firmware layout

```
firmware/ESP32_WiFi_Clock/
  ESP32_WiFi_Clock.ino   - setup()/loop(), ties everything together
  config.h               - pins, AP name, default time zone/NTP
  wifi_manager.h/.cpp     - NVS-backed WiFi credential storage + STA/AP control
  web_portal.h/.cpp       - the setup webserver (scan/save/reset routes)
  webpage_html.h          - the self-contained HTML/CSS/JS setup page
  clock_display.h/.cpp    - TFT_eSPI rendering of the clock theme
  menu.h/.cpp             - on-device menus plus the weather and calendar screens
  weather.h/.cpp          - Open-Meteo current conditions + hourly forecast (no API key)
  calendar_events.h/.cpp  - Nager.Date public holidays for the city's country (no API key)
  net_fetch.h/.cpp        - the one shared HTTPS GET both of those use
  json_lite.h             - the few JSON lookups they need, instead of a JSON library
  tz_database.h           - ~430 IANA zones grouped by continent, for menu.cpp
  rtc_backup.h/.cpp        - optional DS3231 backup RTC over I2C (raw Wire, no library)
  sd_card.h/.cpp           - optional SD card module (SD/SPI, ships with the ESP32 core)
  buzzer.h/.cpp            - optional piezo buzzer: a click on every button press
TFT_eSPI_Setup/User_Setup.h - TFT_eSPI display driver configuration
tools/make_digit_font.html   - traces photos into a compilable digit font (.h)
tools/preview/               - renders the weather/calendar screens to PNG on the host, no device needed (see tools/preview/README.md)
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

**Certain digits never appear (e.g. you only ever see 1, 5, 6, 7, 8, 9 —
never 0, 2, 3 or 4), while badges and the colon dots render fine:** the
smooth font's glyphs are too wide for the digit sprite. TFT_eSPI does *not*
clip an oversized smooth-font glyph — `drawGlyph()` skips it entirely and
draws nothing — so only the digits that happen to be narrow enough show up,
and which digits are missing looks random as the time changes. Regenerate
the `.vlw` font at a point size whose widest glyph is smaller than
`CELL_DIGIT_W` in `clock_display.cpp` (see Customizing). Note this looks
superficially like flaky wiring, but a giveaway that it isn't: the *same*
digits always fail, and the serial monitor shows a clean boot with no
crash or reset.

## Using the clock

**First boot / no saved WiFi:** the clock scans for nearby networks and
shows them on the display (LEFT/RIGHT to browse). Tap OK on yours; if it's
locked, type the password on the on-screen keyboard, then it connects and
saves the credentials. If it can't find or connect to anything, it asks
whether to **try again** or drop into **Manual Mode** (fully offline - see
below); "try again" re-scans as many times as you like. Once connected, the
clock syncs the time over NTP automatically, using the UTC time zone by
default - set the real one afterwards from **Settings → Time Zone**.

**Changing WiFi or time zone later:** the easiest way is the on-device menu
below (hold OK → **Settings** → **WiFi** or **Time Zone**) — no phone
needed for either. The web page still works too: while connected it's
served from `http://esp32-clock.local/` (mDNS - works out of the box on
most phones, Macs and Linux; some Windows/router setups don't support it,
in which case use the IP address from the on-device **About** screen or
your router's client list instead). Reflash, hold OK for 3s at power-up, or
use the on-device WiFi menu item to reconnect to a different network.

**Settings menu:** a scrollable list (LEFT/RIGHT to move, OK to open),
the same style as the SD Card browser - **WiFi**, **Time Zone**,
**Date/Time**, **Weather City**, **Button Sound** (toggles the
[optional buzzer](#optional-piezo-buzzer-button-clicks)'s click straight
from its row, no submenu needed - shows its current ON/OFF state), and
**About**.

**Switching clock faces:** while the clock is running, LEFT/RIGHT taps
cycle between seven clock faces: the rainbow grid face, where each digit
rolls to its next value like a train on a vertical rail track - the old
digit slides up and off the top of its cell while the new one rises from
below to take its place, rather than the instant swap every other face
still uses; a retro LED face (classic digital-alarm-clock style 7-segment
digits, bright red on black, with a faint ghost of the unlit segments);
[**Video**](#video-wallpaper), which loops a short video clip fullscreen;
[**Photo**](#photo-face), a
font-sampler face - each digit *value* 0-9 is its own real photographed
typeface and colour, not one consistent font - on a plain white
background (several of the digits are themselves too dark to read on
the black background every other face uses), with a matching white
status bar; [**Botanical**](#botanical-face), illuminated-manuscript
digits - an orange/red letterform on its own black panel, bordered with
green vines and small yellow flowers - on a deep vine-green background
sampled from that same artwork, with a matching green status bar and
cream text; and [**Silver**](#silver-face), chrome-gradient numerals with
a soft glow on a plain black background, with a matching gunmetal status
bar that gets the same glossy top-edge highlight as the digits
themselves; and [**Flip Clock**](#flip-clock-face), a split-flap
"departure board" face where each digit is an actual two-part card that
flips to its next value, on a slate-grey page matching the status bar.
The status bar re-skins to match whichever face is active (or hides
entirely on Video). The choice isn't saved across a power cycle - it
always starts on the rainbow grid face.

### Video Wallpaper

Requires the [optional SD card](#optional-sd-card-browser) - there's
nowhere else on the ESP32 to fit even a few seconds of video.

The ESP32 has no video decoder, so the web portal (`http://esp32-clock.local/`
while connected, or the setup page while in AP mode) does the actual
decoding in the browser itself: pick a video with the **Video Wallpaper**
card's **Choose video** button, drag the crop box and use the zoom slider
to frame it, pick a length (1-8s), then **Save to clock**. The page reads
frames from the video using its own `<video>`/`<canvas>` decoder, crops/
resizes each one to 320x170 (the clock's *entire* screen - Video Face hides
the usual date/weekday/WiFi status bar for a fullscreen look, unlike every
other face) at 5 fps, converts straight to raw RGB565, and uploads the
result as one file to `/video/video.bin` on the SD card. The original
video file itself is never sent to the clock - only those already-
processed frames.

Playback (LEFT/RIGHT to the **Video** face) reads that file straight off
the SD card, entirely independent of the web portal or WiFi - closing the
browser tab, or the clock losing its WiFi connection, doesn't stop it. It
keeps looping until you upload a different video or remove it (the same
card's **Remove saved video** button). No SD card, or nothing uploaded
yet, and the face just shows a short message instead of a blank screen.

### Photo Face

A font-sampler face built from real photographed digits rather than one
consistent typeface: each digit *value* 0-9 keeps its own distinct font
and colour (a serif "0", a bold "1", a script "2", and so on), unlike
every other face where every digit shares the same font. Several of those
digits are themselves black, dark brown, or dark navy - too close to
invisible on the black background every other face uses - so Photo runs
on a plain white background instead, with a matching white status bar
(`THEME_PHOTO` in `clock_display.cpp`), rather than re-skinning to a dark
badge palette like the other faces.

### Botanical Face

An illuminated-manuscript style face: each digit is its own orange/red
letterform on a black panel, bordered with green vines and small yellow
flowers, in the manner of a historiated initial from an old manuscript
page. Unlike Photo, each digit's black panel is kept as part of the
artwork rather than removed - what changes per face is the *background
behind and around* the panels and the status bar, both set to a deep
vine-green sampled straight from the digits' own leaves (`COL_BOTANICAL_BG`
in `clock_display.cpp`), with cream badge text sampled from the source
art's own parchment-page background.

### Silver Face

Chrome-gradient numerals (`SilverDigits.h`) with a soft outer glow, each
kept on the same slice of black backdrop they were cropped with - like
Botanical, that backdrop is kept rather than removed, which is only
seamless because this face's own background is black too
(`activePhotoBg()` in `clock_display.cpp`). The status bar gets a dark
gunmetal fill (`COL_SILVER_BADGE`) rather than the usual per-face flat
colour, plus a glossy highlight across its top edge - the same brightening
pass (`applyDigitGloss()`) the rainbow grid face already uses on its own
digit cells, reused here so the badges visibly "shine" like the digits do.

### Flip Clock Face

A split-flap "departure board" face: each digit is a two-part card (a
light face on a slate-grey page, matching the status bar to that same
page colour) with a hinge line through the middle, and changing to its
next value is an actual flip rather than the instant swap most faces
use. There's no true 3D rotation on a 2D panel, so it's approximated the
way most software recreations do it - a vertical crop anchored at the
hinge rather than a perspective squish - in two phases
(`drawFlipDigitCellAnimated()` in `clock_display.cpp`): the old top half
collapses down into the hinge, uncovering the new digit's top half
underneath as it shrinks; then a new bottom half grows back out of the
hinge, covering the old digit's bottom half as it expands. Reuses the
same digit font as the rainbow grid face - no extra flash cost - and the
same per-cell grid every face but Photo/Botanical/Silver already shares.

### Weather

**Hold LEFT** on any clock face to see the weather. The screen shows the
city, current temperature and conditions with an icon, what it feels
like, humidity and wind speed, then a strip of the next hours along the
bottom — each with its own icon, temperature and chance of rain.
LEFT/RIGHT scroll that strip through the full 12-hour forecast, holding
either one refetches immediately, and OK returns to the clock.

Set the city in either place — both save to the same setting:

- **On the clock:** hold OK → **Settings** → **Weather City**, and type
  the name on the same character-carousel keyboard the WiFi password
  entry uses (LEFT/RIGHT pick a character, OK appends it, then choose
  **SAVE**). The field starts pre-filled with the current city, so
  correcting one doesn't mean retyping it.
- **On the web page:** the **Weather** card, which saves without
  restarting the clock (unlike the WiFi form). Saving an empty name in
  either place turns the weather screen off again.

Either way the name is looked up once, so the clock needs to already be
on WiFi when you set it — a name like `London` or `Sao Paulo` resolves
to a "City, Country" label and its coordinates, and only the
coordinates are used from then on. The forecast refreshes about every 15
minutes while connected (sooner after a failure), and a refresh that
fails keeps showing the last good data with its age, rather than
blanking the screen.

Data comes from [Open-Meteo](https://open-meteo.com), which needs no API
key and no account, so there's nothing to sign up for or paste in beyond
the city name. Temperatures are Celsius and wind is km/h. Requests are
made over HTTPS without certificate validation
(`client.setInsecure()` in `weather.cpp`) — a deliberate trade for a
device fetching public, read-only data that has no way to update a
pinned CA root short of a reflash.

### Calendar

**Hold RIGHT** on any clock face for the calendar: a month grid with
today boxed in cyan and festival days picked out in orange, and the next
four festivals listed down the right-hand side with their dates.
LEFT/RIGHT step through months, holding either refetches, and OK returns
to the clock.

Holidays come from [Nager.Date](https://date.nager.at) - key-free like
the weather API - for whichever country the [weather city](#weather)
resolved to, so **setting a city is the only setup either feature
needs**. Two honest limits worth knowing:

- Nager.Date doesn't cover every country. When it has nothing for yours
  the screen says `Not available for XX` rather than sitting blank, and
  the month grid still works as a plain calendar.
- It lists *public holidays*, which covers the major festivals in most
  countries but isn't an exhaustive festival calendar, and names show in
  English - the display fonts are ASCII-only, so a local-script name
  would come out as stray glyphs rather than letters.

The list is fetched once the clock comes online and refreshed twice a
day; near year-end it also pulls the following year in, so "coming up"
doesn't run dry every December.

**On-device main menu:** hold OK (not a tap - hold it down) on any clock
face to open the top-level menu: three icon tiles, **SD Card**,
**Settings** and **Back**. LEFT/RIGHT move the selection, a tap of OK
confirms it, and holding OK backs out a level (or exits the menu entirely
from the top level).

- **SD Card** — a read-only file browser for the [optional SD card
  module](#optional-sd-card-browser): shows several entries at once as a
  scrolled list (like a phone's file browser), not one at a time.
  LEFT/RIGHT moves the highlighted row, OK opens a folder or "views" a
  file (its full name and exact byte size — this firmware doesn't render
  file contents, just browses them), and holding OK goes back up a
  folder, then out of the browser entirely once you're back at the root.
  Says "No SD card found" if nothing's wired up.
- **Settings** — opens a plain-text list with four items:
  - **WiFi** — scans for nearby networks and shows them one at a time
    (LEFT/RIGHT to browse, hold OK to go back to the list without changing
    anything). Tap OK on a network to select it; if it's locked, an
    on-screen keyboard appears (LEFT/RIGHT cycles through a
    letter/digit/symbol at a time, OK types the highlighted character, and
    DELETE/CONNECT/CANCEL sit at the end of the same carousel). It then
    connects and, on success, saves the new credentials to NVS, re-syncs
    the time over NTP, and re-announces mDNS — no reboot needed. This is
    also how you get out of **Manual Mode**: connecting successfully here
    switches the clock over immediately.
  - **Time Zone** — pick a region (Africa, America, Asia, Europe, ...),
    then a specific zone within it; the picker opens on whichever zone is
    currently active. Covers the same ~430 IANA zones as the web page's
    time zone search box, colour-coded by region. Confirming applies the
    new POSIX TZ string immediately (saved to NVS, and the clock re-syncs
    against it) — no reboot needed.
  - **Date/Time** — sets the clock by hand, one field at a time (Year,
    Month, Day, Hour, Minute): LEFT/RIGHT changes the highlighted field,
    OK confirms it and moves to the next, and confirming Minute applies
    the change right away (holding OK at any point cancels instead). This
    is how you correct **Manual Mode**'s placeholder clock, or nudge the
    time by hand any time.
  - **About** — shows the clock's current IP address and its mDNS
    hostname (`esp32-clock.local`), the two ways to reach the web setup
    page, or "Offline" while there's no WiFi connection (e.g. in Manual
    Mode).

**Manual Mode:** fully offline - no WiFi, no NTP, no web setup page. With a
[DS3231 backup RTC](#optional-ds3231-rtc-backup) wired up, it restores
whatever time was last known (the RTC keeps ticking on its own battery
through power loss); without one, it starts at `00:00:00` on 1 January of
the firmware's build year (a placeholder). Either way it then just counts
up using the ESP32's own clock, which isn't itself battery-backed - so
without the RTC module, that starting point resets on every power loss.
Correct it from **Settings → Date/Time** (also saved to the RTC if
present), or connect to WiFi from **Settings → WiFi** to get real synced
time instead (also backed up to the RTC once NTP lands).

**Time zone (advanced):** the search box above just fills in the "POSIX time
zone string" field under **Advanced** — you can also type/paste one directly
there yourself (e.g. `UTC0`, `CST-8` for China/Malaysia/Singapore,
`EST5EDT,M3.2.0,M11.1.0` for US Eastern with DST). Full reference list:
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

- **Colors / layout**: badge positions are constants near the top of
  `clock_display.cpp` (`B_YEAR`, `B_MDAY`, `B_WEEK`, `B_DOY`, `B_WIFI`).
  Badge *colours* are per-face, not fixed — each face has its own
  `BadgeTheme` (`THEME_RAINBOW`, `THEME_LED`, `THEME_PHOTO`,
  `THEME_BOTANICAL`), and `badgeTheme()` picks the one matching
  `currentFace`, so the status bar re-skins to match whichever clock face
  is active instead of staying the same palette on every face. Badges
  render as separated, rounded-corner pills (`drawPillBadge()` /
  `carveRoundCorners()`); the month/day badge is a single two-tone pill
  (`drawMonthDayBadge()`).
- **Clock digit fonts**: the big HH:MM:SS digits use a custom anti-aliased
  TFT_eSPI "smooth font" embedded as a byte array and loaded at runtime via
  `digitSpr.loadFont(...)` — no filesystem/SPIFFS needed. The rainbow face
  uses `FredokaDigits87.h` (digits 0-9 rendered from
  [Fredoka](https://fonts.google.com/specimen/Fredoka) Bold at 87pt, OFL-1.1
  licensed). To use a different font for this *smooth* (anti-aliased) font,
  rasterize new glyphs into TFT_eSPI's `.vlw` format and regenerate the
  header; `CELL_DIGIT_W`/`CELL_COLON_W` in `clock_display.cpp` control the
  cell widths if you need to resize. **Every glyph must be strictly
  narrower than `CELL_DIGIT_W` (51px)** — TFT_eSPI silently skips drawing a
  smooth-font glyph too wide for its sprite rather than clipping it, which
  makes just the wide digits invisible (see Troubleshooting).
  Alternatively, `tools/make_digit_font.html` traces photos of digits into
  a *non*-anti-aliased "GFXfont" header (the same format
  `FreeSansBold9pt7b`/`12pt7b` already use here) entirely in a browser, no
  Processing/TFT_eSPI tooling needed — a rougher look than a real vector
  font, but a self-service option that doesn't need a TTF at all.
- **Clock faces**: `drawDigitCell()` in `clock_display.cpp` dispatches to a
  per-face renderer (`drawRainbowGridDigitCell()`, `drawSevenSegDigitCell()`)
  based on `currentFace` - Video, Photo, Botanical and Silver bypass this
  dispatcher entirely and redraw their own whole row each tick
  (`VideoPlayer::draw()`, `drawPhotoRow()`), since none of them fit the
  fixed per-cell column grid the other two share. `ClockDisplay::nextFace()`
  cycles through the
  `ClockFaceId` enum (`FACE_COUNT` faces total) and is wired to a LEFT/RIGHT
  tap in `ESP32_WiFi_Clock.ino`. A face that wants its own smooth font
  (rather than plain geometry, like the LED face) needs to be added to
  `ensureDigitFont()` too, since `digitSpr` can only hold one loaded font at
  a time. Add a new face by adding an enum value, a `drawXxxDigitCell()`
  function, a branch in `drawDigitCell()`, and (if it needs its own font) a
  branch in `ensureDigitFont()`.
- **12-hour clock**: change the `snprintf` format in
  `ClockDisplay::update()` (`clock_display.cpp`) and adjust `timeinfo.tm_hour`.
- **Chinese weekday/lunar labels**: replace the `WD[]` table and the
  day-of-year badge with a lunar-calendar conversion, and load a CJK
  TFT_eSPI smooth font (`.vlw`) covering the characters you need — TFT_eSPI's
  `Smooth_font` example shows how to generate and load one.
- **Rotation**: if the image appears upside-down or mirrored, change
  `tft.setRotation(3)` to `1` in `ClockDisplay::begin()` (`clock_display.cpp`).
