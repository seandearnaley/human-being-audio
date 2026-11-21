#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds Livecut Redux's macOS installer from a Release build's artefacts.
#
#   make-installer.sh <artefacts> <version> <output> --sign <application identity> <installer identity>
#   make-installer.sh <artefacts> <version> <output> --unsigned
#
# <artefacts> is build/Livecut_artefacts/Release. The identities are the full
# names of your "Developer ID Application" and "Developer ID Installer"
# certificates. --unsigned is for checking the installer's layout locally; an
# unsigned installer can't be notarised and Gatekeeper will refuse it.
#
# The installer puts the VST3 and AU in /Library/Audio/Plug-Ins and the
# standalone app in /Applications. Signing uses the hardened runtime and a
# secure timestamp, both of which notarisation requires.
#
# Before it installs, scripts/preinstall removes Livecut 0.1.0 to 1.0.0-beta.1
# from those places, since Livecut Redux has the same plug-in IDs.
set -euo pipefail
export PRODUCT=livecut-redux NAME="Livecut Redux" BUNDLE=com.humanbeingmusic.livecutredux
exec bash "$(dirname "$0")/../../../../factory/release/macos.sh" "$@"
