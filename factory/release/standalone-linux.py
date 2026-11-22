"""Verify that a packaged standalone paints its instrument in a visible X11 window."""
import os
from pathlib import Path
import subprocess
import sys
import time

from PIL import ImageGrab
from Xlib import X, Xatom, display, error


def editor_paints(root, window, pid, name, atoms):
    try:
        owner = window.get_full_property(atoms["_NET_WM_PID"], Xatom.CARDINAL)
        if owner is None or list(owner.value) != [pid]:
            return False
        title = window.get_full_property(Xatom.WM_NAME, X.AnyPropertyType)
        if title is None or title.value != name or window.get_attributes().map_state != X.IsViewable:
            return False
        size = window.get_geometry()
        origin = root.translate_coords(window, 0, 0)
    except (error.BadWindow, error.BadDrawable):
        # A client may close between the manager's snapshot and the property request.
        return False
    w, h = size.width, size.height
    if w < 200 or h < 100:
        return False
    # Exclude the title bar, which paints even if the instrument renderer fails.
    region = (origin.x + w // 4, origin.y + h // 4, origin.x + 3 * w // 4, origin.y + 3 * h // 4)
    return ImageGrab.grab(bbox=region, xdisplay=os.environ["DISPLAY"]).convert("L").getcolors(16) is None


def verify(executable):
    deadline = time.monotonic() + 30
    screen = display.Display()
    root = screen.screen().root
    atoms = {name: screen.intern_atom(name) for name in ("_NET_CLIENT_LIST", "_NET_WM_PID")}
    try:
        with subprocess.Popen([executable]) as app:
            try:
                while app.poll() is None and time.monotonic() < deadline:
                    clients = root.get_full_property(atoms["_NET_CLIENT_LIST"], Xatom.WINDOW)
                    if clients is not None:
                        for xid in clients.value:
                            window = screen.create_resource_object("window", xid)
                            if editor_paints(root, window, app.pid, executable.name.encode(), atoms):
                                print(f"{executable.name} opened and painted its editor")
                                return
                    time.sleep(0.1)
                raise RuntimeError(f"{executable.name} did not paint its editor")
            finally:
                if app.poll() is None:
                    app.terminate()
    finally:
        screen.close()


if __name__ == "__main__":
    verify(Path(sys.argv[1]).resolve())
