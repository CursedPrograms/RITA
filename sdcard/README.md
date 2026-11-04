# RITA's SD card

Optional. Without a card RITA works exactly the same; with one, she can show what DREAM is showing.

**Fill it:** run `make_sdcard.bat` (or `python scripts/make_sdcard.py`; needs ffmpeg). It converts
DREAM's clips (`../DREAM/videos`) into `dream/` here: each one as `<name>.mjpeg` (480 x 270, 15 fps,
back-to-back JPEGs, since an ESP32 has no video decoder) and `<name>.idx` (where each frame starts).
The converted clips are in the repo, so the folder is ready to copy; run it again when DREAM's videos change.

**Copy it:** everything in this folder onto a **FAT32** microSD card, so the card has `/dream/` at its top.
Put it in RITA's SD slot and restart her.

**Use it:** tap DREAM on HOME. RITA asks DREAM (her mirror on port 5015) which clip she's showing
and how far in, and plays the same one from the same frame, so the two screens move together. When
DREAM speaks, RITA streams her voice to the speaker connector (a speaker or a headphone jack there);
the SOUND button turns it off.

| On the card | What it is |
|---|---|
| `dream/<clip>.mjpeg` | one of DREAM's clips (`idle0`, `talking1`, `sleeping`, ...) |
| `dream/<clip>.idx` | its frame index: `RIDX`, fps, width, height, frames, then the offsets |
