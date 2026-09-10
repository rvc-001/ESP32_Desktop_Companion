from PIL import Image
import os

files = {
    "RABBIT_SLEEP": "rabbit sleep.png",
    "RABBIT_MUSIC": "rabbit vibing.png",
    "RABBIT_EAT": "rabbit eat.png"
}

W = 320
H = 211
OFFSET_Y = 29

def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

with open("esp32_desktop_companion_v2/rabbit_sprites.h", "w") as f:
    f.write("// Auto-generated rabbit sprite bitmaps (RGB565)\n")
    f.write("#pragma once\n")
    f.write("#include <stdint.h>\n")
    f.write("#ifndef PROGMEM\n")
    f.write("#define PROGMEM\n")
    f.write("#endif\n\n")
    f.write(f"#define RABBIT_SPRITE_W {W}\n")
    f.write(f"#define RABBIT_SPRITE_H {H}\n")
    f.write(f"#define RABBIT_SPRITE_PIXELS ({W} * {H})\n\n")

    for name, path in files.items():
        print(f"Processing {path}...")
        img = Image.open(path).convert("RGB")
        # The source images are 4:3 (e.g. 1448x1086).
        # We resize them to 320x240.
        img = img.resize((320, 240), Image.Resampling.LANCZOS)
        # We crop the bottom 211 pixels (skip top 29 pixels).
        img = img.crop((0, OFFSET_Y, W, OFFSET_Y + H))
        
        f.write(f"static const uint16_t {name}_BMP[RABBIT_SPRITE_PIXELS] PROGMEM = {{\n")
        pixels = []
        for y in range(H):
            for x in range(W):
                r, g, b = img.getpixel((x, y))
                pixels.append(f"0x{rgb565(r, g, b):04X}")
        
        for i in range(0, len(pixels), 16):
            f.write("  " + ", ".join(pixels[i:i+16]) + ",\n")
        f.write("};\n\n")

print("Done generating rabbit_sprites.h")
