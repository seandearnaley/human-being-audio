#!/usr/bin/env bash
set -euo pipefail
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
case "$RUNNER_OS" in
  macOS) bin=pluginval.app/Contents/MacOS/pluginval; digest=3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f ;;
  Windows) bin=pluginval.exe; digest=c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab ;;
  Linux) bin=pluginval; digest=c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352 ;;
  *) exit 2 ;;
esac
curl -fsSL "https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_${RUNNER_OS}.zip" -o "$dir/tool.zip"
if [[ $RUNNER_OS == macOS ]]; then checksum=(shasum -a 256); else checksum=(sha256sum); fi
(cd "$dir" && printf '%s  tool.zip\n' "$digest" | "${checksum[@]}" -c - && unzip -q tool.zip)
"$dir/$bin" --strictness-level 5 --validate "$1"
if [[ $RUNNER_OS == macOS ]]; then
  "$dir/$bin" --strictness-level 5 --validate "/Library/Audio/Plug-Ins/Components/$NAME.component"
fi
