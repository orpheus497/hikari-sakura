/* [COMMENT] Script function and purpose: Resolve the evdev input code
definitions -- KEY_*, BTN_* and the EV_* event types -- to whichever header the
host system puts them in.

The codes themselves are the same numbers on both platforms, and that is by
design rather than by luck: FreeBSD's evdev is a compatibility implementation
of the Linux input layer, and the devel/evdev-proto port installs a copy of the
Linux header under dev/evdev/. So BTN_LEFT is 0x110 in both, and a binding
configuration is portable between the two without translation. Only the include
path differs.

This header exists so that fact is written down once. Four source files need
these definitions (configuration.c, cursor.c, binding_config.c and
pointer_config.c), and a guarded include copied into each of them would be four
places to keep in agreement and four places for the explanation to rot.

Deliberately NOT folded into hikari/compat.h, because the two headers serve
different sets of binaries. compat.h is reached by the compositor and, since
its rule gained -Iinclude, by hikari-unlocker, which needs explicit_bzero();
hikari-topbar reaches neither header and is built from the TOPBAR_CFLAGS
snapshot precisely so it links nothing but libc. Folding the input layer into
compat.h would push evdev's definitions into hikari-unlocker, which reads no
input codes and has no business seeing them. */

#if !defined(HIKARI_EVDEV_H)
#define HIKARI_EVDEV_H

#if defined(__linux__)
#include <linux/input-event-codes.h>
#else
#include <dev/evdev/input-event-codes.h>
#endif

#endif
