#!/usr/bin/env bash
set -euo pipefail
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
root="$stage/$NAME"
mkdir -p "$root/VST3" "$root/Standalone" dist
cp -a "$ARTIFACTS/VST3/$NAME.vst3" "$root/VST3/"
install -m 755 "$ARTIFACTS/Standalone/$NAME" "$root/Standalone/$NAME"
cp "products/$PRODUCT/"{LICENSE,README.md} "$root/"
cat > "$root/INSTALL.txt" <<EOF
$PUBLISHER
$WEBSITE

$NAME $VERSION — Ubuntu 26.04 x86_64
Close your host. Copy the entire VST3/$NAME.vst3 directory into ~/.vst3/
and rescan VST3 plugins in your host. Run Standalone/$NAME to use the app.
To uninstall, remove the copied bundle and the extracted directory.
Source and dependency pins: https://github.com/$GITHUB_REPOSITORY/tree/$GITHUB_SHA/products/$PRODUCT
EOF
tar -C "$stage" -czf "dist/$LINUX_FILE" "$NAME"
