import urllib.request
import PIL.Image
import PIL.ImageDraw
import io
import os

SIZE = 28

def fetch_favicon(domain):
    url = f"https://icons.duckduckgo.com/ip3/{domain}.ico"
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    try:
        resp = urllib.request.urlopen(req)
        img = PIL.Image.open(io.BytesIO(resp.read())).convert("RGBA")
        return img.resize((SIZE, SIZE), PIL.Image.Resampling.LANCZOS)
    except Exception as e:
        print(f"Failed to fetch {domain}: {e}")
        # fallback
        return PIL.Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))

def draw_folder():
    img = PIL.Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = PIL.ImageDraw.Draw(img)
    # folder body
    d.rounded_rectangle([2, 10, SIZE-2, SIZE-4], radius=3, fill=(255, 200, 50))
    # folder tab
    d.rounded_rectangle([2, 4, 16, 12], radius=2, fill=(255, 180, 0))
    return img

def draw_notepad():
    img = PIL.Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = PIL.ImageDraw.Draw(img)
    d.rounded_rectangle([6, 2, SIZE-6, SIZE-2], radius=2, fill=(240, 240, 240))
    d.rectangle([6, 2, SIZE-6, 8], fill=(50, 150, 255))
    for y in range(12, SIZE-4, 4):
        d.line([10, y, SIZE-10, y], fill=(200, 200, 200), width=1)
    return img

def draw_lock():
    img = PIL.Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = PIL.ImageDraw.Draw(img)
    # Sleek solid white/grey lock, Windows style
    # Body
    d.rounded_rectangle([7, 12, SIZE-7, SIZE-2], radius=2, fill=(240, 240, 240))
    # Shackle (outline)
    d.arc([9, 2, SIZE-9, 20], start=180, end=0, fill=(200, 200, 200), width=3)
    d.line([9, 11, 9, 12], fill=(200, 200, 200), width=3)
    d.line([SIZE-9, 11, SIZE-9, 12], fill=(200, 200, 200), width=3)
    # Keyhole
    d.ellipse([SIZE//2-2, 16, SIZE//2+1, 19], fill=(50, 50, 50))
    d.rectangle([SIZE//2-1, 18, SIZE//2, 22], fill=(50, 50, 50))
    return img

def draw_antigravity():
    # Make a cool A logo for Antigravity
    img = PIL.Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = PIL.ImageDraw.Draw(img)
    d.ellipse([1, 1, SIZE-1, SIZE-1], fill=(20, 20, 20), outline=(100, 100, 255), width=2)
    # Draw an A
    d.line([SIZE//2, 6, 8, SIZE-8], fill=(100, 100, 255), width=4)
    d.line([SIZE//2, 6, SIZE-8, SIZE-8], fill=(100, 100, 255), width=4)
    d.line([12, SIZE//2+4, SIZE-12, SIZE//2+4], fill=(100, 100, 255), width=3)
    return img

icons = {
    "WHATSAPP": fetch_favicon("whatsapp.com"),
    "ZEN": fetch_favicon("zen-browser.app"),
    "BRAVE": fetch_favicon("brave.com"),
    "MUSIC": fetch_favicon("music.apple.com"),
    "VSCODE": fetch_favicon("code.visualstudio.com"),
    "ANTIGRAVITY": draw_antigravity(),
    "FILE_EXPLORER": draw_folder(),
    "NOTEPAD": draw_notepad(),
    "LOCK": draw_lock()
}

def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

with open("esp32_desktop_companion_v2/app_icons.h", "w") as f:
    f.write("#pragma once\n\n")
    f.write("#include <Arduino.h>\n\n")
    f.write(f"#define APP_ICON_SIZE {SIZE}\n\n")
    
    for name, img in icons.items():
        f.write(f"const uint16_t ICON_{name}[{SIZE * SIZE}] PROGMEM = {{\n")
        pixels = []
        for y in range(SIZE):
            for x in range(SIZE):
                r, g, b, a = img.getpixel((x, y))
                # For transparency, if a is low, we might want to blend with the tile background
                # But RGB565 doesn't have alpha. The tile background is either C_TILE_BG or C_TILE_SEL.
                # Actually, let's just make it a mask: we use a specific color for transparent, like 0x0000, 
                # but black is used. Let's just blend over black, and in C++ we use draw16bitRGBBitmap with a transparent color?
                # No, draw16bitRGBBitmap doesn't support transparency out of the box unless we loop or the library supports it.
                # Arduino_GFX has `draw16bitRGBBitmap(x, y, bitmap, w, h, transparent_color)`
                # Let's use 0x0001 (almost black) as transparent key.
                if a < 128:
                    px = 0x0000 # Let's assume transparent is 0x0000 and we don't draw it.
                else:
                    px = rgb565(r, g, b)
                    if px == 0:
                        px = 0x0001 # prevent using transparent key for actual black
                pixels.append(f"0x{px:04X}")
        
        # Format rows of 16 values
        for i in range(0, len(pixels), 16):
            f.write("  " + ", ".join(pixels[i:i+16]) + ",\n")
        f.write("};\n\n")

print("Generated app_icons.h")
