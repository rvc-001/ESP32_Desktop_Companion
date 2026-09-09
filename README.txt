
# Deskoo V2

Hardware:
- Waveshare ESP32-S3-LCD-2 (non-touch)
- 2" 240x320 IPS LCD
- USB-C only

## What changed from the glitchy prototype

The ESP32 renders the complete 320x240 frame into a RAM framebuffer first, then pushes
the finished frame to the LCD in one operation. This eliminates the visible erase/draw
sequence that caused most flicker.

The Stream Deck screen is static and is NOT needlessly refreshed every animation frame.
Music and Shamsher pages target 20 FPS.

## 1. Arduino libraries

Install:
- Arduino_GFX Library by moononournation
- Adafruit GFX Library

Board:
- ESP32S3 Dev Module
- Flash: 16 MB
- PSRAM: OPI PSRAM
- USB CDC On Boot: Enabled
- Port: whatever your board currently uses

Upload `esp32_desktop_companion_v2.ino` ONCE.

After that normal use does not require Arduino IDE.

## 2. PC helper

Open PowerShell in this folder:

    py -m pip install -r requirements.txt
    py desktop_companion.py

It automatically scans COM ports and handshakes with the ESP32.
COM5 is not hardcoded.

The helper routes the display to the Music page whenever the screen connects,
then keeps scanning/reconnecting if the USB-C cable is unplugged and plugged
back in.

The ESP UI uses a Deskoo gradient theme: near-black crimson at the top with a
bright red glow rising from the lower center.

Hotkeys:
- Ctrl+Shift+1 = WhatsApp
- Ctrl+Shift+2 = Zen Browser
- Ctrl+Shift+3 = VS Code
- Ctrl+Shift+4 = Apple Music
- Ctrl+Shift+5 = previous track
- Ctrl+Shift+6 = play / pause
- Ctrl+Shift+7 = next track
- Ctrl+Shift+8 = lock PC
- Ctrl+Shift+9 = shutdown request
- Ctrl+Shift+Right = next page
- Ctrl+Shift+Left = previous page
- Ctrl+Shift+Down = next deck item
- Ctrl+Shift+Up = previous deck item
- Ctrl+Shift+Enter = run selected deck action
- Ctrl+Shift+PageUp = previous track
- Ctrl+Shift+Space = play / pause
- Ctrl+Shift+PageDown = next track
- Ctrl+Shift+R = cycle Shamsher mood
- Ctrl+Shift+S = Shamsher sleeping
- Ctrl+Shift+M = Shamsher headphone music
- Ctrl+Shift+E = Shamsher eating carrot

On the Music page, Ctrl+Shift+PageUp / Space / PageDown highlights the matching
on-screen control and runs it through the ESP action path.

## 3. Real-time media

The helper uses Windows Global System Media Transport Controls via `winsdk`.
Apple Music, browser media and many media players expose metadata through this Windows API.

If `winsdk` cannot read a particular app, the rest of Deskoo still works.

Album art is read from the same Windows media session and sent to the ESP32 as
a compact 64x64 RGB565 image. If album art is missing, the firmware shows a
static album-art placeholder while the helper keeps retrying.
The Music page uses a centered album-art layout inspired by the
`displayAlbumArt/tftAlbumArt` SpotifyArduino example, with metadata, progress
and transport controls stacked below the cover.

The Stream Deck page now includes:
- WhatsApp
- Zen Browser
- VS Code
- Apple Music
- Previous Track
- Play/Pause
- Next Track
- Lock PC
- Shutdown

The app launcher tiles use brand-color logo drawings on the ESP32. They are
drawn directly in firmware so no external image files are needed at boot.
For app tiles, the PC helper first brings an already-open app window to the
front. If no matching window is open, it launches the app.

The Shamsher page shows one high-resolution white rabbit character drawn
directly in firmware. Its mood can be toggled between sleeping, headphone music
and eating carrot with the Shamsher hotkeys above.

## 4. Run directly at Windows startup

After testing the helper once, install the startup launcher:

    py desktop_companion.py --install-startup

This writes a hidden Windows Startup launcher for this exact folder. After that
the normal workflow is simply:

1. Boot Windows
2. Plug in the display
3. Helper detects it automatically
4. Use hotkeys / deck immediately

No Arduino IDE and no Serial Monitor.

To remove the startup launcher:

    py desktop_companion.py --uninstall-startup
