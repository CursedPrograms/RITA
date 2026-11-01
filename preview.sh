#!/bin/sh
# RITA's screens on the PC, no board needed.
#   ./preview.sh           a window: the mouse is your finger
#   ./preview.sh --shots   writes images/screens/*.png
exec python3 "$(dirname "$0")/scripts/preview/preview.py" "$@"
