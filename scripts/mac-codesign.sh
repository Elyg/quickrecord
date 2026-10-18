#!/usr/bin/env bash
# Signs a .app with the QuickRecord certificate (see mac-signing-setup.sh), given in
# MACOS_CERT_P12 / MACOS_CERT_PASSWORD (the release workflow's secrets). Ad hoc without them.
#   bash scripts/mac-codesign.sh path/to/QuickRecord.app
set -euo pipefail

APP="$1"
NAME="QuickRecord Signing"
p12="${MACOS_CERT_P12:-}"
password="${MACOS_CERT_PASSWORD:-}"

if [ -z "$p12" ] || [ -z "$password" ]; then
    echo "note: no signing certificate (release builds get one in CI); signing ad hoc, so macOS" \
         "treats this build as a new app and asks for its permissions again" >&2
    codesign --force --deep --sign - "$APP"
    exit 0
fi

# A throwaway keychain with a known password: codesign can use it without prompts.
tmp=$(mktemp -d)
keychain="$tmp/signing.keychain-db"
keychain_password=$(/usr/bin/openssl rand -hex 16)
original=$(security list-keychains -d user | tr -d '"' | xargs)
cleanup() {
    security list-keychains -d user -s $original
    security delete-keychain "$keychain" 2>/dev/null || true
    rm -rf "$tmp"
}
trap cleanup EXIT

printf '%s' "$p12" | base64 --decode > "$tmp/cert.p12"
security create-keychain -p "$keychain_password" "$keychain"
security set-keychain-settings "$keychain" # no auto-lock
security unlock-keychain -p "$keychain_password" "$keychain"
security import "$tmp/cert.p12" -k "$keychain" -P "$password" -T /usr/bin/codesign >/dev/null
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$keychain_password" "$keychain" >/dev/null
security list-keychains -d user -s "$keychain" $original

codesign --force --deep --keychain "$keychain" --sign "$NAME" "$APP"
codesign --verify --deep "$APP"
echo "Signed with \"$NAME\": $(codesign -dr - "$APP" 2>&1 | grep designated)"
