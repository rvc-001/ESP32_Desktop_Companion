from PIL import Image
import os

files = {
    "RABBIT_SLEEP": {"file": "rabbit sleep.png", "shift_down": 0},
    "RABBIT_MUSIC": {"file": "rabbit vibing.png", "shift_down": 25},
    "RABBIT_EAT": {"file": "rabbit eat.png", "shift_down": 25}
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

    for name, data in files.items():
        path = data["file"]
        shift_down = data["shift_down"]
        print(f"Processing {path}...")
        img = Image.open(path).convert("RGB")
        img = img.resize((320, 240), Image.Resampling.LANCZOS)
        
        # To shift the image DOWN on the screen, we must crop from a HIGHER point in the original image.
        crop_top = OFFSET_Y - shift_down
        img = img.crop((0, crop_top, W, crop_top + H))
        
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
