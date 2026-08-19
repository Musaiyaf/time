// TFT_eSPI display driver configuration for the ESP32-S3-N16R8 + 1.9"
// ST7789 320x170 IPS panel used by this project.
//
// TFT_eSPI reads its configuration from
// <Arduino/libraries/TFT_eSPI/User_Setup.h> at compile time, so this file
// has to be COPIED (overwriting the library's own default) into that
// location, either:
//   - manually (see README.md "Arduino IDE setup"), or
//   - automatically, which is what .github/workflows/build-firmware.yml
//     does for CI builds.
//
// The pin numbers below MUST match firmware/ESP32_WiFi_Clock/config.h.

#define USER_SETUP_ID 9001

// ---- Driver ----
#define ST7789_DRIVER
#define TFT_RGB_ORDER TFT_BGR   // this panel's channel order is BGR, not RGB -
                                 // every colour was rendering with red/blue
                                 // swapped (e.g. an intended blue badge came
                                 // out orange) until this was flipped
// #define TFT_INVERSION_ON      // uncomment if colours look inverted

// Native panel resolution (rotation is handled at runtime in the sketch).
#define TFT_WIDTH  170
#define TFT_HEIGHT 320

// ---- Pins (must match firmware/ESP32_WiFi_Clock/config.h) ----
#define TFT_MISO -1
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   13
#define TFT_RST  14
#define TFT_BL    2
#define TFT_BACKLIGHT_ON HIGH

// ---- Fonts ----
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT8
#define LOAD_GFXFF   // enables the Adafruit GFX "Free Fonts" used for labels
// The big clock digits use an anti-aliased custom font (FredokaDigits92.h,
// loaded at runtime from a byte array via tft.loadFont()) rather than a
// compiled-in LOAD_FONTn - SMOOTH_FONT enables that loader.
#define SMOOTH_FONT

// ---- SPI ----
// Forces TFT_eSPI to use the literal legacy hardware SPI index 3 (a real,
// valid GPSPI peripheral) instead of its own default `#define SPI_PORT
// FSPI`. On Arduino ESP32 core 3.x, esp32-hal-spi.h redefines FSPI to 0 for
// S2/S3/etc (the *driver enum* value), but TFT_eSPI's raw register macro
// REG_SPI_BASE(i) only returns a valid address for i>=2 - for i=0 it
// returns NULL (that range is reserved for the internal flash/PSRAM SPI
// controllers), so TFT_eSPI ends up writing through a null pointer and
// crashes (Guru Meditation StoreProhibited, EXCVADDR 0x10) the instant
// tft.init() runs. USE_HSPI_PORT sidesteps the broken FSPI macro.
#define USE_HSPI_PORT

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
