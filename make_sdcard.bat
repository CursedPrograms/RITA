@echo off
REM Fill sdcard\ with DREAM's clips for RITA (needs ffmpeg), then copy sdcard\ onto a FAT32 microSD card.
python "%~dp0scripts\make_sdcard.py" %*
if errorlevel 1 pause
