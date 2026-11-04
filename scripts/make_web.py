"""make_web.py - RITA's own web page into her firmware.

Her page lives as plain files in scripts/esp32/web/ (index.html, style.css, app.js: edit those).
This writes them into scripts/esp32/web_page.h as strings her web server sends. flash.ps1 runs it
before every compile, so the page on the board is always the files in web/.

    python scripts/make_web.py
"""
from pathlib import Path

here = Path(__file__).resolve().parent
web = here / "esp32" / "web"
out = here / "esp32" / "web_page.h"
FILES = [("WEB_INDEX", "index.html"), ("WEB_STYLE", "style.css"), ("WEB_APP", "app.js")]

parts = ["// web_page.h - RITA's own web page. Written by scripts/make_web.py from scripts/esp32/web/:\n"
         "// edit the files there, not this.\n#pragma once\n#include <Arduino.h>\n\n"]
for name, fname in FILES:
    text = (web / fname).read_text(encoding="utf-8")
    assert ')=====' not in text, f"{fname} contains the raw-string end marker"
    parts.append(f"const char {name}[] PROGMEM = R\"=====({text})=====\";\n\n")
new = "".join(parts)
if not out.exists() or out.read_text(encoding="utf-8") != new:   # untouched: no rebuild
    out.write_text(new, encoding="utf-8", newline="\n")
    print(f"{out}: {sum(len((web / f).read_text(encoding='utf-8')) for _, f in FILES)} bytes")
