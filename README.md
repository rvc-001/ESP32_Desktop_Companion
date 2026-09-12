# Deskoo V2

**Hardware:**
- Waveshare ESP32-S3-LCD-2 (non-touch)
- 2" 240x320 IPS LCD
- USB-C only

---

## What changed from the glitchy prototype

- **Full-Frame Buffered Rendering:** The ESP32 renders the complete 320x240 frame into a RAM framebuffer first, then pushes the finished frame to the LCD in one operation. This eliminates the visible erase/draw sequence that caused most flicker.
- **20 FPS Animations & Snappy UI:** The Stream Deck screen is static and is NOT needlessly refreshed every animation frame. Music and Shamsher pages target a buttery smooth 20 FPS. Page transitions have been optimized to a rapid 3-step punchy fade with debouncing on all hotkeys to prevent accidental double-swiping.
- **Firmware-Baked Assets (No LittleFS):** All graphics, boot animations (including the bouncy carrot loader), and rabbit sprites are generated into C++ header files and compiled directly into `PROGMEM`. There are no `.gif` or `.png` files hosted on a LittleFS partition anymore, resulting in instant loading times.
- **Robust Serial Sync:** A refined `PING/READY/PONG` serial handshake guarantees that the Python script flawlessly connects to the ESP32 the exact millisecond the bootloader finishes, even if the device takes a moment to boot.
- **Singleton Architecture:** The Python host runs a named Windows Mutex lock, ensuring that only a single instance of `desktop_companion.py` can ever run on your PC at any given time, preventing nasty COM port conflicts.

---

## 1. Arduino libraries

**Install:**
- Arduino_GFX Library by moononournation
- Adafruit GFX Library

**Board:**
- ESP32S3 Dev Module
- Flash: 16 MB
- PSRAM: OPI PSRAM
- USB CDC On Boot: Enabled
- Port: Whatever your board currently uses

Upload `esp32_desktop_companion_v2.ino` ONCE.
After that, normal use does not require Arduino IDE.

---

## 2. PC Helper

Open PowerShell in this folder:

```powershell
py -m pip install -r requirements.txt
py desktop_companion.py
```

It automatically scans COM ports and handshakes with the ESP32. COM5 is not hardcoded.
The helper routes the display to the Music page whenever the screen connects, then keeps scanning/reconnecting if the USB-C cable is unplugged and plugged back in.

### Hotkeys:
- **Ctrl+Shift+1** = WhatsApp
- **Ctrl+Shift+2** = Zen Browser
- **Ctrl+Shift+3** = VS Code
- **Ctrl+Shift+4** = Apple Music
- **Ctrl+Shift+5** = previous track
- **Ctrl+Shift+6** = play / pause
- **Ctrl+Shift+7** = next track
- **Ctrl+Shift+8** = lock PC
- **Ctrl+Shift+9** = shutdown request
- **Ctrl+Shift+Right** = next page
- **Ctrl+Shift+Left** = previous page
- **Ctrl+Shift+Down** = next deck item
- **Ctrl+Shift+Up** = previous deck item
- **Ctrl+Shift+Enter** = run selected deck action
- **Ctrl+Shift+PageUp** = previous track
- **Ctrl+Shift+Space** = play / pause
- **Ctrl+Shift+PageDown** = next track
- **Ctrl+Shift+R** = cycle Shamsher mood
- **Ctrl+Shift+S** = Shamsher sleeping
- **Ctrl+Shift+M** = Shamsher headphone music
- **Ctrl+Shift+E** = Shamsher eating carrot

---

## 3. Real-time media

The helper uses Windows Global System Media Transport Controls via `winsdk`.
Apple Music, browser media, and many media players expose metadata through this Windows API.

If `winsdk` cannot read a particular app, the rest of Deskoo still works.

Album art is read from the same Windows media session and sent to the ESP32 over serial as a stream of pixels. If album art is missing, the firmware shows a static album-art placeholder while the helper keeps retrying.

The Stream Deck page now includes: WhatsApp, Zen Browser, VS Code, Apple Music, Previous Track, Play/Pause, Next Track, Lock PC, Shutdown.
The app launcher tiles use brand-color logo drawings on the ESP32. For app tiles, the PC helper first brings an already-open app window to the front. If no matching window is open, it launches the app.

---

## 4. Run directly at Windows startup

Deskoo V2 uses a native Windows `.vbs` script approach to launch silently in the background at startup without popping open a terminal window.

### Option A (Automated Background Runner)
1. Press `Win + R`, type `shell:startup`, and hit Enter.
2. The installation scripts should have already created a `Deskoo_Companion.vbs` script in this folder during setup.
3. This VBS script is mapped directly to the workspace folder and runs `pythonw desktop_companion.py` silently in the background every time Windows boots.

### Option B (Manual Control)
If you prefer manual control, we provide utility scripts directly in the repository:
- Double-click `start_companion.bat` to launch the companion silently in the background right away.
- Double-click `stop_companion.bat` to cleanly forcefully shut down any running Python companion hosts, releasing the COM port (useful if you need to flash the ESP32).

---

## Future Plans
- **Expand Application Support:** Integrate specialized actions for design/dev applications (Figma, Photoshop, Docker, etc.).
- **Dynamic System Monitoring:** Build a new UI page specifically for tracking PC vitals (CPU/GPU load, temps, RAM) over serial in real-time.
- **Enhanced Shamsher AI Responses:** Make the Shamsher mascot react contextually to real PC events (like "celebrating" when music plays, or "sleeping" when idle).
- **Notification Mirrors:** Route Windows push notifications directly to the ESP32 screen for quick glancing.
- **Over-the-Air (OTA) Updates:** Provide a seamless mechanism to update the ESP32 firmware wirelessly instead of requiring physical USB flashing.
