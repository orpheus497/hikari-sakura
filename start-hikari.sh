#!/bin/sh
# [COMMENT] Script function and purpose: Launch wrapper for the hikari Wayland
# compositor. Ensures a correct Wayland session environment is established
# before executing the compositor binary. Should be invoked instead of the
# hikari binary directly unless a login manager (GDM, SDDM, greetd) already
# provides these variables.
#
# Usage (installed system): start-hikari [hikari options]
# Usage (development tree):  ./start-hikari.sh [hikari options]

# [COMMENT] Action purpose: Clear any leaked display variables that would cause
# nested-compositor bugs.
unset WAYLAND_DISPLAY
unset DISPLAY

# [COMMENT] Action purpose: Export mandatory Wayland session environment
# variables.
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_CLASS=user

# [COMMENT] Action purpose: Bootstrap XDG_RUNTIME_DIR if the system (pam_xdg,
# systemd, or elogind) did not provide one. FreeBSD with seatd typically
# requires this. Creates a secure runtime directory at /tmp/hikari-runtime-$UID.
if [ -z "$XDG_RUNTIME_DIR" ]; then
    USER_UID=$(id -u)
    if [ $? -ne 0 ]; then
        echo "start-hikari: failed to retrieve current UID" >&2
        exit 1
    fi
    export XDG_RUNTIME_DIR="/tmp/hikari-runtime-$USER_UID"

    if [ ! -d "$XDG_RUNTIME_DIR" ]; then
        if ! mkdir -m 0700 "$XDG_RUNTIME_DIR"; then
            echo "start-hikari: failed to create XDG_RUNTIME_DIR" >&2
            exit 1
        fi
    fi
fi

# [COMMENT] Action purpose: Validate XDG_RUNTIME_DIR ownership and permissions.
# Both caller-supplied and generated paths are checked before exec. The path
# must be an existing directory owned by the current user with mode 0700.
USER_UID=${USER_UID:-$(id -u)}
DIR_UID=$(stat -c '%u' "$XDG_RUNTIME_DIR" 2>/dev/null || stat -f '%u' "$XDG_RUNTIME_DIR" 2>/dev/null)
DIR_PERMS=$(stat -c '%a' "$XDG_RUNTIME_DIR" 2>/dev/null || stat -f '%OLp' "$XDG_RUNTIME_DIR" 2>/dev/null)
if [ ! -d "$XDG_RUNTIME_DIR" ]; then
    echo "start-hikari: XDG_RUNTIME_DIR ($XDG_RUNTIME_DIR) is not an existing directory" >&2
    exit 1
fi
if [ "$DIR_UID" != "$USER_UID" ] || [ "$DIR_PERMS" != "700" ]; then
    echo "start-hikari: XDG_RUNTIME_DIR has incorrect ownership (uid=$DIR_UID, expected=$USER_UID) or permissions ($DIR_PERMS, expected=700)" >&2
    exit 1
fi

# [COMMENT] Action purpose: Detect ZFS-backed XDG_RUNTIME_DIR and warn the user.
# posix_fallocate() returns EOPNOTSUPP on ZFS, causing Wayland wl_shm buffer
# allocations to fail for clients. The fix is to mount tmpfs over the path or
# disable ZFS automount for the dataset (e.g. zfs set canmount=noauto zroot/tmp).
FS_TYPE=$(stat -f '%T' "$XDG_RUNTIME_DIR" 2>/dev/null)
if [ "$FS_TYPE" = "zfs" ]; then
    echo "start-hikari: WARNING: XDG_RUNTIME_DIR ($XDG_RUNTIME_DIR) is on ZFS." >&2
    echo "  posix_fallocate() is not supported on ZFS -- Wayland clients may fail." >&2
    echo "  Fix: mount tmpfs at this path, or run:" >&2
    echo "    sudo zfs set canmount=noauto <dataset>" >&2
    echo "  See README.md for details." >&2
fi

# [COMMENT] Action purpose: Resolve hikari binary — prefer system-installed
# binary on $PATH so that installed deployments work correctly (e.g. rc.d /
# service scripts). Fall back to ./hikari for in-tree development builds.
if command -v hikari > /dev/null 2>&1; then
    HIKARI_BIN=hikari
elif [ -x "./hikari" ]; then
    HIKARI_BIN=./hikari
else
    echo "start-hikari: hikari binary not found on PATH or in current directory" >&2
    exit 1
fi

# [COMMENT] Action purpose: Wrap execution in a D-Bus session if one is not
# already active. Required for XDG portal, clipboard, and secret service.
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ] && command -v dbus-run-session > /dev/null 2>&1; then
    exec dbus-run-session "$HIKARI_BIN" "$@"
else
    exec "$HIKARI_BIN" "$@"
fi
