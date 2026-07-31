#!/bin/sh
# Wrapper script for Hikari to properly initialize a Wayland session
# This should be executed instead of the `hikari` binary directly, unless
# your login manager (e.g., GDM, SDDM) already provides these environment variables.

# Clear any leaked variables that might cause nesting bugs
unset WAYLAND_DISPLAY
unset DISPLAY

export XDG_SESSION_TYPE=wayland
export XDG_SESSION_CLASS=user

# If XDG_RUNTIME_DIR is not set by the system (e.g. pam_xdg or systemd),
# generate a proper temporary directory for the session IPC bus.
if [ -z "$XDG_RUNTIME_DIR" ]; then
    export XDG_RUNTIME_DIR="/tmp/hikari-runtime-$(id -u)"
    if [ ! -d "$XDG_RUNTIME_DIR" ]; then
        mkdir -m 0700 "$XDG_RUNTIME_DIR"
    fi
fi

# Wrap execution in a dbus session if one is not active
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ] && command -v dbus-run-session >/dev/null 2>&1; then
    exec dbus-run-session ./hikari "$@"
else
    exec ./hikari "$@"
fi
