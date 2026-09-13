#!/usr/bin/env bash
#
# Open the asset pipeline window.
#
#   tools/asset_gui.sh
#
# Finds a Python that can actually draw a window and uses that one. The GUI is
# Tkinter, and Tkinter is not part of Python: it is a separate package that a
# given interpreter either was built against or was not. On macOS the Homebrew
# pythons usually are not and /usr/bin/python3 usually is, so `python3` - which
# is whichever the PATH reaches first - fails on a machine that has a perfectly
# good interpreter for this a directory away. That looked like the program not
# existing.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI="${SCRIPT_DIR}/asset_pipeline_gui.py"

for candidate in python3 /usr/bin/python3 python3.13 python3.12 python3.11 python3.10 python3.9 python; do
    if command -v "$candidate" >/dev/null 2>&1 &&
       "$candidate" -c "import tkinter" >/dev/null 2>&1; then
        exec "$candidate" "$GUI" "$@"
    fi
done

echo "No Python here can draw a window: none of the ones on PATH have Tkinter." >&2
echo "" >&2
echo "  macOS:          brew install python-tk        (or use /usr/bin/python3)" >&2
echo "  Debian/Ubuntu:  sudo apt install python3-tk" >&2
echo "  Fedora:         sudo dnf install python3-tkinter" >&2
echo "" >&2
echo "Everything the window does can also be done from the terminal:" >&2
echo "  ./extract_assets.sh /path/to/WoW/Data" >&2
exit 1
