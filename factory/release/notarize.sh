#!/usr/bin/env bash
# Certificate handling follows the original Livecut release workflow.
set -euo pipefail
umask 077
work=$(mktemp -d)
keychain="$work/signing.keychain-db"
cleanup() {
  if [[ -e $keychain ]]; then security delete-keychain "$keychain"; fi
  rm -rf "$work"
}
trap cleanup EXIT
password=$(openssl rand -hex 32)
security create-keychain -p "$password" "$keychain"
security set-keychain-settings -lut 3600 "$keychain"
security unlock-keychain -p "$password" "$keychain"
for variable in MACOS_APPLICATION_P12 MACOS_INSTALLER_P12; do
  printf '%s' "${!variable}" | base64 -D > "$work/id.p12"
  security import "$work/id.p12" -k "$keychain" -P "$MACOS_P12_PASSWORD" -T /usr/bin/codesign -T /usr/bin/productbuild
done
security set-key-partition-list -S apple-tool:,apple: -s -k "$password" "$keychain" >/dev/null
security list-keychains -d user -s "$keychain" "$HOME/Library/Keychains/login.keychain-db"
(umask 022; bash factory/release/macos.sh "$ARTIFACTS" "$VERSION" dist --sign "$MACOS_APPLICATION_IDENTITY" "$MACOS_INSTALLER_IDENTITY")
pkg="dist/$MACOS_FILE"
printf '%s' "$NOTARY_API_KEY" | base64 -D > "$work/notary.p8"
xcrun notarytool submit "$pkg" --key "$work/notary.p8" --key-id "$NOTARY_API_KEY_ID" --issuer "$NOTARY_API_ISSUER_ID" --wait --output-format json > "$work/result.json"
jq -e '.status == "Accepted"' "$work/result.json"
xcrun stapler staple "$pkg"
xcrun stapler validate "$pkg"
pkgutil --check-signature "$pkg"
spctl --assess --type install --verbose=2 "$pkg"
sudo installer -pkg "$pkg" -target /
for bundle in "/Library/Audio/Plug-Ins/VST3/$NAME.vst3" "/Library/Audio/Plug-Ins/Components/$NAME.component" "/Applications/$NAME.app"; do
  codesign --verify --strict --verbose=2 "$bundle"
  codesign -dvv "$bundle" 2>&1 | grep -Fx "Authority=$MACOS_APPLICATION_IDENTITY"
done
