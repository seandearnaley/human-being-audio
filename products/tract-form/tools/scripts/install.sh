#!/bin/sh
# Installs a built Tract Form into the user's plug-in folders: copy, re-sign,
# then check the result. Development builds do not install themselves, so what
# is in the plug-in folder is always a build someone chose to put there.
#
#   tools/scripts/install.sh [build/dev]
set -eu

build="${1:-build/dev}"
artefacts="$build/plugin/TractForm_artefacts"
config="$(ls "$artefacts" | grep -E '^(Release|RelWithDebInfo|Debug)$' | head -1)"
source="$artefacts/$config"
vst3="$HOME/Library/Audio/Plug-Ins/VST3/Tract Form.vst3"
au="$HOME/Library/Audio/Plug-Ins/Components/Tract Form.component"

[ -d "$source/VST3/Tract Form.vst3" ] || { echo "no build found under $source" >&2; exit 1; }

rm -rf "$vst3" "$au"
cp -R "$source/VST3/Tract Form.vst3" "$vst3"
cp -R "$source/AU/Tract Form.component" "$au"

# A copied bundle's signature no longer matches. Apple Silicon will not load
# unsigned code, so sign ad hoc again and verify.
for bundle in "$vst3" "$au"; do
    codesign --force --deep -s - "$bundle" 2>/dev/null
    codesign -v "$bundle"
done

# Make the system forget the previous Audio Unit.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "installed $config build from $build"
echo "  $vst3"
echo "  $au"
echo "check with:"
echo "  auval -v aumu Trfm Hbmu"
echo "  /Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate-in-process --validate \"$vst3\""
