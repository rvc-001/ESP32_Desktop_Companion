
"""
Deskoo PC helper for Windows.

What it does:
- auto-detects the ESP32 serial port (no hardcoded COM5)
- reconnects automatically after unplug/replug
- sends real-time Windows media title / artist / progress when winsdk is available
- global hotkeys for page switching and stream-deck selection
- executes app/PC actions requested by the deck

Hotkeys:
  Ctrl+Shift+1..9       Run the matching Stream Deck tile
  Ctrl+Shift+Right      Next page
  Ctrl+Shift+Left       Previous page

  Ctrl+Shift+Down       Next deck item
  Ctrl+Shift+Up         Previous deck item
  Ctrl+Shift+Enter      Run selected deck item
  Ctrl+Shift+PageUp     Previous track
  Ctrl+Shift+Space      Play / pause
  Ctrl+Shift+PageDown   Next track
  Ctrl+Shift+R          Cycle Shamsher mood
  Ctrl+Shift+S/M/E      Shamsher sleep / music / eat

Install:
  pip install pyserial pynput winsdk pillow

The winsdk dependency is optional. If it is missing, the display still works;
only real-time Windows media info is disabled.
"""

import asyncio
import argparse
import ctypes
import hashlib
import io
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import webbrowser
from dataclasses import dataclass
from typing import Optional
from ctypes import wintypes

import serial
from serial.tools import list_ports
from pynput import keyboard

BAUD = 115200
DEVICE_SIGNATURE = "PONG:ESP32-DESKTOP-COMPANION"
COVER_SIZE = 64
COVER_CHUNK_PIXELS = 24

# Native ESP32-S3 USB VID is commonly 0x303A.
# We do not rely only on VID because USB-UART bridge variants differ.
PREFERRED_VIDS = {0x303A, 0x10C4, 0x1A86}

ser: Optional[serial.Serial] = None
serial_lock = threading.Lock()
stop_event = threading.Event()

DECK_ACTIONS = [
    "WhatsApp",
    "Zen Browser",
    "VS Code",
    "Apple Music",
    "Previous Track",
    "Play/Pause",
    "Next Track",
    "Lock PC",
    "Shutdown",
]


def candidate_ports():
    ports = list(list_ports.comports())

    def score(p):
        s = 0
        desc = (p.description or "").lower()
        manu = (p.manufacturer or "").lower()

        if p.vid in PREFERRED_VIDS:
            s += 100
        if "esp" in desc or "esp" in manu:
            s += 60
        if "usb" in desc:
            s += 20
        if "serial" in desc or "jtag" in desc or "cdc" in desc:
            s += 20
        return s

    return sorted(ports, key=score, reverse=True)


def write_line(text: str) -> bool:
    global ser
    payload = (text.rstrip() + "\n").encode("utf-8", errors="ignore")

    with serial_lock:
        if not ser or not ser.is_open:
            return False
        try:
            ser.write(payload)
            return True
        except (serial.SerialException, OSError):
            pass

    close_serial()
    return False


def close_serial():
    global ser
    with serial_lock:
        old = ser
        ser = None
    if old:
        try:
            old.close()
        except Exception:
            pass


def try_handshake(port_name: str) -> Optional[serial.Serial]:
    try:
        s = serial.Serial(port_name, BAUD, timeout=0.15, write_timeout=0.5)
        time.sleep(0.25)
        s.reset_input_buffer()
        s.write(b"PING\n")

        deadline = time.time() + 0.8
        while time.time() < deadline:
            line = s.readline().decode(errors="ignore").strip()
            if DEVICE_SIGNATURE in line:
                print(f"[connected] {port_name}")
                return s

        s.close()
    except (serial.SerialException, OSError):
        pass
    return None


def connection_worker():
    global ser

    while not stop_event.is_set():
        if ser and ser.is_open:
            time.sleep(1.0)
            continue

        for p in candidate_ports():
            if stop_event.is_set():
                return
            s = try_handshake(p.device)
            if s:
                with serial_lock:
                    ser = s
                on_connected()
                break

        if not ser:
            print("[waiting] plug in the ESP32 display...")
            time.sleep(2.0)


def serial_reader_worker():
    while not stop_event.is_set():
        s = ser
        if not s or not s.is_open:
            time.sleep(0.25)
            continue

        try:
            line = s.readline().decode(errors="ignore").strip()
            if not line:
                continue

            if line.startswith("ACTION:"):
                run_action(line[len("ACTION:"):].strip())

        except (serial.SerialException, OSError):
            close_serial()


SW_RESTORE = 9
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.EnumWindows.restype = ctypes.c_bool
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.IsWindowVisible.restype = ctypes.c_bool
user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
user32.GetWindowTextLengthW.restype = ctypes.c_int
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetWindowTextW.restype = ctypes.c_int
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowThreadProcessId.restype = wintypes.DWORD
user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user32.ShowWindow.restype = ctypes.c_bool
user32.SetForegroundWindow.argtypes = [wintypes.HWND]
user32.SetForegroundWindow.restype = ctypes.c_bool
kernel32.OpenProcess.argtypes = [wintypes.DWORD, ctypes.c_bool, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.QueryFullProcessImageNameW.argtypes = [
    wintypes.HANDLE,
    wintypes.DWORD,
    wintypes.LPWSTR,
    ctypes.POINTER(wintypes.DWORD),
]
kernel32.QueryFullProcessImageNameW.restype = ctypes.c_bool
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = ctypes.c_bool


def window_process_name(hwnd: int) -> str:
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    if not pid.value:
        return ""

    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid.value)
    if not handle:
        return ""

    try:
        size = wintypes.DWORD(1024)
        buffer = ctypes.create_unicode_buffer(size.value)
        if kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
            return Path(buffer.value).name.lower()
    finally:
        kernel32.CloseHandle(handle)

    return ""


def iter_visible_windows():
    windows: list[tuple[int, str, str]] = []

    def collect(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True

        length = user32.GetWindowTextLengthW(hwnd)
        if length <= 0:
            return True

        buffer = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buffer, length + 1)
        title = buffer.value.strip()
        if title:
            windows.append((hwnd, title, window_process_name(hwnd)))
        return True

    user32.EnumWindows(WNDENUMPROC(collect), 0)
    return windows


def focus_existing_window(
    *,
    process_names: tuple[str, ...] = (),
    title_keywords: tuple[str, ...] = (),
) -> bool:
    wanted_processes = {name.lower() for name in process_names}
    wanted_titles = tuple(keyword.lower() for keyword in title_keywords)

    for hwnd, title, process_name in iter_visible_windows():
        title_l = title.lower()
        process_match = process_name in wanted_processes if wanted_processes else False
        title_match = any(keyword in title_l for keyword in wanted_titles)

        if process_match or title_match:
            user32.ShowWindow(hwnd, SW_RESTORE)
            user32.SetForegroundWindow(hwnd)
            print(f"[focus] {title}")
            return True

    return False


def launch_first_existing(candidates: list[Path], *args: str) -> bool:
    for candidate in candidates:
        if candidate.exists():
            subprocess.Popen([str(candidate), *args], shell=False)
            return True
    return False


def launch_whatsapp() -> None:
    if focus_existing_window(
        process_names=("whatsapp.exe", "whatsappbeta.exe"),
        title_keywords=("whatsapp",),
    ):
        return
    os.startfile("whatsapp:")


def launch_zen_browser() -> None:
    if focus_existing_window(
        process_names=("zen.exe",),
        title_keywords=(" - zen", "zen browser"),
    ):
        return

    candidates = [
        Path(os.environ.get("ProgramFiles", "")) / "Zen Browser" / "zen.exe",
        Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "Zen Browser" / "zen.exe",
        Path(os.environ.get("ProgramFiles(x86)", "")) / "Zen Browser" / "zen.exe",
    ]
    if not launch_first_existing(candidates, "https://www.google.com/"):
        webbrowser.open("https://www.google.com/")


def launch_apple_music() -> None:
    if focus_existing_window(
        process_names=("applemusic.exe", "itunes.exe"),
        title_keywords=("apple music", "itunes"),
    ):
        return

    candidates = [
        Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "WindowsApps" / "AppleMusic.exe",
        Path(os.environ.get("ProgramFiles", "")) / "Apple Music" / "AppleMusic.exe",
        Path(os.environ.get("ProgramFiles", "")) / "iTunes" / "iTunes.exe",
        Path(os.environ.get("ProgramFiles(x86)", "")) / "iTunes" / "iTunes.exe",
    ]
    if launch_first_existing(candidates):
        return

    for uri in ("music:", "applemusic:", "itunes:"):
        try:
            os.startfile(uri)
            return
        except OSError:
            pass

    webbrowser.open("https://music.apple.com/")


def launch_vs_code() -> None:
    if focus_existing_window(
        process_names=("code.exe",),
        title_keywords=("visual studio code",),
    ):
        return
    subprocess.Popen(["code"], shell=True)


def run_action(action: str):
    print("[action]", action)

    try:
        if action == "WhatsApp":
            launch_whatsapp()

        elif action == "Zen Browser":
            launch_zen_browser()

        elif action == "VS Code":
            launch_vs_code()

        elif action == "Apple Music":
            launch_apple_music()

        elif action == "Previous Track":
            run_media_control("previous")

        elif action == "Play/Pause":
            run_media_control("play_pause")

        elif action == "Next Track":
            run_media_control("next")

        elif action == "Lock PC":
            subprocess.Popen(
                ["rundll32.exe", "user32.dll,LockWorkStation"],
                shell=False
            )

        elif action == "Shutdown":
            # Safety: while developing, this is intentionally only a warning.
            # Replace the next line with the commented shutdown command when ready.
            print("[shutdown] requested - disabled during development")
            # subprocess.Popen(["shutdown", "/s", "/t", "0"], shell=False)

    except Exception as exc:
        print("[action error]", exc)


# -------------------- HOTKEYS --------------------

hotkeys = None


def trigger_deck_item(index: int) -> None:
    if not write_line(f"RUN:{index}"):
        print("[hotkey] ESP32 is not connected; deck command was not sent")


def trigger_music_control(index: int, fallback_action: str) -> None:
    if not write_line(f"MUSICRUN:{index}"):
        run_media_control(fallback_action)


def set_shamsher_mood(mood: str) -> None:
    if not write_line(f"RABBITMOOD:{mood}"):
        print("[hotkey] ESP32 is not connected; Shamsher mood was not sent")


def setup_hotkeys():
    global hotkeys

    bindings = {
        "<ctrl>+<shift>+1": lambda: trigger_deck_item(0),
        "<ctrl>+<shift>+2": lambda: trigger_deck_item(1),
        "<ctrl>+<shift>+3": lambda: trigger_deck_item(2),
        "<ctrl>+<shift>+4": lambda: trigger_deck_item(3),
        "<ctrl>+<shift>+5": lambda: trigger_deck_item(4),
        "<ctrl>+<shift>+6": lambda: trigger_deck_item(5),
        "<ctrl>+<shift>+7": lambda: trigger_deck_item(6),
        "<ctrl>+<shift>+8": lambda: trigger_deck_item(7),
        "<ctrl>+<shift>+9": lambda: trigger_deck_item(8),
        "<ctrl>+!": lambda: trigger_deck_item(0),
        "<ctrl>+@": lambda: trigger_deck_item(1),
        "<ctrl>+#": lambda: trigger_deck_item(2),
        "<ctrl>+$": lambda: trigger_deck_item(3),
        "<ctrl>+%": lambda: trigger_deck_item(4),
        "<ctrl>+^": lambda: trigger_deck_item(5),
        "<ctrl>+&": lambda: trigger_deck_item(6),
        "<ctrl>+*": lambda: trigger_deck_item(7),
        "<ctrl>+(": lambda: trigger_deck_item(8),

        "<ctrl>+<shift>+<right>": lambda: write_line("NEXT"),
        "<ctrl>+<shift>+<left>": lambda: write_line("PREV"),

        "<ctrl>+<shift>+<down>": lambda: write_line("SELECT:NEXT"),
        "<ctrl>+<shift>+<up>": lambda: write_line("SELECT:PREV"),
        "<ctrl>+<shift>+<enter>": lambda: write_line("RUN"),

        "<ctrl>+<shift>+<page_up>": lambda: trigger_music_control(0, "previous"),
        "<ctrl>+<shift>+<space>": lambda: trigger_music_control(1, "play_pause"),
        "<ctrl>+<shift>+<page_down>": lambda: trigger_music_control(2, "next"),

        "<ctrl>+<shift>+r": lambda: set_shamsher_mood("NEXT"),
        "<ctrl>+<shift>+s": lambda: set_shamsher_mood("SLEEP"),
        "<ctrl>+<shift>+m": lambda: set_shamsher_mood("MUSIC"),
        "<ctrl>+<shift>+e": lambda: set_shamsher_mood("EAT"),
    }

    hotkeys = keyboard.GlobalHotKeys(bindings)
    hotkeys.start()

    print("[hotkeys] Ctrl+Shift+1-9 deck, arrows, Enter, Space, PageUp/PageDown, R/S/M/E")


# ---------------- WINDOWS MEDIA -----------------

try:
    from winsdk.windows.media.control import (
        GlobalSystemMediaTransportControlsSessionManager as MediaManager
    )
    from winsdk.windows.storage.streams import DataReader
    HAVE_WINSDK = True
except Exception:
    HAVE_WINSDK = False

try:
    from PIL import Image
    HAVE_PIL = True
except Exception:
    HAVE_PIL = False


@dataclass
class MediaState:
    title: str = ""
    artist: str = ""
    progress: int = -1
    playing: bool = False


_last_media = MediaState()
_last_cover_hash = ""
_last_cover_pixels: Optional[list[int]] = None
_last_track_identity = ""
_last_cover_attempt = 0.0


def sanitize_serial_text(s: str) -> str:
    # | and newlines are protocol separators
    return (
        (s or "")
        .replace("|", "/")
        .replace("\r", " ")
        .replace("\n", " ")
        .strip()
    )


async def read_media_state() -> Optional[MediaState]:
    if not HAVE_WINSDK:
        return None

    try:
        manager = await MediaManager.request_async()
        session = manager.get_current_session()
        if not session:
            return MediaState("No media", "Windows", 0, False)

        props = await session.try_get_media_properties_async()
        playback = session.get_playback_info()
        timeline = session.get_timeline_properties()

        title = sanitize_serial_text(props.title) or "Unknown title"
        artist = sanitize_serial_text(props.artist) or sanitize_serial_text(props.album_artist) or "Unknown artist"

        # PlaybackStatus values: 4 is Playing in the Windows enum.
        playing = int(playback.playback_status) == 4

        pos = timeline.position.total_seconds()
        end = timeline.end_time.total_seconds()
        progress = int(max(0, min(100, (pos / end * 100.0) if end > 0 else 0)))

        return MediaState(title, artist, progress, playing)

    except Exception as exc:
        print("[media error]", exc)
        return None


async def read_media_cover_pixels() -> Optional[list[int]]:
    if not HAVE_WINSDK or not HAVE_PIL:
        return None

    try:
        manager = await MediaManager.request_async()
        session = manager.get_current_session()
        if not session:
            return None

        props = await session.try_get_media_properties_async()
        thumbnail = getattr(props, "thumbnail", None)
        if not thumbnail:
            return None

        stream = await thumbnail.open_read_async()
        size = int(getattr(stream, "size", 0))
        if size <= 0:
            return None

        reader = DataReader(stream.get_input_stream_at(0))
        await reader.load_async(size)
        raw = bytearray(size)
        reader.read_bytes(raw)

        image = Image.open(io.BytesIO(raw)).convert("RGB")
        side = min(image.size)
        left = (image.width - side) // 2
        top = (image.height - side) // 2
        image = image.crop((left, top, left + side, top + side))
        resample = getattr(getattr(Image, "Resampling", Image), "LANCZOS")
        image = image.resize((COVER_SIZE, COVER_SIZE), resample)

        pixels: list[int] = []
        for r, g, b in image.getdata():
            pixels.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
        return pixels

    except Exception as exc:
        print("[cover error]", exc)
        return None


def send_media_state(state: MediaState) -> None:
    write_line(
        f"MUSIC:{state.title}|{state.artist}|{state.progress}|{1 if state.playing else 0}"
    )


def send_cover_pixels(pixels: Optional[list[int]]) -> None:
    if not pixels:
        write_line("COVER:RESET")
        write_line("COVER:DONE")
        return

    write_line("COVER:RESET")
    for offset in range(0, len(pixels), COVER_CHUNK_PIXELS):
        chunk = pixels[offset:offset + COVER_CHUNK_PIXELS]
        hex_pixels = "".join(f"{p:04X}" for p in chunk)
        write_line(f"COVER:CHUNK:{offset}:{hex_pixels}")
        time.sleep(0.035)
    write_line("COVER:DONE")


def on_connected() -> None:
    write_line("ROUTE:MUSIC")
    if _last_media.title:
        send_media_state(_last_media)
    if _last_cover_pixels:
        send_cover_pixels(_last_cover_pixels)


async def control_current_media(action: str) -> bool:
    if not HAVE_WINSDK:
        print("[media control] winsdk not installed")
        return False

    try:
        manager = await MediaManager.request_async()
        session = manager.get_current_session()
        if not session:
            print("[media control] no active media session")
            return False

        if action == "previous":
            return bool(await session.try_skip_previous_async())
        if action == "next":
            return bool(await session.try_skip_next_async())
        if action == "play_pause":
            return bool(await session.try_toggle_play_pause_async())

    except Exception as exc:
        print("[media control error]", exc)
    return False


def run_media_control(action: str) -> None:
    threading.Thread(
        target=lambda: asyncio.run(control_current_media(action)),
        daemon=True
    ).start()


async def media_loop():
    global _last_cover_attempt, _last_cover_hash, _last_cover_pixels, _last_media, _last_track_identity

    if not HAVE_WINSDK:
        print("[media] winsdk not installed; media page will stay in demo mode")
        return
    if not HAVE_PIL:
        print("[media] pillow not installed; album art is disabled")

    while not stop_event.is_set():
        state = await read_media_state()

        if state:
            if state != _last_media:
                _last_media = state
                send_media_state(state)

            track_identity = f"{state.title}\0{state.artist}"
            track_changed = track_identity != _last_track_identity
            missing_art_retry = (
                state.title != "No media"
                and not _last_cover_pixels
                and time.time() - _last_cover_attempt > 5.0
            )

            if track_changed or missing_art_retry:
                _last_cover_attempt = time.time()
                _last_track_identity = track_identity
                if track_changed:
                    _last_cover_hash = ""
                    _last_cover_pixels = None

                pixels = await read_media_cover_pixels()
                packed = bytearray()
                if pixels:
                    for pixel in pixels:
                        packed.extend(pixel.to_bytes(2, "big"))
                cover_hash = hashlib.sha1(packed).hexdigest() if packed else ""

                if pixels and cover_hash != _last_cover_hash:
                    _last_cover_hash = cover_hash
                    _last_cover_pixels = pixels
                    send_cover_pixels(pixels)
                    print("[cover] album art sent")
                elif not pixels and (track_changed or time.time() - _last_cover_attempt < 1.0):
                    send_cover_pixels(None)
                    print("[cover] waiting for album art from Windows media session")

        await asyncio.sleep(1.0)


def media_thread_worker():
    asyncio.run(media_loop())


def startup_file_path() -> Path:
    startup_dir = Path(os.environ["APPDATA"]) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / "Startup"
    return startup_dir / "Deskoo.vbs"


def legacy_startup_file_path() -> Path:
    startup_dir = Path(os.environ["APPDATA"]) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / "Startup"
    return startup_dir / "ESP32 Desktop Companion.vbs"


def install_startup() -> None:
    script = Path(__file__).resolve()
    pythonw = Path(sys.executable)
    if pythonw.name.lower() == "python.exe":
        candidate = pythonw.with_name("pythonw.exe")
        if candidate.exists():
            pythonw = candidate

    target = startup_file_path()
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(
        "\n".join([
            'Set shell = CreateObject("WScript.Shell")',
            f'shell.CurrentDirectory = "{script.parent}"',
            f'shell.Run Chr(34) & "{pythonw}" & Chr(34) & " " & Chr(34) & "{script}" & Chr(34) & " --startup", 0, False',
        ]),
        encoding="utf-8"
    )

    legacy = legacy_startup_file_path()
    if legacy != target and legacy.exists():
        legacy.unlink()
    print(f"[startup] installed: {target}")


def uninstall_startup() -> None:
    target = startup_file_path()
    if target.exists():
        target.unlink()
        print(f"[startup] removed: {target}")
    else:
        print("[startup] not installed")


def parse_args():
    parser = argparse.ArgumentParser(description="Deskoo helper")
    parser.add_argument("--install-startup", action="store_true", help="launch this helper automatically at Windows sign-in")
    parser.add_argument("--uninstall-startup", action="store_true", help="remove automatic Windows startup launch")
    parser.add_argument("--startup", action="store_true", help=argparse.SUPPRESS)
    return parser.parse_args()


def main():
    args = parse_args()
    if args.install_startup:
        install_startup()
        return
    if args.uninstall_startup:
        uninstall_startup()
        return

    print("Deskoo")
    print("------")

    setup_hotkeys()

    threading.Thread(target=connection_worker, daemon=True).start()
    threading.Thread(target=serial_reader_worker, daemon=True).start()
    threading.Thread(target=media_thread_worker, daemon=True).start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        close_serial()


if __name__ == "__main__":
    main()
