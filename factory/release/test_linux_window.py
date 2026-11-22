"""Exercise the same X11 requests as the packaged editor smoke test."""
import importlib.util
import os
from pathlib import Path
import sys
import unittest


@unittest.skipUnless(sys.platform.startswith("linux") and os.environ.get("DISPLAY"), "needs an X11 display")
class EditorWindowTests(unittest.TestCase):
    def test_a_destroyed_client_does_not_abort_window_discovery(self):
        from Xlib import X, display
        script = Path(__file__).with_name("standalone-linux.py")
        spec = importlib.util.spec_from_file_location("standalone_linux", script)
        verifier = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(verifier)
        connection = display.Display()
        self.addCleanup(connection.close)
        root = connection.screen().root
        window = root.create_window(0, 0, 320, 240, 0, X.CopyFromParent)
        window.destroy()
        connection.sync()
        atoms = {"_NET_WM_PID": connection.intern_atom("_NET_WM_PID")}
        self.assertFalse(verifier.editor_paints(root, window, os.getpid(), b"Pole Field", atoms))
