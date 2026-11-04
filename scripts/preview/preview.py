#!/usr/bin/env python3
"""preview.py - RITA's screens on the PC, no board needed. Her real esp32.ino, drawn by the real LovyanGFX.

    python scripts/preview/preview.py           a window: the mouse is your finger
    python scripts/preview/preview.py --shots   writes images/screens/*.png

Needs g++ and SDL2 (Windows: MSYS2's mingw64, pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2),
Pillow for --shots, and the libraries flash.bat installs (LovyanGFX, ArduinoJson).
"""
import os
import pathlib
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
BUILD = REPO / "build" / "preview"
SCREENS = REPO / "images" / "screens"
EXE = BUILD / ("rita_preview.exe" if os.name == "nt" else "rita_preview")
SECTIONS = ["-ffunction-sections", "-fdata-sections"]   # so the link can drop what she never uses (the CJK fonts)
FLAGS = ["-std=gnu++17", "-O1", "-w", *SECTIONS, "-DARDUINOJSON_ENABLE_ARDUINO_STRING=1",
         f"-DRITA_SDCARD=\"{(REPO / 'sdcard').as_posix()}\""]   # her SD card: the repo's sdcard/ folder


def compiler():
    """g++ and the environment it runs in. MSYS2's mingw64 first: it's the one with SDL2."""
    env = dict(os.environ)
    if os.name == "nt":
        mingw = pathlib.Path(r"C:\msys64\mingw64\bin")
        if (mingw / "g++.exe").exists():
            env["PATH"] = f"{mingw}{os.pathsep}{env['PATH']}"
            return str(mingw / "g++.exe"), env
    gpp = shutil.which("g++")
    if not gpp:
        raise SystemExit("No g++ found. See the top of preview.py.")
    return gpp, env


def libraries():
    """Where flash.bat put the Arduino libraries."""
    home = pathlib.Path.home()
    tries = [home / "Documents" / "Arduino" / "libraries", home / "Arduino" / "libraries"]
    if os.environ.get("LOCALAPPDATA"):
        tries.append(pathlib.Path(os.environ["LOCALAPPDATA"]) / "Arduino15" / "user" / "libraries")
    for lib in tries:
        if (lib / "LovyanGFX").exists() and (lib / "ArduinoJson").exists():
            return lib
    raise SystemExit("LovyanGFX and ArduinoJson aren't installed: run flash.bat -CompileOnly once.")


def build():
    gpp, env = compiler()
    lib = libraries()
    lovyan = lib / "LovyanGFX" / "src"
    BUILD.mkdir(parents=True, exist_ok=True)
    includes = [f"-I{BUILD}", f"-I{HERE / 'shim'}", f"-I{REPO / 'scripts' / 'esp32'}", f"-I{lovyan}",
                f"-I{lib / 'ArduinoJson' / 'src'}"]

    def compile_lovyan(source):
        obj = BUILD / "lgfx" / (str(source.relative_to(lovyan)).replace(os.sep, "_") + ".o")
        if obj.exists():
            return obj
        obj.parent.mkdir(exist_ok=True)
        tool = gpp if source.suffix == ".cpp" else gpp.replace("g++", "gcc")
        flags = FLAGS if source.suffix == ".cpp" else ["-O1", "-w", *SECTIONS]
        got = subprocess.run([tool, *flags, *includes, "-c", str(source), "-o", str(obj)],
                             env=env, capture_output=True, text=True)
        if got.returncode:
            raise SystemExit(f"{source}\n{got.stderr}")
        return obj

    sources = [p for p in (lovyan / "lgfx").rglob("*") if p.suffix in (".cpp", ".c")]
    print(f"LovyanGFX: {len(sources)} files (only the first build compiles them)")
    with ThreadPoolExecutor(os.cpu_count()) as pool:
        objects = list(pool.map(compile_lovyan, sources))

    shutil.copyfile(REPO / "scripts" / "esp32" / "esp32.ino", BUILD / "sketch.inc")
    libs = ["-lSDL2"] + (["-lws2_32", "-lwinmm"] if os.name == "nt" else ["-lpthread"])
    subprocess.run([gpp, *FLAGS, *includes, str(HERE / "main.cpp"), *map(str, objects), *libs, "-Wl,--gc-sections", "-s", "-o", str(EXE)],
                   env=env, check=True)
    return env


def take_shots(env):
    from PIL import Image

    raw = BUILD / "shots"
    shutil.rmtree(raw, ignore_errors=True)
    raw.mkdir()
    SCREENS.mkdir(exist_ok=True)
    subprocess.run([str(EXE), "--shots", str(raw)], env=env, check=True)
    for ppm in sorted(raw.glob("*.ppm")):
        Image.open(ppm).save(SCREENS / f"{ppm.stem}.png", optimize=True)
    print("written to", SCREENS)


def main():
    env = build()
    if "--shots" in sys.argv:
        take_shots(env)
    else:
        subprocess.run([str(EXE)], env=env)


if __name__ == "__main__":
    main()
