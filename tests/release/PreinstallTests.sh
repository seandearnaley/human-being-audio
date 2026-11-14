#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs the macOS installer's preinstall script against throwaway bundles in a
# temporary volume, never against the real /Library or /Applications.
set -euo pipefail

preinstall="$(cd "$(dirname "$0")/../.." && pwd)/packaging/macos/scripts/preinstall"
volume=$(mktemp -d)
trap 'rm -rf "$volume"' EXIT

vst3="$volume/Library/Audio/Plug-Ins/VST3"
components="$volume/Library/Audio/Plug-Ins/Components"
applications="$volume/Applications"
mkdir -p "$vst3" "$components" "$applications"

# Makes a bundle at <path> whose Info.plist has <identifier>, or no identifier if it is empty.
make_bundle() {
    local path=$1 identifier=$2
    mkdir -p "$path/Contents"
    /usr/libexec/PlistBuddy -c "Add :CFBundleName string Livecut" "$path/Contents/Info.plist" > /dev/null
    if [[ -n $identifier ]]; then
        /usr/libexec/PlistBuddy -c "Add :CFBundleIdentifier string $identifier" "$path/Contents/Info.plist" > /dev/null
    fi
}

failures=0
expect() {
    local state=$1 path=$2
    if [[ $state == removed && -e $path ]] || [[ $state == kept && ! -e $path ]]; then
        echo "FAILED: expected $path to be $state" >&2
        failures=1
    fi
}

# Installer passes the package, the destination and the volume, which may end in a slash.
run_preinstall() {
    "$preinstall" "$volume/Livecut-Redux.pkg" "$volume/" "$volume/" > /dev/null
}

# Nothing to remove.
run_preinstall

# Livecut 0.1.0 to 1.0.0-beta.1 go, at all three locations.
make_bundle "$vst3/Livecut.vst3" com.seandearnaley.livecut
make_bundle "$components/Livecut.component" com.seandearnaley.livecut
make_bundle "$applications/Livecut.app" com.seandearnaley.livecut
make_bundle "$vst3/Livecut Redux.vst3" com.humanbeingmusic.livecutredux
run_preinstall
expect removed "$vst3/Livecut.vst3"
expect removed "$components/Livecut.component"
expect removed "$applications/Livecut.app"
expect kept "$vst3/Livecut Redux.vst3"

# Anything else called Livecut stays: the original 2004 Livecut, a bundle with
# no identifier, and identifiers that only begin or end like this project's.
make_bundle "$components/Livecut.component" com.mdsp.livecut
make_bundle "$vst3/Livecut.vst3" ""
make_bundle "$applications/Livecut.app" com.seandearnaley.livecut.app
run_preinstall
expect kept "$components/Livecut.component"
expect kept "$vst3/Livecut.vst3"
expect kept "$applications/Livecut.app"

rm -rf "$applications/Livecut.app"
make_bundle "$applications/Livecut.app" xcom.seandearnaley.livecut
run_preinstall
expect kept "$applications/Livecut.app"

if [[ $failures -ne 0 ]]; then
    exit 1
fi
echo "Preinstall checks passed"
