"""make_avatars.py - the fleet's round avatars for RITA's home screen, from each robot's repo.

Every robot keeps images/avatar_round.png in its own repo (next to RITA's in the DREAM Robotics
Ecosystem folder). This puts each on her screen's background colour (C_BG in esp32.ino), scales it
to SIZE pixels and writes them all into esp32/avatars.h as JPEGs her screen draws (LovyanGFX
drawJpg). Run it again when an avatar changes or a robot joins her roster (ROSTER in esp32.ino):

    python scripts/make_avatars.py
"""
import io
from pathlib import Path

from PIL import Image

SIZE = 84                  # px, square: one circle in her 5 x 2 home grid
BG = (7, 9, 15)            # C_BG in esp32.ino
QUALITY = 85

# name in the fleet list -> its repo (the order is her home grid's)
ROBOTS = [
    ("WHIP", "WHIP-Robot-v00"), ("NORA", "NORA-Robot-v00"), ("MILA", "MILA-Robot-v00"),
    ("NIKA", "NIKA-Robot-v00"), ("KIDA00", "KIDA-Robot-v00"), ("KIDA01", "KIDA-Robot-v01"),
    ("IDA", "IDA-Robot-v00"), ("ARM", "ARM-Robot-v01"), ("ARIA", "ARIA-Robot-v00"),
]

here = Path(__file__).resolve().parent
fleet = here.parent.parent
out = here / "esp32" / "avatars.h"


def jpeg(path):
    img = Image.open(path).convert("RGBA").resize((SIZE, SIZE), Image.LANCZOS)
    pic = Image.new("RGB", (SIZE, SIZE), BG)
    pic.paste(img, mask=img.split()[3])   # outside the circle: her background
    buf = io.BytesIO()
    pic.save(buf, "JPEG", quality=QUALITY, optimize=True)
    return buf.getvalue()


parts = ["// avatars.h - the fleet's round avatars for RITA's home screen. Written by scripts/make_avatars.py\n"
         "// from each robot's images/avatar_round.png: change them there and run it again, don't edit this.\n"
         "#pragma once\n#include <Arduino.h>\n\n"
         f"const int AVATAR_SIZE = {SIZE};   // px, square\n\n"]
table = []
total = 0
for name, repo in ROBOTS:
    if not (fleet / repo / "images" / "avatar_round.png").exists():   # no picture yet: a plain circle on her home screen
        continue
    data = jpeg(fleet / repo / "images" / "avatar_round.png")
    total += len(data)
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    parts.append(f"const uint8_t AV_{name}[] PROGMEM = {{   // {repo}/images/avatar_round.png\n  "
                 + ",\n  ".join(rows) + "\n};\n\n")
    table.append(f'  {{ "{name}", AV_{name}, sizeof(AV_{name}) }},')
parts.append("struct Avatar { const char* name; const uint8_t* jpg; size_t len; };\n"
             "const Avatar AVATARS[] = {\n" + "\n".join(table) + "\n};\n"
             "const int AVATAR_COUNT = sizeof(AVATARS) / sizeof(AVATARS[0]);\n")
out.write_text("".join(parts), encoding="utf-8", newline="\n")
print(f"{out}: {len(ROBOTS)} avatars, {SIZE}x{SIZE}, {total} bytes")
