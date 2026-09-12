from PIL import Image
import os

W = 320
H = 240

def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

with open("esp32_desktop_companion_v2/boot_screen.h", "w") as f:
    f.write("// Auto-generated boot screen bitmap (RGB565)\n")
    f.write("#pragma once\n")
    f.write("#include <stdint.h>\n")
    f.write("#ifndef PROGMEM\n")
    f.write("#define PROGMEM\n")
    f.write("#endif\n\n")
    f.write(f"#define BOOT_SCREEN_W {W}\n")
    f.write(f"#define BOOT_SCREEN_H {H}\n")
    f.write(f"#define BOOT_SCREEN_PIXELS ({W} * {H})\n\n")

    img = Image.open("boot_screen.png").convert("RGB")
    img = img.resize((320, 240), Image.Resampling.LANCZOS)
    
    f.write(f"static const uint16_t BOOT_SCREEN_BMP[BOOT_SCREEN_PIXELS] PROGMEM = {{\n")
    pixels = []
    for y in range(H):
        for x in range(W):
            r, g, b = img.getpixel((x, y))
            pixels.append(f"0x{rgb565(r, g, b):04X}")
    
    for i in range(0, len(pixels), 16):
        f.write("  " + ", ".join(pixels[i:i+16]) + ",\n")
    f.write("};\n\n")

print("Done generating boot_screen.h")
