#!/bin/sh
# ##Script function and purpose: Launch wrapper for the hikari Wayland compositor.
# Ensures a correct Wayland session environment is established before executing
# the compositor binary. Should be invoked instead of the hikari binary directly
# unless a login manager (GDM, SDDM, greetd) already provides these variables.
#
# Usage (installed system): start-hikari [hikari options]
# Usage (development tree):  ./start-hikari.sh [hikari options]

# Clear any leaked display variables that would cause nested-compositor bugs
unset WAYLAND_DISPLAY
unset DISPLAY

export XDG_SESSION_TYPE=wayland
export XDG_SESSION_CLASS=user

# ##Condition purpose: Bootstrap XDG_RUNTIME_DIR if the system (pam_xdg, systemd,
# or elogind) did not provide one. FreeBSD with seatd typically requires this.
if [ -z "$XDG_RUNTIME_DIR" ]; then
    export XDG_RUNTIME_DIR="/tmp/hikari-runtime-$(id -u)"
    if [ ! -d "$XDG_RUNTIME_DIR" ]; then
        mkdir -m 0700 "$XDG_RUNTIME_DIR"
    fi
fi

# ##Action purpose: Resolve hikari binary — prefer system-installed binary on $PATH
# so that installed deployments work correctly (e.g. rc.d / service scripts).
# Fall back to ./hikari for in-tree development builds without installation.
if command -v hikari > /dev/null 2>&1; then
    HIKARI_BIN=hikari
elif [ -x "./hikari" ]; then
    HIKARI_BIN=./hikari
else
    echo "start-hikari: hikari binary not found on PATH or in current directory" >&2
    exit 1
fi

# ##Condition purpose: Wrap execution in a D-Bus session if one is not already
# active. Required for XDG portal, clipboard, and secret service functionality.
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ] && command -v dbus-run-session > /dev/null 2>&1; then
    exec dbus-run-session "$HIKARI_BIN" "$@"
else
    exec "$HIKARI_BIN" "$@"
fi
