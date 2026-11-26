"""Check the installed standalone's native editor against the actual JUCE client area."""
import ctypes
from ctypes.wintypes import HWND, POINT, RECT
import sys


def verify(parent):
    user = ctypes.windll.user32
    user.GetWindow.restype = HWND
    client = RECT()
    if not user.GetClientRect(parent, ctypes.byref(client)):
        raise ctypes.WinError()
    # The JUCE standalone hosts one instrument below its title and audio settings.
    child = user.GetWindow(parent, 5)  # GW_CHILD
    while child:
        handle = HWND(child)
        bounds = RECT()
        if not user.GetWindowRect(handle, ctypes.byref(bounds)):
            raise ctypes.WinError()
        if user.IsWindowVisible(handle) and bounds.right - bounds.left > client.right / 2:
            origin = POINT()
            user.MapWindowPoints(handle, parent, ctypes.byref(origin), 1)
            if origin.y <= 0:
                raise ValueError(f"Native editor overlaps the standalone title bar at y={origin.y}")
            print(f"Native instrument begins below the standalone controls at y={origin.y}")
            return
        child = user.GetWindow(handle, 2)  # GW_HWNDNEXT
    raise ValueError("Could not find the visible native instrument")


if __name__ == "__main__":
    verify(HWND(int(sys.argv[1])))
