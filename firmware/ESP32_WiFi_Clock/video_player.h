#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// Plays a looping video (Video Face) entirely from the SD card - the file
// is written once by the web portal's upload endpoint (see web_portal.cpp's
// /video/upload) and read back here frame by frame; nothing about
// playback depends on the web portal or WiFi still being up, so it keeps
// working after the browser tab that uploaded it is closed.
//
// The video itself is never decoded on-device - see the web portal's
// upload page, which extracts already-cropped/resized RGB565 frames in
// the browser using the source video's own <video>/<canvas> decoding, and
// uploads a small custom container (4-byte "VID1" magic, then
// little-endian uint16 width/height/frameCount/fps, then frameCount raw
// RGB565 frames back to back, each width*height*2 bytes). This is a plain
// blit target for the ESP32, not a codec.
namespace VideoPlayer {

// True if the SD card has a valid saved video (checked once, cached).
bool isAvailable();

// Restarts playback from frame 0 on the next draw() call. Call this
// whenever Video Face is (re)entered - see ClockDisplay::nextFace()/
// prevFace() - so switching back to it always starts from the beginning
// rather than resuming mid-clip.
void reset();

// Draws the next due frame into the w x h rectangle at (x, y) on tft, if
// the video's own frame interval has elapsed since the last draw (loops
// back to frame 0 after the last one). Safe to call on every
// ClockDisplay::update() tick regardless of actual elapsed time - it
// self-paces off millis() and no-ops between frames. No-ops entirely if
// there's no valid video, or its saved width/height don't match w/h.
void draw(TFT_eSPI &tft, int x, int y, int w, int h);

} // namespace VideoPlayer
