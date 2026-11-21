#!/bin/sh
# Run inside Xvfb. Openbox announces readiness after advertising its X11 properties.
set -u
openbox --startup 'echo HBA_WINDOW_MANAGER_READY' | {
    while IFS= read -r line; do
        if [ "$line" = HBA_WINDOW_MANAGER_READY ]; then
            result=0
            "$@" || result=$?
            openbox --exit
            exit "$result"
        fi
    done
    echo 'Openbox exited before the X11 desktop was ready' >&2
    exit 1
}
