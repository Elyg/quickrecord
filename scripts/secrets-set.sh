#!/usr/bin/env bash
# Interactive keychain setup. Stores each secret in the GNOME Keyring (libsecret) under
# this project's service name, then reloads direnv so .envrc re-reads them.
#
# Canonical pattern — copy and adjust SERVICE + the _store lines to this project's keys.
# Requires: gum (provided by the nix devShell) and secret-tool (libsecret).
# Wire it up in the .justfile as:  secrets-set:\n    bash scripts/secrets-set.sh

SERVICE="quickrecord"
PRETTY="quickrecord"   # used only in the keyring label

_store() {
    label="$1"
    key="$2"
    existing=$(secret-tool lookup service "$SERVICE" key "$key" 2>/dev/null)
    if [ -n "$existing" ]; then
        gum style --foreground 3 "  ~ $label (already set, Enter to keep)"
    else
        gum style "  $label"
    fi
    value=$(gum input --password --placeholder "value" --prompt "    > ") || { echo; gum style --foreground 3 "Cancelled."; exit 0; }
    if [ -n "$value" ]; then
        echo -n "$value" | secret-tool store --label="$PRETTY – $label" service "$SERVICE" key "$key"
        gum style --foreground 2 "    ✓ stored"
    else
        gum style --foreground 8 "    - kept"
    fi
    echo
}

gum style --bold "$PRETTY — keychain setup (Enter to skip)"
echo

# --- one _store line per secret (label, key) ---
# quickrecord has no secrets yet. Add lines like:
# _store "Some API Key" some_key

echo
gum style --foreground 2 "Done."
direnv reload
