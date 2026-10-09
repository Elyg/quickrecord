#!/usr/bin/env bash
# Exports secrets from the keychain to a .env file for Docker deployment.
# The .env file is printed to stdout — pipe or redirect as needed.
#
# Canonical pattern — copy and adjust SERVICE + the lines to this project's keys.
# Wire it up in the .justfile as:  export-env:\n    bash scripts/export-env.sh > .env

SERVICE="quickrecord"

_get() { secret-tool lookup service "$SERVICE" key "$1" 2>/dev/null; }

cat <<EOF
# quickrecord has no secrets yet
EOF
