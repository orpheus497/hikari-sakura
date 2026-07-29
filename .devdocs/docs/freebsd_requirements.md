# FreeBSD Requirements & Operational Particulars

## Overview
This document specifies all kernel settings, environment setups, privilege mechanics, security configurations, and dependency matrices required to run `hikari` reliably on modern FreeBSD (FreeBSD 13.x / 14.x+).

---

## 1. System Kernel & Evdev Input Configuration

Wayland compositors rely on the FreeBSD `evdev` interface (`/dev/input/event*`) for unified keyboard, mouse, and touch input processing via `libinput`.

### Sysctl Settings
Set `kern.evdev.rcpt_mask` to prevent duplicate event delivery across legacy `/dev/sysmouse` and `evdev`:

```sh
# Add to /etc/sysctl.conf for persistent boot configuration:
kern.evdev.rcpt_mask=12
```

### Mouse Daemon (`moused`) Compatibility
If `moused` is enabled in `/etc/rc.conf` (`service moused enable`), update `sysctl` to:
```sh
kern.evdev.rcpt_mask=3
```

---

## 2. Shared Memory & `XDG_RUNTIME_DIR` Setup

Wayland clients allocate shared memory buffers using POSIX shared memory file descriptors and `posix_fallocate`. 

> [!IMPORTANT]
> **ZFS Limitation:** ZFS does not support `posix_fallocate` on file descriptors. Creating `XDG_RUNTIME_DIR` on a ZFS dataset will cause native Wayland clients (e.g., Firefox, Alacritty) to crash upon buffer allocation.

### Solution: Mount `/tmp` on `tmpfs`
Ensure `/tmp` is backed by `tmpfs` in `/etc/fstab`:

```fstab
# /etc/fstab
tmpfs   /tmp    tmpfs   rw,mode=1777   0   0
```

### Environment Variable Setup
In your user shell profile (`~/.profile`, `~/.zshrc`, or `~/.cshrc`):

```sh
export XDG_RUNTIME_DIR=$(mktemp -d /tmp/runtime-${USER}.XXXXXX)
chmod 0700 "${XDG_RUNTIME_DIR}"
# Validate that the directory is owned by the current user with 0700 permissions
if [ "$(stat -f "%u:%Op" "${XDG_RUNTIME_DIR}")" != "$(id -u):40700" ]; then
  echo "Error: XDG_RUNTIME_DIR has invalid ownership or permissions."
  exit 1
fi
```

---

## 3. Privilege & Session Management (`seatd`)

`hikari` requires direct access to DRM devices (`/dev/dri/card*`, `/dev/dri/renderD*`) and input devices (`/dev/input/event*`).

### Recommended Approach: `seatd`
Install and enable `seatd` daemon on FreeBSD:

```sh
pkg install seatd
sysrc seatd_enable="YES"
service seatd start
```

Ensure your user is added to the `video` and `input` groups:

```sh
pw groupmod video -m username
pw groupmod input -m username
```

### Legacy SUID Installation
If `seatd` is not present, `hikari` can be installed setuid root:

```sh
make WITH_SUID=YES install
```

---

## 4. Screen Lock Security & PAM (`hikari-unlocker`)

Screen unlocking in `hikari` is performed by an isolated helper binary: `hikari-unlocker`.

### PAM Configuration
Copy the FreeBSD PAM configuration file:

```sh
cp etc/pam.d/hikari-unlocker.FreeBSD /usr/local/etc/pam.d/hikari-unlocker
```

### File Permissions
`hikari-unlocker` MUST be owned by `root` with `4555` (setuid) permissions:

```sh
chown root:wheel /usr/local/bin/hikari-unlocker
chmod 4555 /usr/local/bin/hikari-unlocker
```

---

## 5. Build & Runtime Dependency Matrix

| Package | Purpose | Minimum Version | FreeBSD Port Name |
|---------|---------|-----------------|-------------------|
| `wlroots` | Wayland Compositor Library | >= 0.18.0 | `x11-toolkits/wlroots` |
| `pango` | Text Rendering Engine | Recent | `x11-toolkits/pango` |
| `cairo` | 2D Graphics Rendering | Recent | `graphics/cairo` |
| `libinput` | Input Device Event Handling | Recent | `x11/libinput` |
| `xkbcommon` | Keyboard Layout & Keymap | Recent | `x11/libxkbcommon` |
| `pixman` | Pixel Manipulation Library | Recent | `x11/pixman` |
| `libucl` | UCL Configuration File Parser | Recent | `textproc/libucl` |
| `evdev-proto` | Evdev Protocol Headers | Recent | `devel/evdev-proto` |
| `epoll-shim` | Epoll Emulation on FreeBSD | Recent | `devel/epoll-shim` |
| `wayland-protocols` | Wayland Protocol XML Schemas | Recent | `graphics/wayland-protocols` |
| `XWayland` | (Optional) X11 Compatibility Layer | Recent | `x11-servers/xwayland` |
