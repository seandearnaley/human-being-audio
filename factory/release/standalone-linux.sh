#!/usr/bin/env bash
set -euo pipefail
"$1" &
pid=$!
trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
# Wait for X11 to map a visible window owned by this exact executable.
timeout 30s xdotool search --sync --onlyvisible --pid "$pid" >/dev/null
