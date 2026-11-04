"""make_sdcard.py - fill RITA's sdcard/ folder: DREAM's clips, the way RITA's ESP32 can play them.

    python scripts/make_sdcard.py          (or make_sdcard.bat)

Reads DREAM's videos (../DREAM/videos, next to RITA in the DREAM Robotics Ecosystem folder):
the clips her page plays for each state (idle*, listening*, thinking*, talking*, sleeping*,
flirtytalk*, intro*). Each becomes, in sdcard/dream/:

    <name>.mjpeg   480 x 270 at 15 fps, back-to-back JPEGs (no video decoder on an ESP32: it
                   draws JPEGs, so a video is a stack of them)
    <name>.idx     where each frame starts: "RIDX", fps, width, height (u16), frames (u32),
                   then frames + 1 offsets (u32, little-endian). RITA uses it to jump straight
                   to the frame DREAM is showing, so the two stay in step

Then copy everything in sdcard/ onto a FAT32 microSD card (the dream folder at the card's top)
and put it in RITA. Needs ffmpeg. Clips already converted are skipped unless the video is newer.
"""
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

W, H, FPS, QUALITY = 480, 270, 15, 8     # QUALITY: ffmpeg's -q:v, 2 (best) .. 31; 8 keeps frames ~15-30 KB
MAX_FRAME = 48 * 1024                    # dream_mirror.h's frame buffer
STATES = re.compile(r"^(idle|listening|thinking|talking|sleeping|flirtytalk|intro)\d*\.mp4$", re.I)

here = Path(__file__).resolve().parent
repo = here.parent
videos = repo.parent / "DREAM" / "videos"
out = repo / "sdcard" / "dream"


def index(mjpeg):
    data = mjpeg.read_bytes()
    starts, i = [], data.find(b"\xff\xd8\xff")
    while i >= 0:
        starts.append(i)
        i = data.find(b"\xff\xd8\xff", i + 3)
    offs = starts + [len(data)]
    sizes = [b - a for a, b in zip(offs, offs[1:])]
    head = b"RIDX" + struct.pack("<HHHI", FPS, W, H, len(starts))
    return head + struct.pack(f"<{len(offs)}I", *offs), len(starts), max(sizes or [0])


def main():
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        sys.exit("ffmpeg isn't on PATH: install it (https://ffmpeg.org) and run this again.")
    if not videos.is_dir():
        sys.exit(f"DREAM's videos aren't at {videos}")
    out.mkdir(parents=True, exist_ok=True)
    total = 0
    for src in sorted(p for p in videos.iterdir() if STATES.match(p.name)):
        mj, ix = out / (src.stem + ".mjpeg"), out / (src.stem + ".idx")
        if ix.exists() and mj.exists() and mj.stat().st_mtime >= src.stat().st_mtime:
            total += mj.stat().st_size
            print(f"  {src.stem}: up to date")
            continue
        # scale to fill 480 x 270 (16:9), 15 fps, no sound (her voice comes from DREAM as she speaks)
        subprocess.run([ffmpeg, "-v", "error", "-y", "-i", str(src), "-an",
                        "-vf", f"scale={W}:{H}:force_original_aspect_ratio=increase,crop={W}:{H},fps={FPS}",
                        "-q:v", str(QUALITY), "-pix_fmt", "yuvj420p", "-f", "mjpeg", str(mj)], check=True)
        table, n, biggest = index(mj)
        ix.write_bytes(table)
        total += mj.stat().st_size
        warn = f"  (a frame is {biggest // 1024} KB: over RITA's {MAX_FRAME // 1024} KB, raise QUALITY)" if biggest > MAX_FRAME else ""
        print(f"  {src.stem}: {n} frames, {n / FPS:.1f} s, {mj.stat().st_size / 1e6:.1f} MB{warn}")
    print(f"\n{out}: {total / 1e6:.0f} MB. Copy everything in sdcard/ onto a FAT32 microSD card.")


if __name__ == "__main__":
    main()
