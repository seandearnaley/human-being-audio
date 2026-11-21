#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

usage() {
    echo "Usage: $0 ARTIFACTS VERSION OUTPUT --sign APPLICATION_ID INSTALLER_ID | --unsigned" >&2
    exit 64
}

[[ $# -ge 4 ]] || usage
artefacts=$1
version=$2
output=$3
mode=$4

case "$mode" in
    --sign)
        [[ $# -eq 6 ]] || usage
        application_identity=$5
        installer_identity=$6
        ;;
    --unsigned)
        [[ $# -eq 4 ]] || usage
        ;;
    *)
        usage
        ;;
esac

repo=$(cd "$(dirname "$0")/../.." && pwd)
product_dir="$repo/products/$PRODUCT"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Packages one bundle so it installs at <location>, signing it first unless
# --unsigned. Any further arguments go to pkgbuild.
package() {
    local bundle=$1 location=$2 name=$3 entitlements=$4
    shift 4
    local root="$work/root-$name"
    local staged="$root/$(basename "$bundle")"

    # pkgbuild archives extended attributes and Installer restores them, so stage
    # a copy without them; a quarantine flag would make Gatekeeper block the plug-in.
    ditto --noextattr --noqtn "$bundle" "$staged"

    local slices
    slices=" $(lipo -archs "$staged/Contents/MacOS/$NAME") "
    [[ $slices == *" arm64 "* && $slices == *" x86_64 "* ]] || { echo "Missing universal slice: $staged" >&2; exit 1; }
    if [[ $mode == --sign ]]; then
        local options=(--force --timestamp --options runtime --sign "$application_identity")
        [[ -n $entitlements ]] && options+=(--entitlements "$entitlements")
        codesign "${options[@]}" "$staged"
        codesign --verify --strict --verbose=2 "$staged"
    fi

    # Plug-ins and the app must install where the installer says, never be
    # "relocated" to wherever an older copy happens to live.
    pkgbuild --analyze --root "$root" "$work/$name.plist"
    plutil -replace 0.BundleIsRelocatable -bool NO "$work/$name.plist"

    pkgbuild --root "$root" \
             --component-plist "$work/$name.plist" \
             --install-location "$location" \
             --identifier "$BUNDLE.$name" \
             --version "$version" \
             "$@" \
             "$work/packages/$name.pkg"
}

mkdir -p "$work/packages" "$output"
# The installer always installs all three packages, so the first one carries the
# script that removes old copies of each.
scripts=()
entitlements=""
if [[ $PRODUCT == livecut-redux ]]; then
    scripts=(--scripts "$product_dir/packaging/macos/scripts")
    entitlements="$product_dir/packaging/macos/Standalone.entitlements"
fi
package "$artefacts/VST3/$NAME.vst3" "/Library/Audio/Plug-Ins/VST3" vst3 "" ${scripts[@]+"${scripts[@]}"}
package "$artefacts/AU/$NAME.component" "/Library/Audio/Plug-Ins/Components" au ""
package "$artefacts/Standalone/$NAME.app" "/Applications" app "$entitlements"
mkdir "$work/notices"
cp "$product_dir/"{LICENSE,README.md} "$work/notices/"
pkgbuild --root "$work/notices" --identifier "$BUNDLE.notices" --version "$version" \
    --install-location "/Library/Application Support/Human Being Audio/$NAME" "$work/packages/notices.pkg"

productbuild --synthesize \
    --package "$work/packages/vst3.pkg" \
    --package "$work/packages/au.pkg" \
    --package "$work/packages/app.pkg" \
    --package "$work/packages/notices.pkg" \
    "$work/distribution.xml"

installer="$output/${NAME// /-}-$version.pkg"
product_options=(--distribution "$work/distribution.xml" --package-path "$work/packages")
[[ $mode == --sign ]] && product_options+=(--sign "$installer_identity" --timestamp)
productbuild "${product_options[@]}" "$installer"

echo "Built $installer"
