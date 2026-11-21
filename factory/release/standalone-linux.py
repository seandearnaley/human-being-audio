"""Open a packaged standalone in X11 and verify its editor paints inside the window."""
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from PIL import ImageGrab


def verify(executable):
    deadline = time.monotonic() + 30
    with subprocess.Popen([executable]) as app:
        try:
            ids = subprocess.check_output(
                ["xdotool", "search", "--sync", "--onlyvisible", "--all",
                 "--pid", str(app.pid), "--name", "^" + re.escape(executable.name) + "$"],
                timeout=deadline - time.monotonic(), text=True,
            )
            window, = ids.splitlines()
            geometry = subprocess.check_output(
                ["xdotool", "getwindowgeometry", "--shell", window], text=True,
            )
            bounds = dict(line.split("=", 1) for line in geometry.splitlines())
            x, y, width, height = (int(bounds[key]) for key in ("X", "Y", "WIDTH", "HEIGHT"))
            if width < 200 or height < 100:
                raise RuntimeError(f"Unexpected {executable.name} window size: {width} x {height}")
            # Sample the editor interior. The JUCE title bar also paints on a blank editor.
            region = (x + width // 4, y + height // 4, x + 3 * width // 4, y + 3 * height // 4)
            while app.poll() is None and time.monotonic() < deadline:
                image = ImageGrab.grab(bbox=region, xdisplay=os.environ["DISPLAY"]).convert("L")
                if image.getcolors(16) is None:
                    print(f"{executable.name} opened and painted its editor at {width} x {height}")
                    return
                time.sleep(0.1)
            raise RuntimeError(f"{executable.name} did not paint its editor")
        finally:
            if app.poll() is None:
                app.terminate()


if __name__ == "__main__":
    verify(Path(sys.argv[1]).resolve())
