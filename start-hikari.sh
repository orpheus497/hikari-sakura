#!/bin/sh
# [COMMENT] Script function and purpose: Launch wrapper for the Hikari Sakura Wayland
# compositor. Ensures a correct Wayland session environment is established
# before executing the compositor binary. Should be invoked instead of the
# hikari binary directly unless a login manager (GDM, SDDM, greetd) already
# provides these variables.
#
# Usage (installed system): start-hikari [hikari options]
# Usage (development tree):  ./start-hikari.sh [hikari options]

# [COMMENT] Action purpose: Clear every leaked display variable that would send
# the compositor to a nested backend instead of the hardware.
#
# All three matter, and the list has to match what wlroots actually reads.
# wlr_backend_autocreate tests WAYLAND_DISPLAY *and* WAYLAND_SOCKET before it
# considers anything else, builds a nested Wayland backend if either is set, and
# stops looking; DISPLAY does the same for X11; DRM and libinput are reached
# only when none of the three is set. None of those branches falls back, so one
# stale value does not produce a session in a window -- it stops the compositor
# starting at all.
#
# WAYLAND_SOCKET was missing from this list, which left the wrapper's whole
# purpose conditional: clearing two of the three still let an inherited
# WAYLAND_SOCKET pick the nested backend.
#
# Anyone who *wants* Hikari nested inside an existing session should use the
# hikari-sakura.desktop entry, or run the binary directly, rather than this
# wrapper -- clearing these is the wrapper's job, not an obstacle to work round.
unset WAYLAND_DISPLAY
unset WAYLAND_SOCKET
unset DISPLAY

# [COMMENT] Action purpose: Export mandatory Wayland session environment
# variables.
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_CLASS=user

# [COMMENT] Action purpose: Export the desktop identity. This is a single name,
# not a colon-separated list, because it is read verbatim by everything that
# reports which desktop is running -- a list shows up as "Sakura:wlroots" in
# every system-info tool.
#
# It carried a ":wlroots" suffix previously for one reason only: the portal
# backend was selected by matching this variable against the UseIn= field of
# xdg-desktop-portal-wlr's portal file, which lists wlroots, sway, Wayfire,
# river, phosh and Hyprland but no Sakura name. Dropping the suffix therefore
# used to mean no backend matched and screen sharing silently found no
# provider.
#
# That coupling is now gone: share/xdg-desktop-portal/sakura-portals.conf names
# the backends for this desktop directly, so selection no longer depends on
# this value at all. The two are a matched pair -- if that file is not
# installed, this name must regain its ":wlroots" suffix or capture breaks with
# no diagnostic.
#
# Set unconditionally rather than with a default, because this script's whole
# purpose is to establish a known-good session environment; a display manager
# that set something else did so for a session entry that named this desktop.
export XDG_CURRENT_DESKTOP="Sakura"

# [COMMENT] Action purpose: Name the session for the two consumers that read
# XDG_SESSION_DESKTOP rather than XDG_CURRENT_DESKTOP. Kept identical to the
# value above so the desktop cannot be reported under two different names
# depending on which variable a tool happens to read.
export XDG_SESSION_DESKTOP="Sakura"

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
# must be an absolute path, an existing directory owned by the current user
# with mode 0700.
case "$XDG_RUNTIME_DIR" in
    /*) ;;
    *)
        echo "start-hikari: XDG_RUNTIME_DIR ($XDG_RUNTIME_DIR) is not an absolute path" >&2
        exit 1
        ;;
esac
USER_UID=$(id -u) || { echo "start-hikari: failed to determine current UID" >&2; exit 1; }
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
# posix_fallocate() fails on ZFS -- its vop_allocate is wired to VOP_EOPNOTSUPP
# on FreeBSD 15, so the call returns EOPNOTSUPP (on 14.x it was VOP_EINVAL and
# the error was EINVAL) -- causing Wayland wl_shm buffer
# allocations to fail for clients. The fix is to mount tmpfs over the path or
# disable ZFS automount for the dataset (e.g. zfs set canmount=noauto zroot/tmp).
FS_TYPE=$(df -T "$XDG_RUNTIME_DIR" 2>/dev/null | awk 'NR==2{print $2}')
if [ "$FS_TYPE" = "zfs" ]; then
    echo "start-hikari: WARNING: XDG_RUNTIME_DIR ($XDG_RUNTIME_DIR) is on ZFS." >&2
    echo "  posix_fallocate() is not supported on ZFS -- Wayland clients may fail." >&2
    echo "  Fix: mount tmpfs at this path, or run:" >&2
    echo "    sudo zfs set canmount=noauto <dataset>" >&2
    echo "  See README.md for details." >&2
fi

# [COMMENT] Action purpose: Derive the directory that contains this wrapper
# script so that a co-installed hikari binary (sibling executable) is found
# regardless of the caller's working directory. The cd/pwd pipeline can fail if
# the script's directory has been removed or is inaccessible after the script
# was launched (e.g. a deleted tmpfs mount). Treat any such failure as fatal so
# that the HIKARI_BIN lookup below never runs with an empty SCRIPT_DIR.
SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd) || {
    echo "start-hikari: cannot resolve script directory from \$0='$0'" >&2
    exit 1
}

# [COMMENT] Action purpose: Resolve hikari binary — prefer the sibling
# executable at ${SCRIPT_DIR}/hikari (covers both installed /usr/local/bin and
# in-tree development layouts). Fall back to $PATH, then ./hikari for edge
# cases where the script was copied without its sibling.
if [ -x "${SCRIPT_DIR}/hikari" ]; then
    HIKARI_BIN="${SCRIPT_DIR}/hikari"
elif command -v hikari > /dev/null 2>&1; then
    HIKARI_BIN=hikari
elif [ -x "./hikari" ]; then
    HIKARI_BIN=./hikari
else
    echo "start-hikari: hikari binary not found in ${SCRIPT_DIR}, on PATH, or in current directory" >&2
    exit 1
fi

# [COMMENT] Action purpose: Capture compositor diagnostics when HIKARI_LOG is
# set to a file path. wlr_log writes exclusively to stderr and nothing
# redirects it by default, which is why the Phase 53/57/61 crash
# investigations had no output to work from. Kept opt-in so an ordinary
# session keeps its current tty behaviour unchanged.
#
# This redirects the wrapper's own descriptors rather than piping into tee,
# so the exec below is a real exec: the compositor stays the top-level
# process and its exit status and signal disposition remain the script's.
# A pipeline would report tee's status instead, so a compositor killed by
# SIGSEGV would surface as a clean exit 0 -- the opposite of what a
# diagnostic build needs. `set -o pipefail` is not POSIX and this script
# declares #!/bin/sh, so it is not an option here. The trade-off is that
# output no longer echoes to the terminal while logging; the compositor
# takes over the VT regardless, so capture is the point.
#
# Writability is probed in a subshell first: a redirection failure on `exec`
# (a special built-in) would otherwise terminate the shell with no message.
if [ -n "$HIKARI_LOG" ]; then
    if ! ( : >> "$HIKARI_LOG" ) 2>/dev/null; then
        echo "start-hikari: cannot write HIKARI_LOG ($HIKARI_LOG)" >&2
        exit 1
    fi
    exec >> "$HIKARI_LOG" 2>&1
fi

# [COMMENT] Action purpose: Wrap execution in a D-Bus session if one is not
# already active. Required for XDG portal, clipboard, and secret service.
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ] && command -v dbus-run-session > /dev/null 2>&1; then
    exec dbus-run-session "$HIKARI_BIN" "$@"
else
    exec "$HIKARI_BIN" "$@"
fi
