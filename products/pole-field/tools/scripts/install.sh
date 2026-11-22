#!/bin/sh
# Installs a built Pole Field into the user's plug-in folders: copy, re-sign,
# then reset AudioComponentRegistrar so Live 12 / AU hosts pick it up cleanly.
#
#   tools/scripts/install.sh [build/dev]
set -eu

build="${1:-build/dev}"
artefacts="$build/plugin/PoleField_artefacts"
config="$(ls "$artefacts" | grep -E '^(Release|RelWithDebInfo|Debug)$' | head -1)"
source="$artefacts/$config"
vst3="$HOME/Library/Audio/Plug-Ins/VST3/Pole Field.vst3"
au="$HOME/Library/Audio/Plug-Ins/Components/Pole Field.component"

[ -d "$source/VST3/Pole Field.vst3" ] || { echo "no build found under $source" >&2; exit 1; }

mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"
rm -rf "$vst3" "$au"
cp -R "$source/VST3/Pole Field.vst3" "$vst3"
cp -R "$source/AU/Pole Field.component" "$au"

for bundle in "$vst3" "$au"; do
    codesign --force --deep -s - "$bundle" 2>/dev/null
    codesign -v "$bundle"
done

killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "installed $config build from $build"
echo "  $vst3"
echo "  $au"
echo "check with:"
echo "  auval -v aumf Plfd Hbmu"
echo "  /Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate-in-process --validate \"$vst3\""
