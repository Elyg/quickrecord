#!/usr/bin/env bash
# One-time: a self-signed code signing certificate for QuickRecord.app. Builds signed with the
# same certificate keep one identity, so macOS keeps Screen Recording / Input Monitoring /
# Microphone permissions across updates (ad-hoc builds count as a new app every time).
#
# The certificate only goes to the repo's GitHub Actions secrets, where the release workflow
# signs with it; no copy is kept on this Mac. Anyone holding it could sign a program that macOS
# would treat as QuickRecord (with its permissions), so it shouldn't sit readable on disk.
#   bash scripts/mac-signing-setup.sh            # create one unless the secrets already exist
#   bash scripts/mac-signing-setup.sh --force    # replace it (installed apps get a new identity)
set -euo pipefail

NAME="QuickRecord Signing"

if ! command -v gh >/dev/null; then
    echo "gh (GitHub CLI) is needed to store the certificate in the Actions secrets." >&2
    exit 1
fi
if [ "${1:-}" != "--force" ] && gh secret list | grep -q '^MACOS_CERT_P12\b'; then
    echo "The GitHub Actions secrets already hold a certificate (--force to replace it)."
    exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
/usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -days 7300 -subj "/CN=$NAME" \
    -keyout "$tmp/key.pem" -out "$tmp/cert.pem" \
    -addext "basicConstraints=critical,CA:false" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=critical,codeSigning" 2>/dev/null
password=$(/usr/bin/openssl rand -hex 16)
/usr/bin/openssl pkcs12 -export -inkey "$tmp/key.pem" -in "$tmp/cert.pem" -name "$NAME" \
    -out "$tmp/cert.p12" -passout "pass:$password"

base64 -i "$tmp/cert.p12" | gh secret set MACOS_CERT_P12
printf '%s' "$password" | gh secret set MACOS_CERT_PASSWORD
echo "Created \"$NAME\" and stored it in the GitHub Actions secrets MACOS_CERT_P12 / MACOS_CERT_PASSWORD."
