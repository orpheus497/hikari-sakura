# Hikari - Wayland Compositor

![Screenshot](https://acmelabs.space/~raichoo/hikari.png)

## Description

_hikari_ is a stacking Wayland compositor with additional tiling capabilities,
it is heavily inspired by the Calm Window manager (cwm(1)). Its core concepts
are *views*, *groups*, *sheets* and the *workspace*.

The workspace is the set of views that are currently visible.

A sheet is a collection of views, each view can only be a member of a single
sheet. Switching between sheets will replace the current content of the
workspace with all the views that are a member of the selected sheet. _hikari_
has 9 general purpose sheets that correspond to the numbers **1** to **9** and a
special purpose sheet **0**. Views that are a member of sheet **0** will
always be visible but stacked below the views of the selected sheet.

Groups are a bit more fine grained than sheets. Like sheets, groups are a
collection of views. Unlike sheets you can have a arbitrary number of groups
and each group can have an arbitrary name. Views from one group can be spread
among all available sheets. Some operations act on entire groups rather than
individual views.

## Setting up Wayland on FreeBSD

Wayland currently requires some care to work properly on FreeBSD. This section
aims to document the recent state of how to enable Wayland on the FreeBSD
`STABLE` branch and will change once support is being improved.

### Mouse configuration

To make mice work `kern.evdev.rcpt_mask` should be set to `12`. Depending on
your version of FreeBSD this is done automatically or via setting the value in
`/etc/sysctl.conf`.

Some systems might require `moused` for mice to work. Enable it with `service
moused enable`. This requires setting `kern.evdev.rcpt_mask` to `3`.

### Setting up XDG\_RUNTIME\_DIR

Wayland compositors require `XDG_RUNTIME_DIR` to be set to a user-owned
directory with mode `0700`. Some Wayland clients (e.g. native Wayland `firefox`)
require `posix_fallocate` to work in that directory. This is not supported by
ZFS, therefore on ZFS-root systems you must ensure `/tmp` is backed by `tmpfs`:

1. Prevent the ZFS dataset from mounting over the tmpfs:

```sh
sudo zfs set canmount=noauto zroot/tmp
```

1. Add a `tmpfs` entry to `/etc/fstab` (if not already present):

```sh
tmpfs   /tmp   tmpfs   rw,mode=1777,size=256m   0   0
```

1. Reboot, then verify `/tmp` is mounted as `tmpfs`:

   ```sh
   mount | grep ' on /tmp '
   ```

   The output should show `tmpfs` as the filesystem type, for example:
   `tmpfs on /tmp (tmpfs, ...)`. A line showing `zfs` instead means `/tmp`
   is still backed by ZFS — re-check every setup step above, including the
   `/etc/fstab` entry, and make sure the system has been rebooted.

> **Note:** Without step 1, ZFS automount will mount `zroot/tmp` *over* the
> fstab tmpfs entry, and `/tmp` will still be ZFS despite the fstab line.

If your system does not set `XDG_RUNTIME_DIR` automatically (e.g. FreeBSD with
`seatd` and no `elogind`), the `start-hikari` wrapper script will create one at
`/tmp/hikari-runtime-$UID` with the correct permissions. You can also set it
manually:

```sh
export XDG_RUNTIME_DIR=/tmp/runtime-$(id -u)
mkdir -p -m 0700 "$XDG_RUNTIME_DIR"
```

### Setting up PAM

Setting up PAM is needed to give `hikari` the ability to unlock the screen when
using the screen locker. Copy `etc/pam.d/hikari-unlocker.FreeBSD` from the
source tree to `/usr/local/etc/pam.d/hikari-unlocker`.

### Setting up the keyboard layout

`hikari` gets its keyboard settings from `xkb` environment variables. To select
a layout set the `XKB_DEFAULT_LAYOUT` to the desired value before starting
`hikari`.

```sh
export XKB_DEFAULT_LAYOUT="de(nodeadkeys),de"
```

## Building

`hikari` currently only works on FreeBSD. This will likely change in
the future when more systems adopt Wayland. When building directly from the
repository, breaking changes might be encountered. These are documented in the
`UPDATING` file which should be consulted before updating to a newer build.

### Dependencies

* wlroots (0.20)
* pango
* cairo
* libinput
* xkbcommon
* pixman
* libucl
* evdev-proto
* epoll-shim (FreeBSD)
* XWayland (optional, runtime dependency)

### Compiling and Installing

The build process will produce two binaries `hikari` and `hikari-unlocker`, plus
a `start-hikari` wrapper script. `hikari-unlocker` is used to check credentials
for unlocking the screen, which needs to be installed with root setuid.
`hikari` can rely on `seatd` to gain root privileges when required; however, if
needed it can also be installed with root setuid — see "Installing with SUID"
below. All three files are installed to `${PREFIX}/bin` by `make install`.

### Launching

Use `start-hikari` to launch the compositor. It sets up the required Wayland
session environment before executing the `hikari` binary:

* Clears leaked `WAYLAND_DISPLAY` / `DISPLAY` variables
* Creates `XDG_RUNTIME_DIR` if the system did not provide one
* Validates `XDG_RUNTIME_DIR` ownership (current user) and permissions (`0700`)
* Warns if `XDG_RUNTIME_DIR` resides on ZFS (incompatible with `posix_fallocate`)
* Resolves the `hikari` binary from the script's own directory, `$PATH`, or `./`
* Wraps execution in a D-Bus session if one is not already active

```sh
start-hikari
```

If you are using a display manager (GDM, SDDM, greetd), the installed
`hikari.desktop` session file will call `start-hikari` automatically.

`hikari` can be configured via `$XDG_CONFIG_HOME/hikari/hikari.conf`, the
default configuration can be found under `$PREFIX/etc/hikari/hikari.conf`
(depending on the value of `PREFIX` that was specified during the installation).

The default configuration expects your default terminal emulator to be specified
in the `$TERMINAL` environment variable.

The installation destination can be configured by setting `PREFIX` (default is
`/usr/local` and does not need to be given explicitly). If you want to install
`hikari` into a directory other than `/usr/local` you always should state the
`PREFIX` when issuing `make`, since this information is also used to specify
where `hikari` can find the default configuration on your system and is needed
during the compilation process. To override installation paths for `etc` specify
`ETC_PREFIX`.

#### Building on FreeBSD

Simply run `make`. The installation destination can be configured by setting
`PREFIX` (default is `/usr/local` and does not need to be given explicitly).

```sh
make
```

`uninstall` requires the same values for prefixes.

### Building with all features enabled

The following sections explain how to enabled features on an individual basis.
However, to enable every feature the build system offers the `WITH_ALL` flag.

```sh
make WITH_ALL=YES
```

#### Building with XWayland support

`hikari` offers optional XWayland support which is enabled via setting
`WITH_XWAYLAND`.

```sh
make WITH_XWAYLAND=YES
```

#### Building with screencopy support

Screencopy support allows tools like `grim` to work with `hikari`, it also
allows applications to copy the desktop content. This is disabled by default
and can be added by setting `WITH_SCREENCOPY`.

```sh
make WITH_SCREENCOPY=YES
```

#### Building with gammacontrol support

Gamma control is needed for tools like `redshift`. This is disabled by default
and can be enabled via setting `WITH_GAMMACONTROL`.

```sh
make WITH_GAMMACONTROL=YES
```

#### Building with layer-shell support

Some applications that are used to build desktop components require
`layer-shell`. Examples for this are `waybar`, `wofi` and `slurp`. To turn on
`layer-shell` support compile with the `WITH_LAYERSHELL` option.

```sh
make WITH_LAYERSHELL=YES
```

#### Building with virtual input support

Virtual input support is needed for applications like `wayvnc`.

```sh
make WITH_VIRTUAL_INPUT=YES
```

#### Building the manpage

Building the `hikari` manpage requires [`pandoc`](http://pandoc.org/). To build
the manpage just run `make VERSION=1.0.0 doc`, where `VERSION` is the version
number that will be spliced into the manpage. The distribution tarball of
`hikari` comes with a precompiled manpage removing the need for a `pandoc`
installation.

#### Installing with SUID

If `hikari` should require root privileges for startup, state `WITH_SUID=YES`
during installation.

```sh
make WITH_SUID=YES install
```

#### Building a DEBUG build

In the case of a crash or a bug you should build a debug version of `hikari` and
try to reproduce the issue. This builds `hikari` with debug symbols and
sanitizers enabled. Extracting a stack trace for debugging purposes is also very
helpful if you are planning to submit a bug report.

```sh
make DEBUG=YES
```

## Community

The `hikari` community gears to be inclusive and welcoming to everyone, this is
why we chose to adhere to the [Geekfeminism Code of
Conduct](https://hikari.acmelabs.space/coc.html).

If you care to be a part of our community, please join our Matrix chat at
`#hikari:acmelabs.space` and/or subscribe to our mailing list by sending a mail
to `hikari+subscribe@acmelabs.space`.

## Contributing

Please make sure you use `clang-format` with the accompanying `.clang-format`
configuration before submitting any patches.
