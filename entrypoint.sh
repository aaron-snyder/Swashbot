#!/bin/sh
# Hosts like Railway mount the save volume owned by root. Fix the ownership,
# then run the bot as the unprivileged "swashbot" user.
set -e
DATA_DIR="$(dirname "${SWASHBOT_DATA:-/data/swashbot.json}")"
if [ "$(id -u)" = "0" ]; then
    mkdir -p "$DATA_DIR"
    chown -R swashbot:swashbot "$DATA_DIR"
    exec setpriv --reuid=swashbot --regid=swashbot --init-groups swashbot "$@"
fi
exec swashbot "$@"
