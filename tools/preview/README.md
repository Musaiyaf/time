# Host render preview

Renders the weather and calendar screens from the real firmware drawing
code straight to PNG screenshots on your own machine - no device, no
flashing, no phone photo needed to see whether a layout change actually
worked.

## Why this exists

`menu.cpp`'s `drawWeatherScreen()`/`drawCalendarScreen()` are only ever
exercised by holding a button on real hardware, so a layout bug (clipped
text, two labels overlapping) was invisible until someone happened to
look at the screen in the state that triggers it. This harness compiles
those same functions - unmodified, aside from a couple of
`#ifdef HOST_PREVIEW`-guarded pass-through hooks in `menu.h`/`menu.cpp` -
against a from-scratch stand-in for `TFT_eSPI` that renders into an
in-memory RGB565 framebuffer instead of a real panel, and writes it out
as a PNG. Every hardware/network dependency (WiFi, SD, the weather and
calendar HTTP fetches) is replaced by a fake that reads from one posable
`HostScene` struct (`shim/host_scene.h`), so a scene can push the layout
to its edges on purpose - a very long city name, 100% humidity, a
festival landing on today - not just whatever the real world happened to
supply when someone last looked at the clock.

This already found two real firmware bugs before they were fixed on
hardware:
- the "Humidity" label collided with a 100%-wide value (now "Humid").
- the "+Nh" scroll indicator collided with the last hourly column's
  rain-percentage text (now removed - the hour labels already show
  exactly which hours are on screen).

## Usage

```
tools/preview/build.sh                  # renders every scene
tools/preview/build.sh weather_scrolled # renders just one
```

PNGs land in `tools/preview/out/<scene>.png`, at 3x scale so individual
pixels are still visible on a normal monitor (the real panel is only
320x170). See `main.cpp` for the full scene list, or run the binary with
no matching scene name to have it print one.

## Adding a scene

Add a `sceneFoo()` function in `main.cpp` that calls `resetScene()`, pokes
whatever fields of the global `scene` matter for the case you're testing,
calls the relevant `Menu::preview*()` hook, then `save("foo")`. List it in
the `SCENES[]` table at the bottom of the file.

If the screen you want to preview doesn't have a `Menu::preview*()` hook
yet, add one - a thin pass-through to the file-local draw function,
guarded by `#ifdef HOST_PREVIEW`, declared in `menu.h` and defined in
`menu.cpp` (see `previewWeatherScreen`/`previewCalendarScreen` for the
simple case, and `previewWeatherNoCity`/`previewWeatherNoForecastYet` for
a screen that's reached via a different code path than the "normal"
one - mirror the real call site exactly, don't just pose empty data and
call the normal draw function, or the preview can show a composite that
can never actually appear on hardware).

## Scope and limitations

- Only the weather and calendar screens are wired up. The clock faces
  (rainbow grid, seven-segment, video, photo, botanical) aren't - nobody
  has hit a bug in one of those from a photo, so there was no case to
  build a scene for.
- Digit fonts loaded via `loadFont()` (the big clock-face digit bitmaps,
  which are `.vlw` smooth fonts) are **not** rendered - the shim only
  records that a smooth font was requested. A scene that needs to show an
  actual clock face's digits would need that gap addressed first.
- Fidelity depends on the shim's `TFT_eSPI.cpp` matching the real
  library's text-layout math (datum offsets, glyph baseline placement,
  the opaque-text background-erasure rectangle). It's transcribed from
  the real TFT_eSPI source, not approximated, but a future TFT_eSPI
  version could still drift from it.
- The three GFXFF fonts the firmware uses (`FreeSansBold9pt7b/12pt7b/18pt7b`)
  are bundled verbatim under `third_party/gfxff_fonts/` (BSD-licensed, see
  `license.txt` there) so the harness doesn't depend on any particular
  machine's Arduino toolchain cache.
- The wall clock is frozen (`hostSetFakeNow()` in `shim/host_runtime.cpp`)
  so renders are reproducible run to run - nothing here depends on the
  real date/time unless a scene explicitly poses one.
