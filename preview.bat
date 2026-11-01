@echo off
REM RITA's screens on the PC, no board needed.
REM   preview.bat           a window: the mouse is your finger
REM   preview.bat --shots   writes images\screens\*.png
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\preview.ps1" %*
if errorlevel 1 pause
