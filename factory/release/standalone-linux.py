"""Verify that a packaged standalone paints its instrument in a visible X11 window."""
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from PIL import ImageGrab


def editor_paints(window):
    geometry = subprocess.run(
        ["xdotool", "getwindowgeometry", "--shell", window], capture_output=True, text=True,
    )
    if geometry.returncode and ("BadWindow" in geometry.stderr or "BadDrawable" in geometry.stderr):
        return False  # A temporary renderer window closed during enumeration.
    geometry.check_returncode()
    bounds = dict(line.split("=", 1) for line in geometry.stdout.splitlines())
    x, y, width, height = (int(bounds[key]) for key in ("X", "Y", "WIDTH", "HEIGHT"))
    if width < 200 or height < 100:
        return False
    # The title bar paints even when the instrument itself has no working renderer.
    region = (x + width // 4, y + height // 4, x + 3 * width // 4, y + 3 * height // 4)
    image = ImageGrab.grab(bbox=region, xdisplay=os.environ["DISPLAY"]).convert("L")
    return image.getcolors(16) is None


def verify(executable):
    deadline = time.monotonic() + 30
    with subprocess.Popen([executable]) as app:
        try:
            search = ["xdotool", "search", "--onlyvisible", "--all", "--pid", str(app.pid),
                      "--name", "^" + re.escape(executable.name) + "$"]
            while app.poll() is None and time.monotonic() < deadline:
                windows = subprocess.run(search, capture_output=True, text=True)
                # xdotool returns 1 with no output when no window matches.
                if windows.returncode and (windows.returncode != 1 or windows.stderr.strip()):
                    windows.check_returncode()
                if any(editor_paints(window) for window in windows.stdout.splitlines()):
                    print(f"{executable.name} opened and painted its editor")
                    return
                time.sleep(0.1)
            raise RuntimeError(f"{executable.name} did not paint its editor")
        finally:
            if app.poll() is None:
                app.terminate()


if __name__ == "__main__":
    verify(Path(sys.argv[1]).resolve())
