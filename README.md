# Hikari Sakura — a Wayland desktop environment for FreeBSD

![Hikari Sakura running on FreeBSD](share/hikari_sakura_alpha.png)

## Description

*Hikari Sakura* is a FreeBSD-focused revamp and modernization of the original
Hikari — created by `raichoo` and later carried at
<https://github.com/antaz/hikari>, which has since been abandoned upstream. It
has diverged considerably from its origin and is aimed at being the first
purpose-built Wayland desktop environment for FreeBSD rather than a compositor
alone.

It is a stacking Wayland compositor with additional tiling capabilities,
heavily inspired by the Calm Window manager (cwm(1)). Its core concepts are
*views*, *groups*, *sheets* and the *workspace*.

The workspace is the set of views that are currently visible.

A sheet is a collection of views, each view can only be a member of a single
sheet. Switching between sheets will replace the current content of the
workspace with all the views that are a member of the selected sheet. _Hikari
Sakura_ has 9 general purpose sheets that correspond to the numbers **1** to
**9** and a special purpose sheet **0**. Views that are a member of sheet **0**
will always be visible but stacked below the views of the selected sheet.

Groups are a bit more fine grained than sheets. Like sheets, groups are a
collection of views. Unlike sheets you can have a arbitrary number of groups
and each group can have an arbitrary name. Views from one group can be spread
among all available sheets. Some operations act on entire groups rather than
individual views.

Beyond window management, Hikari Sakura provides its own **top bar** and its own
**screen locker**, both themed from the same sixteen-colour palette as
everything else. The screen locker is drawn entirely in-process. The top bar is
too — the compositor renders it in its own scene graph — but its *content* comes
from `hikari-topbar`, a separate unprivileged process, so that blocking sensor
reads cannot stall the Wayland event loop. See [The top bar](#the-top-bar) and
the *Lock screen* section of hikari(1).

### Naming

**Hikari Sakura** is the name of the desktop environment as a whole. `hikari`
is the name of the compositor binary, the configuration file, the manual page
and the configuration directory — those keep the short name and are written in
lower case throughout this document and in hikari(1).

## Companion projects

Hikari Sakura is designed to be used with two sibling projects, and takes the
second half of its name from the display manager:

| Project | Role |
| --- | --- |
| [**sofi**](https://github.com/orpheus497/sofi) | The shell: application menu, task strip, sheet switcher, notification daemon and history, system tray host, message toasts — each a `zwlr_layer_shell_v1` surface from one binary. |
| [**sakura**](https://github.com/orpheus497/sakura) | The display manager: a FreeBSD-only TUI login manager that runs on a virtual terminal, talks to OpenPAM directly, and needs no graphical toolkit or session bus. |

Both are optional — Hikari Sakura runs on its own, and any layer-shell client
(`waybar`, `wofi`, `mako`) or display manager (GDM, SDDM, greetd) works in
their place.

They are, however, what the defaults assume:

* **The shipped configuration binds four keys to `sofi`** —
  `L+Space` (application menu), `L+w` (task and window manager), `L+e` (sheet
  switcher) and `L+n` (notification history). Without `sofi` installed those
  four bindings do nothing. Either install it or rebind the corresponding
  `actions` entries in `hikari.conf`.
* **`sofi`'s sheet switcher is a client of the compositor's control socket.**
  Hikari Sakura's ten-sheet-per-output model is not expressible in any
  standards-track Wayland protocol, so the compositor exposes it over a small
  Unix socket instead — see [The control socket](#the-control-socket).
* **`sakura` launches the session through the installed `hikari.desktop`
  entry**, the same as any other display manager that reads
  `${PREFIX}/share/wayland-sessions`.

Each project is built and installed independently; neither requires the other
at build time.

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

Setting up PAM is needed to give Hikari Sakura the ability to unlock the screen
when using the screen locker. `make install` does this for you, installing
`etc/pam.d/hikari-unlocker.FreeBSD` to
`${ETC_PREFIX}/etc/pam.d/hikari-unlocker`. If you are running from the source
tree without installing, copy it into place by hand:

```sh
sudo install -m 644 etc/pam.d/hikari-unlocker.FreeBSD \
    ${ETC_PREFIX}/etc/pam.d/hikari-unlocker
```

Substitute the same `ETC_PREFIX` you build with — `/usr/local` if you never set
it — so the policy lands where `hikari-unlocker` expects it.

### Setting up the keyboard layout

`hikari` natively configures its keyboards via the `inputs { keyboards { ... } }`
block in `hikari.conf`. You can directly specify your `xkb` layout, options,
repeat rates, and delays there.

If a keyboard is not explicitly configured in `hikari.conf`, Hikari Sakura will
fall back to using `xkb` environment variables. To select a layout this way, set
`XKB_DEFAULT_LAYOUT` before starting Hikari Sakura.

```sh
export XKB_DEFAULT_LAYOUT="de(nodeadkeys),de"
```

## Configuration & Customization

Hikari Sakura is configured using `libucl` syntax. The configuration file is
expected to be located at `$XDG_CONFIG_HOME/hikari/hikari.conf`. If it does not
exist, Hikari Sakura will fall back to the default config installed at
`${ETC_PREFIX}/etc/hikari/hikari.conf`.

The shipped default configuration is written as an annotated reference: every
key in it is a default made explicit, so deleting any of them changes nothing.
Read it alongside hikari(1), which is the complete reference for every section
and every action name.

`make install-user` seeds a personal copy for you — see
[Seeding a user configuration](#seeding-a-user-configuration).

The configuration file allows you to define:

- **ui**: colors, border sizes, fonts, gaps, the sixteen-colour `palette` and
  the semantic `colorscheme` derived from it, plus the `animation`, `lock` and
  `bar` subsections.
- **layout** / **layouts**: the default layout and named workspace tiling
  patterns.
- **inputs**: pointers, keyboards, switches (lid) and trackpad gestures.
- **outputs**: per-output wallpaper and position.
- **views**: window matching rules (by app ID) to automatically group, float, or
  pin applications to specific sheets.
- **marks**: single-key marks that focus or spawn a given command.
- **actions**: user-defined shell commands (e.g. launching a terminal, volume
  control).
- **bindings**: keyboard and mouse shortcuts mapped to actions or internal
  operations.

**Capabilities & Limitations:**

- You can use environment variables (e.g. `$TERMINAL`) in the configuration
  file; they will be substituted when the configuration is loaded.
- Structural changes like UI themes, custom actions, and new bindings can be
  hot-reloaded using the `reload` action (default: `L+S+r`).
- Two things do not reload. The `hikari-topbar` helper is spawned once at
  startup, so `ui { palette }` reaches the bar only on the next compositor
  start; and `outputs { position }` re-applies on reload only if the value
  actually changed.
- A key written twice is neither merged nor an error — `hikari` uses the
  **first** and ignores the rest, and says so on standard error at startup. If
  a setting appears to do nothing, read that output first. See
  [Logging and diagnostics](#logging-and-diagnostics) for how to capture it
  under a display manager.
- Hikari Sakura does not support conditional statements or complex logic within
  the config file itself.
- Some environment-level initializations (like XWayland) require a full restart
  of the compositor to apply.
- Hikari Sakura provides its own built-in status bar, described below. External
  layer-shell components such as `waybar` work alongside it if you prefer them.

### Autostart

On startup `hikari` executes `~/.config/hikari/autostart` if that file exists
and is both readable and executable. Use it to start `sofi`'s daemons, a
notification agent, a portal, or anything else your session needs:

```sh
#!/bin/sh
sofi -notification-daemon &
sofi -tray-daemon &
```

`hikari -a <executable>` overrides the path for a single run.

## The top bar

Hikari Sakura draws its own top bar in the compositor's scene graph. The
content comes from `hikari-topbar`, a separate unprivileged process the
compositor spawns at startup and reads a swaybar-protocol JSON stream from over
a pipe. It is a separate process on purpose: its sensors are sampled with
blocking `popen()` calls several times a second, and running those inside the
compositor would stall the Wayland event loop on every tick. A slow or wedged
sensor costs the bar its freshness and nothing else.

The bar's height follows `ui { font }`, so it scales with your font choice. Its
colours come from the compositor's `ui { palette }`, passed to the helper on
its command line — which is why a palette change reaches the bar on the next
compositor start rather than on a reload.

### Blocks

| Block | Source | Shown when |
| --- | --- | --- |
| CPU usage | `kern.cp_time` | always |
| RAM usage | `hw.physmem`, `vm.stats.vm.*` | always |
| CPU temperature | `dev.cpu.0.temperature`, falling back to `hw.acpi.thermal.tz0.temperature` | a sensor is readable |
| GPU usage, VRAM, temperature | `nvidia-smi` | an NVIDIA GPU is detected at startup |
| `$HOME` partition usage | `getfsstat(2)` / `statfs(2)` | always |
| Media / now playing | `playerctl` | always (shows `Idle` with nothing playing) |
| Network | `getifaddrs(3)` | always |
| Backlight | `backlight(8)` | the tool reports a level |
| Volume | `pactl` | a default sink is readable |
| Battery | `hw.acpi.battery.*` | a battery is present |
| Clock and date | `strftime(3)` | always |

Blocks with no readable source are omitted entirely rather than shown empty, so
a desktop with no battery and integrated graphics simply has a shorter bar.

The battery block picks its colour from the charge level, mapping onto specific
palette entries — the bands are listed in the shipped `hikari.conf`. Retheming
the palette retints the battery with it.

### Runtime dependencies

None of these are required to build or run Hikari Sakura; each one only affects
the block that uses it.

* **A Nerd Font**, installed and selected via `ui { font }`. The blocks are
  labelled with private-use-area glyphs, so without one the bar renders boxes.
* `playerctl` — the media block. Without it, the block reads `Idle`.
* `pactl` (PulseAudio or PipeWire-pulse) — the volume block.
* `backlight(8)`, part of the FreeBSD base system — the backlight block.
* `nvidia-smi` — the GPU blocks. Probed once at startup; absent on
  integrated-only machines, where the GPU blocks never appear.

### Long block text

The media block's content is whatever the playing track is called and is
therefore unbounded. `ui { bar { max-block-chars, scroll-interval,
scroll-separator } }` caps it and scrolls the remainder as a banner. Those keys
are read by the compositor rather than by the helper, so they do take effect on
a configuration reload. See the *bar* subsection of hikari(1).

### Palette fallback

If `hikari-topbar` is started by hand with no palette argument, it falls back to
reading `~/.cache/wal/colors` (pywal), and to white if that is absent too. The
compositor always passes its own palette, so this path is only reached when the
helper is run standalone.

## The control socket

Hikari Sakura's sheet model — ten sheets per workspace, permanently bound to
their output — is not expressible in any standards-track Wayland protocol.
`foreign-toplevel` has no notion of a workspace, and `ext-workspace-v1`'s
`assign` moves a workspace to an output group rather than a window to a
workspace. An external sheet switcher therefore has nothing to read and nothing
to call.

The compositor supplies that missing surface as a small request/response text
socket at `$XDG_RUNTIME_DIR/hikari.sock`, mode `0600`. It reports which sheet is
displayed and how many views each sheet holds, and accepts the two operations a
switcher needs (`sheet` and `pin`). This is what `sofi -show sheets` speaks.

It is deliberately minimal and is not a general scripting interface: anything
expressible as a Wayland protocol belongs in a Wayland protocol. Every request
is refused unless the compositor is in normal mode, so nothing reachable through
the socket can act on — or read view counts from — a locked screen.

The full protocol, including the response grammar and every error string, is
documented in the **CONTROL SOCKET** section of hikari(1).

## Laptop Optimization

When using Hikari Sakura on a laptop, especially on FreeBSD, you may want to map
your multimedia keys and handle the lid switch.

### Multimedia Keys (Volume & Brightness)

Hikari Sakura natively understands `XF86` keysyms via `xkbcommon`. The shipped
configuration binds the volume keys through `pactl` — which works wherever
PulseAudio or PipeWire is running — and brightness through FreeBSD's base-system
`backlight(8)`. On a machine with neither sound server, swap the volume actions
for FreeBSD's native `mixer(8)`:

```ucl
actions {
  vol-up       = "mixer vol +5"
  vol-down     = "mixer vol -5"
  vol-mute     = "mixer vol.mute=^"
  bright-up    = "backlight incr 5"
  bright-down  = "backlight decr 5"
}

bindings {
  keyboard {
    "0+XF86AudioRaiseVolume"  = action-vol-up
    "0+XF86AudioLowerVolume"  = action-vol-down
    "0+XF86AudioMute"         = action-vol-mute
    "0+XF86MonBrightnessUp"   = action-bright-up
    "0+XF86MonBrightnessDown" = action-bright-down
  }
}
```

*Note: The `0+` prefix specifies that no modifier keys (like Super or Alt) are
required.*

Playback keys are bound the same way, through `playerctl`:

```ucl
actions {
  pause-play   = "playerctl play-pause"
  media-next   = "playerctl next"
  media-prev   = "playerctl previous"
}

bindings {
  keyboard {
    "0+XF86AudioPlay"  = action-pause-play
    "0+XF86AudioPause" = action-pause-play
    "0+XF86AudioNext"  = action-media-next
    "0+XF86AudioPrev"  = action-media-prev
  }
}
```

### Lid Switch Handling

Hikari Sakura can natively parse switch events (like lid switches) through the
`inputs { switches { ... } }` block in `hikari.conf`. You can bind these events
to any Hikari Sakura action. For example, to lock the screen when the lid
closes:

```ucl
inputs {
  switches {
    "Control Method Lid Switch" = lock
  }
}
```

However, Hikari Sakura itself does not manage suspend/resume states. On FreeBSD,
suspend is typically handled by `devd(8)` or `acpi(4)`. Ensure
`hw.acpi.lid_switch_state` is set appropriately via `sysctl` or
`/etc/sysctl.conf` to trigger a suspend (e.g. state `S3`) when the lid is
closed.

The screen locker reads the power source each time its blank timer is armed, so
`ui { lock { blank-timeout-ac, blank-timeout-battery } }` can give mains and
battery sessions different blanking behaviour, and unplugging while locked takes
effect on the next keystroke.

## Touchscreen & Trackpad Gestures

Hikari Sakura natively supports touchscreens and multi-finger trackpad gestures.

Touchscreens are attached to the same output layout as the mouse cursor, and
touch input is forwarded to clients via the standard Wayland touch protocol. The
first touch point of a new touch sequence also drives Hikari Sakura's own focus,
raise, move, and resize behavior — the same as a left mouse click — so windows
can be managed by tapping and dragging directly on a touchscreen. Additional
simultaneous touch points are left for the client to interpret (e.g.
pinch-to-zoom in a PDF viewer).

Trackpad swipe, pinch, and hold gestures (via `wlr_pointer_gestures_v1`) can be
bound to Hikari Sakura actions in the `inputs { gestures { ... } }` block, using
keys of the form `swipe-<direction>-<fingers>`, `pinch-<direction>-<fingers>`,
or `hold-<fingers>`, where `<fingers>` is an integer from 1 to 5. A gesture that
matches a binding triggers the action instead of being forwarded to the client;
unmatched gestures are forwarded to the client. Update events are buffered until
the gesture ends (in case it turns out to match a binding); a gesture with more
than 128 update events has the excess silently dropped from what is forwarded.

```ucl
inputs {
  gestures {
    "swipe-left-3"  = workspace-cycle-next
    "swipe-right-3" = workspace-cycle-prev
    "pinch-in-3"    = view-toggle-maximize-full
    "hold-3"        = action-terminal
  }
}
```

## Building

Hikari Sakura currently only works on FreeBSD. This is unlikely to change. When
building directly from the repository, breaking changes might be encountered due
to the project being in its `first` stages; it is currently considered
`unstable`.

### Dependencies

* wlroots (0.20)
* wayland-protocols
* pango
* cairo
* libinput
* xkbcommon
* pixman
* libucl
* evdev-proto
* epoll-shim (FreeBSD)
* XWayland (optional, runtime dependency)
* pandoc (needed by `make install` from a git checkout — see [Building the manpage](#building-the-manpage))

### Compiling and Installing

The build produces three binaries — `hikari`, `hikari-unlocker` and
`hikari-topbar` — plus the `start-hikari` wrapper script.

* `hikari-unlocker` checks credentials for unlocking the screen and is installed
  setuid root (`4555`).
* `hikari-topbar` feeds the native top bar with system telemetry. It runs as a
  separate unprivileged process (`555`) so its sensor polling cannot stall the
  compositor's event loop.
* `hikari` relies on `seatd` to gain the privileges it needs; if your setup
  requires otherwise it can also be installed setuid root — see
  ["Installing with SUID"](#installing-with-suid) below.

Simply run `make`:

```sh
make
```

All four files are installed to `${PREFIX}/bin` by `make install`, which also
installs the default configuration to `${ETC_PREFIX}/etc/hikari/hikari.conf`,
the PAM policy to `${ETC_PREFIX}/etc/pam.d/hikari-unlocker`, the manpage, the
default wallpaper, and the `hikari.desktop` wayland-session entry.

The installation destination can be configured by setting `PREFIX` (default is
`/usr/local` and does not need to be given explicitly). If you want to install
Hikari Sakura into a directory other than `/usr/local` you always should state
the `PREFIX` when issuing `make`, since this information is also used to specify
where Hikari Sakura can find the default configuration on your system and is
needed during the compilation process. To override installation paths for `etc`
specify `ETC_PREFIX`. `make uninstall` requires the same values for prefixes.

### Seeding a user configuration

`make install-user` writes a personal configuration for the invoking user. Run
it as yourself, **without** `sudo` — it writes into `$HOME`, not `DESTDIR`:

```sh
make install-user
```

It copies the default wallpaper to `~/.config/hikari/hikari_wallpaper.png` and
writes `~/.config/hikari/hikari.conf` with the wallpaper path already pointing
at that copy, so the background renders out of the box. Unlike `install`, it
never overwrites an existing `hikari.conf` — it says so and leaves yours alone.

`make uninstall-user` removes the wallpaper copy and deliberately leaves your
configuration in place.

### Optional features

**The `WITH_ALL` switch defaults to `YES`,** which turns on XWayland,
screencopy, gamma control, layer-shell, virtual input and foreign-toplevel
management. A plain `make` gives you all six, so most of the flags below exist
to turn things **off**.

`WITH_ALL` does **not** cover everything. Two switches stay off unless you name
them explicitly, and both are deliberate:

* `WITH_EXT_IMAGE_CAPTURE` — excluded because advertising the protocol makes
  screen sharing worse on hybrid-GPU hardware; see
  [below](#ext-image-copy-capture-opt-in-not-recommended-yet).
* `WITH_SUID` — not a compositor feature but an installation mode: it installs
  the `hikari` binary **setuid root** (`4555` instead of `555`) rather than
  letting it acquire what it needs through `seatd`. Requesting root privileges
  for the compositor is a decision to make on purpose, so it is never implied by
  `WITH_ALL`.

Any `WITH_*` variable given on the command line wins over `WITH_ALL`, so
features can be disabled individually:

```sh
make WITH_SCREENCOPY=NO           # everything except screencopy
make WITH_ALL=NO                  # no optional features at all
make WITH_ALL=NO WITH_XWAYLAND=YES  # XWayland only
```

Every switch accepts `NO` in any case (`no`, `No`, `NO`); any other value —
including `YES` — enables the feature.

| Flag | Default | What it does |
| --- | --- | --- |
| `WITH_XWAYLAND` | on | Runs X11 clients through XWayland. |
| `WITH_SCREENCOPY` | on | `wlr-screencopy`, used by `grim` and by screen sharing. |
| `WITH_GAMMACONTROL` | on | Gamma control, needed by tools like `redshift`. |
| `WITH_LAYERSHELL` | on | `zwlr_layer_shell_v1` — **required by `sofi`**, and by `waybar`, `wofi` and `slurp`. |
| `WITH_VIRTUAL_INPUT` | on | Virtual keyboard and pointer, needed by applications like `wayvnc`. |
| `WITH_FOREIGN_TOPLEVEL_MANAGEMENT` | on | `wlr-foreign-toplevel-management`, which is how external taskbars — including `sofi -show window` — enumerate and activate windows. Its wlroots header declares itself unstable, so the switch exists to build without it if a future wlroots drops the protocol. |
| `WITH_EXT_IMAGE_CAPTURE` | **off** | `ext-image-copy-capture-v1`. Excluded from `WITH_ALL` on purpose — see below. |
| `WITH_SUID` | **off** | Installs `hikari` itself setuid root (`4555` instead of `555`). |

#### ext-image-copy-capture (opt-in, not recommended yet)

`ext-image-copy-capture-v1` is the successor to `wlr-screencopy`, which wlroots
documents as deprecated and intends to remove. Hikari Sakura can advertise it,
but does **not** by default — and it is deliberately excluded from `WITH_ALL`:

```sh
make WITH_EXT_IMAGE_CAPTURE=YES
```

The reason is that `xdg-desktop-portal-wlr` switches to this protocol as soon as
a compositor advertises it (it logs `wayland: using ext_image_copy_capture`), and
on hybrid-GPU hardware that path has been observed to deliver **black frames**
while `wlr-screencopy` captures correctly — so advertising it makes screen
sharing worse on machines where the older protocol works fine. `grim`, which uses
screencopy, is unaffected either way.

Enable it to re-test once your graphics stack moves on; the implementation lives
entirely inside wlroots, so nothing in this compositor needs to change.

#### Installing with SUID

If Hikari Sakura should require root privileges for startup, state `WITH_SUID=YES`
during installation.

```sh
make WITH_SUID=YES install
```

#### Building the manpage

`share/man/man1/hikari.1` is generated from `share/man/man1/hikari.md` by
[`pandoc`](http://pandoc.org/), and is **not** checked into the repository.
What that means in practice:

* `make` does not need pandoc. Building the three binaries never touches the
  manual page.
* **`make install` from a git checkout does need pandoc**, because the manual
  page is one of its prerequisites and no checkout contains it.
* **Installing from a distribution tarball does not.** `make dist` regenerates
  the page before archiving it, so the tarball carries a manual page newer than
  its source and `make install` leaves it alone.

To rebuild it deliberately — which is what to run after editing
`share/man/man1/hikari.md` — use the `doc` target, which regenerates
unconditionally:

```sh
make VERSION=1.0.0 doc
```

`VERSION` is spliced into the page's header line. `make dist VERSION=1.0.0`
runs this for you, so a release tarball's manual page always carries the version
the tarball is named for.

#### Building a DEBUG build

In the case of a crash or a bug you should build a debug version of Hikari Sakura and
try to reproduce the issue. `DEBUG=YES` builds with debug symbols, disables
optimisation, and leaves `assert()` enabled (release builds define `NDEBUG`).
Extracting a stack trace for debugging purposes is also very helpful if you are
planning to submit a bug report.

```sh
make DEBUG=YES
```

AddressSanitizer is **not** enabled by `DEBUG=YES`. It is opt-in, because ASan
interferes with the privileged operations performed before the backend
initialises. Enable it explicitly when you need it:

```sh
make DEBUG=YES ASAN=YES
```

## Launching

Use `start-hikari` to launch the compositor. It sets up the required Wayland
session environment before executing the `hikari` binary:

* Clears leaked `WAYLAND_DISPLAY` / `DISPLAY` variables
* Sets `XDG_SESSION_TYPE`, `XDG_SESSION_CLASS` and
  `XDG_CURRENT_DESKTOP="Hikari Sakura:wlroots"` — both entries of that list
  matter, the first as this desktop's own identity and the second because it is
  what `xdg-desktop-portal-wlr` lists in its `UseIn=` field
* Creates `XDG_RUNTIME_DIR` if the system did not provide one
* Validates `XDG_RUNTIME_DIR` ownership (current user) and permissions (`0700`)
* Warns if `XDG_RUNTIME_DIR` resides on ZFS (incompatible with `posix_fallocate`)
* Redirects output to `$HIKARI_LOG` when that variable is set
* Resolves the `hikari` binary from the script's own directory, `$PATH`, or `./`
* Wraps execution in a D-Bus session if one is not already active

```sh
start-hikari
```

If you are using a display manager — [`sakura`](https://github.com/orpheus497/sakura),
GDM, SDDM or greetd — the installed `hikari.desktop` session file calls
`start-hikari` automatically.

The default configuration expects your default terminal emulator to be specified
in the `$TERMINAL` environment variable.

### Logging and diagnostics

`hikari` and wlroots both log to standard error, and a session started from a
display manager usually discards it — which is why configuration warnings and
crash output can seem to vanish. Set `HIKARI_LOG` to a writable file path and
`start-hikari` redirects its own descriptors there before `exec`:

```sh
HIKARI_LOG=/tmp/hikari.log start-hikari
```

The redirect is a real `exec` rather than a pipe into `tee`, so the compositor
stays the top-level process and its exit status and signal disposition are
preserved — a compositor killed by `SIGSEGV` reports as such instead of as a
clean exit. The trade-off is that output no longer echoes to the terminal while
logging.

## Files

| Path | Contents |
| --- | --- |
| `$XDG_CONFIG_HOME/hikari/hikari.conf` | User configuration. Falls back to `~/.config/hikari/hikari.conf` when `XDG_CONFIG_HOME` is unset. |
| `${ETC_PREFIX}/etc/hikari/hikari.conf` | System-wide default configuration, used when no user configuration is readable. |
| `~/.config/hikari/autostart` | Executed at startup if readable and executable. |
| `${ETC_PREFIX}/etc/pam.d/hikari-unlocker` | PAM policy used by `hikari-unlocker`. |
| `${PREFIX}/share/wayland-sessions/hikari.desktop` | Session entry read by display managers. |
| `${PREFIX}/share/backgrounds/hikari/hikari_wallpaper.png` | Default wallpaper. |
| `$XDG_RUNTIME_DIR/hikari.sock` | Control socket, mode `0600`. Removed on exit. |

## Environment

| Variable | Effect |
| --- | --- |
| `XDG_RUNTIME_DIR` | Wayland socket directory. Must be user-owned and mode `0700`; `start-hikari` creates and validates it. |
| `XDG_CONFIG_HOME` | Configuration directory root. Falls back to `$HOME/.config`. |
| `TERMINAL` | Terminal emulator used by the default configuration. |
| `HIKARI_LOG` | File path for `start-hikari` to capture compositor output to. |
| `XKB_DEFAULT_LAYOUT`, `XKB_DEFAULT_MODEL`, `XKB_DEFAULT_OPTIONS`, `XKB_DEFAULT_RULES` | Fallback keyboard configuration for keyboards not configured in `hikari.conf`. Read once at startup. |
| `XDG_CURRENT_DESKTOP` | Set by `start-hikari` to `Hikari Sakura:wlroots` for portal backend selection. |
| `DBUS_SESSION_BUS_ADDRESS` | If unset, `start-hikari` wraps the compositor in `dbus-run-session`. |

Any environment variable can also be referenced from string values in
`hikari.conf` as `$VARIABLE` or `${VARIABLE}`; write `$$` to escape it.

## Documentation

* `man hikari` — the complete reference: concepts, every action name, bindings,
  layouts, the UI/palette/colorscheme/animation/lock/bar configuration, inputs,
  outputs, and the control socket protocol.
* `etc/hikari/hikari.conf` — the annotated default configuration.
* [sofi](https://github.com/orpheus497/sofi) and
  [sakura](https://github.com/orpheus497/sakura) document their own
  configuration in their respective repositories.

## Contributing

Please make sure you use `clang-format` with the accompanying `.clang-format`
configuration before submitting any patches.

## License

Hikari Sakura is released under the 2-clause BSD license. See [LICENSE](LICENSE).
