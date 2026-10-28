"""make_splash.py - RITA's boot picture for her screen, from images/avatar_round.png.

Puts the round avatar on her screen's background colour (C_BG in esp32.ino, 7 9 15), scales
it to SIZE pixels and writes it into esp32/splash.h as a JPEG her screen draws at boot
(LovyanGFX drawJpg). Run it again after changing the avatar:

    python scripts/make_splash.py
"""
import io
from pathlib import Path

from PIL import Image

SIZE = 240                 # px, square: the left half of her 480 x 320 screen
BG = (7, 9, 15)            # C_BG in esp32.ino
QUALITY = 85

here = Path(__file__).resolve().parent
src = here.parent / "images" / "avatar_round.png"
out = here / "esp32" / "splash.h"

avatar = Image.open(src).convert("RGBA").resize((SIZE, SIZE), Image.LANCZOS)
pic = Image.new("RGB", (SIZE, SIZE), BG)
pic.paste(avatar, mask=avatar.split()[3])   # the corners outside the circle take her background
buf = io.BytesIO()
pic.save(buf, "JPEG", quality=QUALITY, optimize=True)   # baseline: what LovyanGFX's decoder reads
data = buf.getvalue()

rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
out.write_text(
    "// splash.h - RITA's avatar for the boot screen. Written by scripts/make_splash.py from\n"
    "// images/avatar_round.png: change the picture there and run it again, don't edit this.\n"
    "#pragma once\n#include <Arduino.h>\n\n"
    f"const int SPLASH_SIZE = {SIZE};   // px, square\n"
    "const uint8_t SPLASH_JPG[] PROGMEM = {\n  " + ",\n  ".join(rows) + "\n};\n",
    encoding="utf-8", newline="\n")
print(f"{out}: {SIZE}x{SIZE} JPEG, {len(data)} bytes")
