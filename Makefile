# [COMMENT] Action purpose: Every WITH_* switch below is tested with
# `defined(X) && ${X:tu} != "NO"` rather than the shorter `.ifdef X`, because
# `.ifdef` tests whether a variable is DEFINED, not what it is set to -- so
# `make WITH_XWAYLAND=NO` previously still compiled XWayland in, and there was
# no way to turn any feature off from the command line at all. `:tu` upper-cases
# the value first so `no`, `No` and `NO` all work. Command-line variables cannot
# be removed with `.undef` (verified: bmake keeps them in the cmdline scope and
# the subsequent `.ifdef` still matches), which is why the check has to be made
# at each site rather than normalised once up here.
WITH_ALL = YES

.if defined(WITH_ALL) && ${WITH_ALL:tu} != "NO"
WITH_XWAYLAND = YES
WITH_SCREENCOPY = YES
WITH_GAMMACONTROL = YES
WITH_LAYERSHELL = YES
WITH_VIRTUAL_INPUT = YES
WITH_FOREIGN_TOPLEVEL_MANAGEMENT = YES
.endif

OS != uname
VERSION ?= "CURRENT"
PREFIX ?= /usr/local
PKG_CONFIG ?= pkg-config
# [COMMENT] Action purpose: Where this install writes etc/. On FreeBSD that is
# under the prefix -- /usr/local/etc -- which is both the port convention and a
# path OpenPAM searches, so ETC_PREFIX tracks PREFIX and nothing more is needed.
#
# Linux is the exception, and not for style reasons: Linux-PAM resolves a
# service policy under /etc/pam.d, and that location is compiled into the
# library rather than being relocatable by whoever installs a consumer. A
# policy written to /usr/local/etc/pam.d is never read, so hikari-unlocker --
# which is the entire screen-unlock path -- would fail to authenticate with
# nothing to indicate why. An empty ETC_PREFIX makes every ${ETC_PREFIX}/etc
# site below resolve to /etc, which is also where FHS puts configuration
# regardless of the binary prefix.
#
# Empty is deliberate rather than literal "/etc": the sites below all append
# /etc themselves, and the same variable feeds -DHIKARI_ETC_PREFIX, whose
# stringified expansion in main.c appends "/etc/hikari/hikari.lua". Verified
# that an empty -D value stringifies to "" and yields exactly
# /etc/hikari/hikari.lua under -std=c11 -Wall -Wextra -pedantic.
#
# Still `?=`, so a distribution packager can override it as before.
.if ${OS} == "Linux"
ETC_PREFIX ?=
.else
ETC_PREFIX ?= ${PREFIX}
.endif

# [COMMENT] Action purpose: The two knobs the Linux PAM policy selection in
# `install` reads. Linux has no single system-wide authentication stack --
# Debian, Ubuntu and openSUSE provide common-auth, Fedora, RHEL and Arch
# provide system-auth -- so the recipe probes the machine rather than guessing
# the distribution, which is the same principle src/platform.c applies.
#
# PAM_CONFDIR is the pam.d of the LIVE target system, deliberately NOT under
# DESTDIR. DESTDIR is a staging root for package building; it holds no
# common-auth to find. The probe reads the real system and the policy is still
# written under DESTDIR.
#
# PAM_STACK names the stack outright and skips the probe. It is the documented
# escape hatch for two cases. First, a distribution we do not recognise, where
# the admin knows the answer and should not have to patch the build. Second,
# and this is the one that matters: a CROSS-DISTRO PACKAGE BUILD, where probing
# the build host is meaningless because the build host is not the target host.
# A packager building a Fedora RPM on a Debian builder MUST pass
# PAM_STACK=system-auth; nothing the probe can do will work that out for them.
# This is a real, named limitation of install-time detection, not a bug in it.
#
# Valid values are the suffixes of the etc/pam.d/hikari-unlocker.Linux.* files:
# common-auth and system-auth. Both `make install PAM_STACK=system-auth` and
# `PAM_STACK=system-auth make install` work, because bmake imports the
# environment.
#
# Neither variable has any effect on FreeBSD, which keeps its single
# etc/pam.d/hikari-unlocker.FreeBSD policy selected by OS.
PAM_CONFDIR ?= /etc/pam.d
PAM_STACK ?=

OBJS = \
	action.o \
	action_config.o \
	animation.o \
	binding_config.o \
	binding_group.o \
	bar.o \
	blur.o \
	border.o \
	buffer.o \
	command.o \
	completion.o \
	configuration.o \
	config_lua.o \
	cursor.o \
	decoration.o \
	dnd_mode.o \
	exec.o \
	font.o \
	foreign_toplevel.o \
	ipc.o \
	geometry.o \
	gesture_config.o \
	group.o \
	group_assign_mode.o \
	indicator.o \
	indicator_bar.o \
	indicator_frame.o \
	input_buffer.o \
	input_grab_mode.o \
	keyboard.o \
	keyboard_config.o \
	layer_shell.o \
	layout.o \
	layout_config.o \
	layout_policy.o \
	layout_select_mode.o \
	lock_clock.o \
	lock_config.o \
	lock_indicator.o \
	lock_mode.o \
	main.o \
	mark.o \
	mark_assign_mode.o \
	mark_select_mode.o \
	maximized_state.o \
	memory.o \
	move_mode.o \
	normal_mode.o \
	output.o \
	output_config.o \
	output_management.o \
	platform.o \
	pointer.o \
	pointer_config.o \
	pointer_constraints.o \
	position_config.o \
	reflow.o \
	resize_mode.o \
	screen_capture.o \
	server.o \
	sheet.o \
	sheet_assign_mode.o \
	split.o \
	switch.o \
	switch_config.o \
	tile.o \
	touch.o \
	view.o \
	view_config.o \
	workspace.o \
	xdg_view.o

.if defined(WITH_XWAYLAND) && ${WITH_XWAYLAND:tu} != "NO"
OBJS += \
	xwayland_unmanaged_view.o \
	xwayland_view.o
.endif

WAYLAND_PROTOCOLS != ${PKG_CONFIG} --variable pkgdatadir wayland-protocols

.PHONY: distclean clean clean-doc doc dist install uninstall install-user uninstall-user FORCE
.PATH: src

# Allow specification of /extra/ CFLAGS and LDFLAGS
CFLAGS += ${CFLAGS_EXTRA}
LDFLAGS += ${LDFLAGS_EXTRA}

# [COMMENT] Action purpose: Undo the sysroot redirection portable bmake applies
# to DESTDIR, which otherwise makes `bmake install DESTDIR=...` -- the standard
# staged install every distribution packager performs -- fail to compile.
#
# bmake's own /usr/share/mk/sys/Linux.mk contains, verbatim:
#
#     .if defined(DESTDIR)
#     CPPFLAGS+=	-nostdinc -idirafter ${DESTDIR}/usr/include
#     .endif
#
# The same three lines appear in its AIX, HP-UX, IRIX, OSF1, SunOS and UnixWare
# files. There DESTDIR means a cross-build sysroot holding a target system's
# headers. Here it means what GNU conventions and every packaging tool mean by
# it -- a staging directory to install INTO -- and it holds no headers at all,
# so the redirection points the compiler at an empty directory and the first
# #include fails: "assert.h: No such file or directory". Measured, before this
# block existed, on a tree that builds cleanly without DESTDIR.
#
# This Makefile sets no CPPFLAGS of its own; the injected flags reach the
# compiler only through bmake's built-in .c.o rule, whose COMPILE.c expands
# ${CPPFLAGS}. Removing exactly the three words sys.mk added therefore costs
# nothing and changes nothing else.
#
# ONLY -nostdinc is removed, and only that one word. It is the word that does
# the damage: with it gone, `cc -idirafter /empty/usr/include` compiles the test
# case above cleanly, because -idirafter is searched AFTER the standard
# directories and a -I naming a directory that does not exist is silently
# ignored. So the surviving flag is a no-op and does not need removing.
#
# Removing -idirafter as well would be worse than leaving it. The flag takes its
# directory as a SEPARATE word, and `:N` filters words, not sequences -- so
# stripping `-idirafter` from a CPPFLAGS that already contained one would leave
# its argument behind as a bare word, which the compiler reads as an input FILE.
# Measured: with CPPFLAGS='-idirafter /opt/inc' in the environment and DESTDIR
# set, the earlier three-word filter reduced it to the single word `/opt/inc`.
# -nostdinc takes no argument, so it cannot do that.
#
# A no-op wherever the word is absent, which is why this cannot affect FreeBSD
# -- whose sys.mk was not verified here, there being no FreeBSD host -- whatever
# that sys.mk does. Note also that a CPPFLAGS set on the COMMAND LINE is
# immutable and reaches the compiler unfiltered, which is correct: make does not
# let a makefile override it, and sys.mk's += cannot append to it either, so
# there is nothing to undo in that case.
.if defined(DESTDIR)
CPPFLAGS := ${CPPFLAGS:N-nostdinc}
.endif

.if defined(DEBUG) && ${DEBUG:tu} != "NO"
# [COMMENT] Action purpose: Debug build — full symbols, no optimisation, strict
# warnings. ASan is deliberately excluded from the base debug build because
# wlroots/GBM maps DMA buffers via mmap(2) directly; ASan intercepts those
# calls and either false-positives or crashes the compositor before the DRM
# backend initialises. Enable ASan only when explicitly needed via ASAN=YES
# (e.g. unit-testing non-graphics code paths).
CFLAGS += -g -Werror -Wno-unused-function -Wno-unused-variable -O0
.if "${ASAN}" == "YES"
CFLAGS += -fsanitize=address
LDFLAGS += -fsanitize=address
.endif
.else
CFLAGS += -DNDEBUG
.endif

# [COMMENT] Action purpose: Snapshot the shared DEBUG/ASAN/warning/hardening
# flags for hikari-topbar here, before the feature macros and the pkg-config
# include paths are appended to CFLAGS below. The `:=` is load-bearing: with a
# deferred `=` this would expand at use time and the topbar build would inherit
# every -DHAVE_* and every wlroots/pango include it has no use for.
#
# -Iinclude is appended by hand rather than inherited, and it is the ONLY
# include path this binary gets. CFLAGS does not carry it yet at this point in
# the file -- the `-I. -Iinclude` line sits further down, next to the feature
# macros this snapshot exists to keep out -- but topbar.c and the two sensor
# translation units all include <hikari/topbar_sensors.h>, so without it the
# compile cannot find the project's own header. `-Iinclude` and not `-I.`: the
# contract header is the single project header this binary needs, and leaving
# the tree root off the search path keeps a stray top-level header from being
# picked up by a bare #include in a libc-only program.
TOPBAR_CFLAGS := ${CFLAGS} -std=gnu11 -Wall -Iinclude

# [COMMENT] Action purpose: Select the platform half of the top bar's sensor
# contract. include/hikari/topbar_sensors.h declares five functions; exactly
# one implementation of it is compiled, chosen here by ${OS}, and src/topbar.c
# holds everything portable. FreeBSD is the default branch rather than a tested
# one, so a platform this file has never seen gets the sysctl implementation
# and a link error naming the missing symbol -- which is the honest outcome --
# instead of silently building against Linux's /proc and /sys.
#
# The object names are derived from the source names with `:.c=.o` so the two
# can never drift apart, and the pair is collected into TOPBAR_OBJS for the
# link rule and the `clean` target to share.
.if ${OS} == "Linux"
TOPBAR_SENSORS_SRC = topbar_sensors_linux.c
.else
TOPBAR_SENSORS_SRC = topbar_sensors_freebsd.c
.endif

TOPBAR_SENSORS_OBJ = ${TOPBAR_SENSORS_SRC:.c=.o}
TOPBAR_OBJS = topbar.o ${TOPBAR_SENSORS_OBJ}

.if defined(WITH_XWAYLAND) && ${WITH_XWAYLAND:tu} != "NO"
CFLAGS += -DHAVE_XWAYLAND=1
.endif

.if defined(WITH_GAMMACONTROL) && ${WITH_GAMMACONTROL:tu} != "NO"
CFLAGS += -DHAVE_GAMMACONTROL=1
.endif

.if defined(WITH_SCREENCOPY) && ${WITH_SCREENCOPY:tu} != "NO"
CFLAGS += -DHAVE_SCREENCOPY=1
.endif

# [COMMENT] Action purpose: ext-image-copy-capture-v1 is OPT-IN and deliberately
# excluded from WITH_ALL, which is why it is tested here rather than being set
# above with the others.
#
# It is the modern replacement for wlr-screencopy and wlroots' own header says
# screencopy "will be dropped in a future wlroots version", so hikari wants it
# eventually. But xdg-desktop-portal-wlr PREFERS it the moment a compositor
# advertises it (it logs "wayland: using ext_image_copy_capture"), and on this
# hardware that path produces black frames while wlr-screencopy captures
# correctly -- verified by `grim`, which uses screencopy and works. Advertising
# it therefore makes screen sharing WORSE on a machine where the older protocol
# is fine, because the client silently switches to the broken path.
#
# The implementation is entirely inside wlroots; hikari only creates the two
# globals, so there is nothing here to fix. Enable with
# `make WITH_EXT_IMAGE_CAPTURE=YES` to re-test when wlroots or the graphics
# stack moves on -- most likely once the hybrid-GPU dmabuf issue tracked as
# FB-3 in BLUEPRINT.md section 13 is resolved.
.if defined(WITH_EXT_IMAGE_CAPTURE) && ${WITH_EXT_IMAGE_CAPTURE:tu} != "NO"
CFLAGS += -DHAVE_EXT_IMAGE_CAPTURE=1
.endif

# [COMMENT] Action purpose: zwlr_foreign_toplevel_management_v1 -- the acting
# half of window listing, and the only protocol that lets an external window
# switcher or task manager focus, close or minimise another client's window.
# Included in WITH_ALL, unlike WITH_EXT_IMAGE_CAPTURE above, because it costs
# nothing to advertise and nothing else regresses when it is present.
#
# It has a switch at all because its wlroots header opens with "This an unstable
# interface of wlroots. No guarantees are made regarding the future consistency
# of this API", and its listing half is already superseded by the
# standards-track ext-foreign-toplevel-list-v1 that hikari also advertises. If a
# future wlroots drops it, `make WITH_FOREIGN_TOPLEVEL_MANAGEMENT=NO` keeps the
# tree building while the replacement is written: src/foreign_toplevel.c carries
# stub definitions so every call site in view.c stays unconditional.
.if defined(WITH_FOREIGN_TOPLEVEL_MANAGEMENT) && \
    ${WITH_FOREIGN_TOPLEVEL_MANAGEMENT:tu} != "NO"
CFLAGS += -DHAVE_FOREIGN_TOPLEVEL_MANAGEMENT=1
.endif

.if defined(WITH_LAYERSHELL) && ${WITH_LAYERSHELL:tu} != "NO"
CFLAGS += -DHAVE_LAYERSHELL=1
.endif

.if defined(WITH_SUID) && ${WITH_SUID:tu} != "NO"
PERMS = 4555
.else
PERMS = 555
.endif

.if defined(WITH_VIRTUAL_INPUT) && ${WITH_VIRTUAL_INPUT:tu} != "NO"
CFLAGS += -DHAVE_VIRTUAL_INPUT=1
.endif

# [COMMENT] Action purpose: HIKARI_PREFIX resolves the setuid unlocker and the
# top bar helper through compile-time absolute paths, so a modified PATH cannot
# substitute a different binary into either pipeline.
CFLAGS += -Wall -I. -Iinclude -DHIKARI_ETC_PREFIX=${ETC_PREFIX} -DHIKARI_PREFIX=${PREFIX}
CFLAGS += -DHIKARI_TOPBAR_PATH='"${PREFIX}/bin/hikari-topbar"'

# [COMMENT] Action purpose: Emit a make-readable dependency list beside every
# object file, so that editing a header rebuilds every object that includes it.
# Without this the Makefile declares exactly one header dependency in total
# (`main.o: version.h` below), which means a change to a struct in
# include/hikari/ recompiles only the .c files that were themselves edited and
# leaves every other object linked at the OLD field offsets. That is not a
# stale-build annoyance -- it silently produces a binary in which two objects
# disagree about where a struct member lives, and it cost a boot-failing
# compositor once already (a pointer added to struct hikari_server moved
# output_layout by eight bytes; output.o read the old offset and dereferenced
# NULL at startup).
#
# -MMD writes <object>.d as a side effect of the compile itself, so it costs no
# extra pass, and skips system headers -- those are the ones that do not change
# between builds and would only bloat the file. -MP adds a phony target for
# each header, which is what stops make from erroring out with "no rule to make
# target" when a header is deleted or renamed rather than edited.
#
# Deliberately placed AFTER the TOPBAR_CFLAGS snapshot above: hikari-topbar is
# compiled straight to an executable rather than to an object, so a dependency
# file for it would describe a target that no rule ever consults.
CFLAGS += -MMD -MP

WLROOTS_CFLAGS != ${PKG_CONFIG} --cflags wlroots-0.20
WLROOTS_LIBS != ${PKG_CONFIG} --libs wlroots-0.20

WLROOTS_CFLAGS += -DWLR_USE_UNSTABLE

PANGO_CFLAGS != ${PKG_CONFIG} --cflags pangocairo
PANGO_LIBS != ${PKG_CONFIG} --libs pangocairo

CAIRO_CFLAGS != ${PKG_CONFIG} --cflags cairo
CAIRO_LIBS != ${PKG_CONFIG} --libs cairo

PIXMAN_CFLAGS != ${PKG_CONFIG} --cflags pixman-1
PIXMAN_LIBS != ${PKG_CONFIG} --libs pixman-1

XKBCOMMON_CFLAGS != ${PKG_CONFIG} --cflags xkbcommon
XKBCOMMON_LIBS != ${PKG_CONFIG} --libs xkbcommon

WAYLAND_CFLAGS != ${PKG_CONFIG} --cflags wayland-server
WAYLAND_LIBS != ${PKG_CONFIG} --libs wayland-server

LIBINPUT_CFLAGS != ${PKG_CONFIG} --cflags libinput
LIBINPUT_LIBS != ${PKG_CONFIG} --libs libinput

# [COMMENT] Action purpose: Locate Lua 5.4, which replaced libucl as the
# configuration language. The module name is NOT portable: FreeBSD's lang/lua54
# installs lua-5.4.pc, while Debian and Ubuntu ship lua5.4.pc plus lua-5.4,
# lua54 and a bare lua as aliases, and other distributions pick differently
# again. Probing in order and taking the first hit is what keeps one Makefile
# working everywhere; hardcoding any single spelling breaks at least one target.
#
# The loop is a `!=` shell assignment rather than a .for over .if ${PKG_CONFIG}
# because bmake evaluates `!=` once at parse time, so pkg-config runs at most
# four times for the whole build rather than once per expansion.
# The constraint is not decoration. --exists alone accepts whatever the module
# happens to be, and the unversioned `lua` module at the end of this list is a
# different version on different distributions. Lua 5.5 gave lua_newstate a
# third seed argument, which src/config_lua.c does not pass, so an unconstrained
# probe on such a machine selects a module that cannot compile and the build
# dies on a wrong-argument-count error pointing at a call site that looks
# correct -- not on the .error below, which exists to say something useful.
# src/config_lua.c carries a matching #if LUA_VERSION_NUM guard for the case
# where LUA_CFLAGS is set by hand and never reaches this probe at all.
LUA_PKG != for m in lua-5.4 lua5.4 lua54 lua; do \
	if ${PKG_CONFIG} --exists "$$m >= 5.4" "$$m < 5.5" 2>/dev/null; then \
		echo $$m; break; fi; \
	done

# [COMMENT] Action purpose: Fail at parse time with a sentence a human can act
# on. Without this the two assignments below expand to empty, the compile dies
# on a missing lua.h, and nothing says which package is absent.
.if empty(LUA_PKG)
.error Lua 5.4 development files not found. Tried pkg-config modules lua-5.4, lua5.4, lua54 and lua. Install FreeBSD lang/lua54, Debian/Ubuntu liblua5.4-dev, Fedora lua-devel or Arch lua.
.endif

LUA_CFLAGS != ${PKG_CONFIG} --cflags ${LUA_PKG}
LUA_LIBS != ${PKG_CONFIG} --libs ${LUA_PKG}

.if ${OS} == "FreeBSD"
EPOLL_SHIM_CFLAGS != ${PKG_CONFIG} --cflags epoll-shim
EPOLL_SHIM_LIBS != ${PKG_CONFIG} --libs epoll-shim
.endif

CFLAGS += \
	${WLROOTS_CFLAGS} \
	${PANGO_CFLAGS} \
	${CAIRO_CFLAGS} \
	${PIXMAN_CFLAGS} \
	${XKBCOMMON_CFLAGS} \
	${WAYLAND_CFLAGS} \
	${LIBINPUT_CFLAGS} \
	${LUA_CFLAGS} \
	${EPOLL_SHIM_CFLAGS}

LIBS = \
	${WLROOTS_LIBS} \
	${PANGO_LIBS} \
	${CAIRO_LIBS} \
	${PIXMAN_LIBS} \
	${XKBCOMMON_LIBS} \
	${WAYLAND_LIBS} \
	${LIBINPUT_LIBS} \
	${LUA_LIBS} \
	${EPOLL_SHIM_LIBS}

PROTOCOL_HEADERS = xdg-shell-protocol.h

.if defined(WITH_LAYERSHELL) && ${WITH_LAYERSHELL:tu} != "NO"
PROTOCOL_HEADERS += wlr-layer-shell-unstable-v1-protocol.h
.endif

all: hikari hikari-unlocker hikari-topbar

# [COMMENT] Action purpose: Regenerate version.h on every build. The phony
# FORCE prerequisite keeps the target permanently out of date; the header is
# written to a temporary file and atomically renamed, so an interrupted build
# can never leave a partial or empty version.h behind (the rename only runs
# after the write succeeds).
version.h: FORCE
	echo "#define HIKARI_VERSION \"${VERSION}\"" > version.h.tmp && mv version.h.tmp version.h

FORCE:

main.o: version.h

# [COMMENT] Action purpose: Every object needs the generated protocol headers,
# not merely the link step. `hikari:` lists ${PROTOCOL_HEADERS} and ${OBJS} as
# prerequisites of the SAME target, and prerequisites are unordered, so a
# parallel build schedules wayland-scanner and cc in one batch. Most objects
# reach wlr-layer-shell-unstable-v1-protocol.h through include/hikari/server.h
# -> hikari/layer_shell.h -> wlr/types/wlr_layer_shell_v1.h, which resolves it
# from the `-I.` at the tree root. The .d files do record that edge, but only
# once a build has already succeeded, and `clean` deletes them again.
#
# Measured on a clean checkout: `bmake border.o` fails outright with
# "wlr-layer-shell-unstable-v1-protocol.h: No such file or directory", which is
# the whole proof -- nothing but this line states the dependency. The build
# normally survives only because wayland-scanner happens to finish before the
# first cc reaches the include. Stating it here fixes `bmake -j` on a clean
# tree and a direct `bmake <object>.o` alike.
${OBJS}: ${PROTOCOL_HEADERS}

hikari: version.h ${PROTOCOL_HEADERS} ${OBJS}
	${CC} ${LDFLAGS} ${CFLAGS} ${INCLUDES} -o ${.TARGET} ${OBJS} ${LIBS}

xdg-shell-protocol.h:
	wayland-scanner server-header ${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml ${.TARGET}

wlr-layer-shell-unstable-v1-protocol.h:
	wayland-scanner server-header protocol/wlr-layer-shell-unstable-v1.xml ${.TARGET}

# [COMMENT] Action purpose: -Iinclude so this rule can reach hikari/compat.h.
# It could not before, and that was the one binary where it mattered most:
# hikari_unlocker.c calls explicit_bzero() six times on password buffers, and
# with no -I at all it could only ever get that symbol from libc. On a C library
# that does not export it -- which is exactly the case compat.h was written for
# -- the setuid-root binary handling passwords failed to LINK, while the two
# binaries that merely display things could compile the shim in. The header adds
# nothing on FreeBSD and pulls in no library on Linux, so this does not widen
# what the rule links; it only lets the declaration be found.
#
# [COMMENT] Action purpose: compat.h is a prerequisite for the same reason it is
# one for the top bar objects below -- this rule uses ${CFLAGS_EXTRA}, not
# ${CFLAGS}, and -MMD -MP lives only in the latter. The rule therefore emits no
# .d file and nothing else records that hikari_unlocker.c includes the header.
# Measured: editing include/hikari/compat.h and re-running the target printed
# "`hikari-unlocker' is up to date" and left the old binary byte-for-byte in
# place. That matters more here than anywhere else in the tree, because this is
# the setuid-root binary and the shim it would be missing is the explicit_bzero
# it calls on password buffers.
hikari-unlocker: hikari_unlocker.c include/hikari/compat.h
	${CC} ${CFLAGS_EXTRA} ${LDFLAGS_EXTRA} -Iinclude -o hikari-unlocker hikari_unlocker.c -lpam

# [COMMENT] Action purpose: Build the top bar telemetry helper as its own
# binary. It is deliberately NOT linked into the compositor: it samples sensors
# with blocking popen() calls, which would stall the Wayland event loop if run
# in-process. hikari reads its swaybar-protocol output over a non-blocking pipe.
# It shares the project's DEBUG/ASAN/warning/hardening/GNU11 settings via the
# TOPBAR_CFLAGS snapshot above, but none of the compositor's feature macros or
# wlroots include paths. Note that topbar.c relies on __BSD_VISIBLE staying set
# for u_int, IFF_UP and usleep -- no feature-test macro may be defined for this
# target (see the comment atop src/topbar.c).
#
# Two translation units since the platform sensor split: the portable half in
# src/topbar.c and the ${OS}-selected half named by TOPBAR_SENSORS_SRC above.
#
# Both are compiled by the EXPLICIT rules below rather than by the built-in
# .c.o suffix rule, and that is the whole reason the rules exist. `.PATH: src`
# makes every source in src/ visible to the suffix rule, which compiles with
# ${CFLAGS} -- the compositor's flags, carrying -DHAVE_*, -MMD -MP and the
# wlroots/pango/pixman include paths. A rule with its own commands takes
# precedence over the suffix rule in bmake, so naming these two objects here is
# what keeps TOPBAR_CFLAGS, and therefore the libc-only property, intact.
#
# The header is a prerequisite because nothing else records that dependency:
# -MMD lives in ${CFLAGS}, not in the snapshot, so these objects emit no .d
# file and an edit to the sensor contract would otherwise not rebuild them.
hikari-topbar: ${TOPBAR_OBJS}
	${CC} ${LDFLAGS} ${TOPBAR_CFLAGS} -o ${.TARGET} ${TOPBAR_OBJS}

topbar.o: src/topbar.c include/hikari/topbar_sensors.h
	${CC} ${TOPBAR_CFLAGS} -c -o ${.TARGET} src/topbar.c

${TOPBAR_SENSORS_OBJ}: src/${TOPBAR_SENSORS_SRC} include/hikari/topbar_sensors.h
	${CC} ${TOPBAR_CFLAGS} -c -o ${.TARGET} src/${TOPBAR_SENSORS_SRC}

# [COMMENT] Action purpose: hikari.1 is generated and .gitignore'd, so removing
# it is exactly what `clean` should do. The `_darcs` test this replaces was a
# leftover from before the project moved to git and matched nothing, which made
# clean-doc a silent no-op -- leaving a stale manual page behind every clean.
clean-doc:
	@echo "cleaning manpage"
	@rm -f share/man/man1/hikari.1

clean: clean-doc
	@echo "cleaning headers"
	@rm -f version.h
	@rm ${PROTOCOL_HEADERS} 2> /dev/null ||:
	@echo "cleaning object files"
	@rm ${OBJS} 2> /dev/null ||:
# [COMMENT] Action purpose: The top bar's objects are removed on their own
# line, and both platform names are listed rather than just ${TOPBAR_OBJS}.
# They are not in ${OBJS} -- they are compiled with TOPBAR_CFLAGS instead --
# and ${TOPBAR_OBJS} names only the object for the ${OS} running the clean, so
# a tree built on one platform and cleaned under `make OS=...` from the other
# would otherwise keep a stale object that the next link silently reuses.
	@rm topbar.o topbar_sensors_linux.o topbar_sensors_freebsd.o 2> /dev/null ||:
	@echo "cleaning dependency files"
	@rm ${OBJS:.o=.d} 2> /dev/null ||:
	@echo "cleaning executables"
	@rm hikari 2> /dev/null ||:
	@rm hikari-unlocker 2> /dev/null ||:
	@rm hikari-topbar 2> /dev/null ||:

# [COMMENT] Action purpose: One definition, used by both rules below, so the
# roff page and the `doc` target can never be generated with different titles.
PANDOC_MAN = pandoc -M title:"HIKARI(1) ${VERSION} | Hikari Sakura" -s \
	--to man -o share/man/man1/hikari.1 share/man/man1/hikari.md

# [COMMENT] Action purpose: The prerequisite on the markdown source is the point
# of this rule. Without it -- as was the case previously -- editing
# share/man/man1/hikari.md never rebuilt the roff page, so the installed manual
# could drift arbitrarily far from the source it claims to be generated from.
#
# It costs nothing to add, because hikari.1 is a generated artefact that is
# .gitignore'd and NOT committed: a git checkout has no hikari.1 at all, so this
# rule fires from scratch and pandoc is required for `make install` from the
# repository either way. In an unpacked distribution tarball both files are
# present with tar-preserved timestamps, and `dist` regenerates the page before
# archiving it, so the .1 is always the newer of the two and pandoc is not
# needed to install from a tarball.
share/man/man1/hikari.1: share/man/man1/hikari.md
	${PANDOC_MAN}

.PHONY: doc
doc:
	${PANDOC_MAN}

# [COMMENT] Action purpose: Every path listed here must exist, because tar
# fails the whole archive on a missing member. `CoC.md` and `CHANGELOG.md` were
# listed but have never been in this tree, so `make dist` could not produce a
# tarball at all; the `darcs revert` that preceded it was an upstream leftover
# from before this project moved to git, and reverting the working tree during
# a build is not something a dist target should do regardless of the VCS.
# The prerequisite is `doc`, not share/man/man1/hikari.1: the committed roff
# page carries whatever VERSION it was last generated with in its .TH line, and
# the file already existing would satisfy a file prerequisite without
# regenerating it -- shipping a 1.0.0 tarball whose manual page says CURRENT.
# `doc` is phony and always regenerates, so the page in the archive matches the
# VERSION the archive is named for. This is why `dist` needs pandoc while
# `install` deliberately does not.
# [COMMENT] Action purpose: The archive member list, lifted out of the recipe
# so the tar invocation below can be written once per platform without
# duplicating twenty-four paths. Nothing here may ever contain a `#`.
DIST_FILES = \
		version.h \
		main.c \
		hikari_unlocker.c \
		include/hikari/*.h \
		src/*.c \
		protocol/*.xml \
		Makefile \
		GNUmakefile \
		test.mk \
		compile_flags.txt \
		.clang-format \
		LICENSE \
		README.md \
		start-hikari.sh \
		share/man/man1/hikari.md \
		share/man/man1/hikari.1 \
		share/hikari_sakura_alpha.png \
		share/backgrounds/hikari/hikari_wallpaper.png \
		share/wayland-sessions/hikari.desktop \
		share/wayland-sessions/hikari-sakura.desktop \
		share/xdg-desktop-portal/sakura-portals.conf \
		etc/hikari/hikari.lua \
		etc/pam.d/hikari-unlocker.*

# [COMMENT] Action purpose: Stage the files under a directory named after the
# release, then archive that directory. The point of the two spellings this
# replaces was only to prefix every path with "hikari-${VERSION}/", and each was
# specific to one tar: --transform is GNU tar's, -s is bsdtar's, and neither
# implementation accepts the other's. That is why this rule used to be branched
# on ${OS}, and the branch is the thing worth deleting -- it meant `make dist`
# was the one target whose behaviour depended on which tar happened to be
# installed rather than on which platform it claimed to support.
#
# Having the files actually LIVE in a directory of that name needs no flag at
# all. What is left -- -c, -f, -x and -z -- is present in GNU tar, bsdtar and
# FreeBSD's base tar alike, so one recipe now serves every platform and every
# implementation. The pipe into a subshell is the portable directory copy: POSIX
# cp(1) cannot create the intermediate directories that include/hikari/ and
# src/ need, and tar can.
#
# Deliberately NOT switched to bsdtar on Linux. bsdtar is not installed by
# default on the common distributions -- on this machine it comes from Debian's
# libarchive-tools, a separate package -- while GNU tar is the default, so
# requiring bsdtar would ADD a build dependency and break `make dist` on a
# stock system. Requiring neither is strictly better than requiring either.
# Two further things this recipe gets right, both of which the obvious spelling
# gets wrong.
#
# It stages under hikari-${VERSION}.stage and NOT under hikari-${VERSION}. The
# second is the name an extracted release has, so a recipe that began
# `rm -rf hikari-${VERSION}` would delete a tree someone had unpacked beside the
# checkout and been editing -- and `make dist` is exactly the command whose user
# is most likely to have one lying around. The suffixed name is this build's
# own, and nothing else is expected to create it.
#
# It also does NOT pipe one tar into the other. A pipeline's status is the LAST
# command's, so a source tar that fails part way -- a DIST_FILES entry missing
# or unreadable -- is masked by a successful extract, and `make dist` reports
# success while shipping an incomplete archive. Measured: `tar -cf - present.txt
# missing.txt | (cd stage && tar -xf -)` exits 0 with only present.txt staged.
# Writing the intermediate archive to a file instead puts each tar on its own
# recipe line, where bmake stops at the first non-zero exit, so a missing member
# fails the build.
DISTSTAGE = hikari-${VERSION}.stage

hikari-${VERSION}.tar.gz: version.h doc
	@rm -rf ${DISTSTAGE}
	@mkdir -p ${DISTSTAGE}/hikari-${VERSION}
	@tar -cf ${DISTSTAGE}/dist.tar ${DIST_FILES}
	@(cd ${DISTSTAGE}/hikari-${VERSION} && tar -xf ../dist.tar)
	@(cd ${DISTSTAGE} && tar -czf ../hikari-${VERSION}.tar.gz hikari-${VERSION})
	@rm -rf ${DISTSTAGE}

# [COMMENT] Action purpose: version.h is regenerated on every build by the
# FORCE rule above, so removing it here is always safe. It used to be guarded on
# a `_darcs` directory that no checkout of this repository has, which meant
# `distclean` silently did nothing at all.
distclean: clean-doc
	@echo "cleaning version.h"
	@rm -f version.h

# [COMMENT] Action purpose: `.ORDER` is what makes `dist` correct under `-j`.
# Prerequisites of a target are unordered, so `make -j dist` may run distclean
# concurrently with the archive -- and the two touch the same files in opposite
# directions: distclean removes version.h and (via clean-doc) hikari.1, while
# the tarball's own prerequisites regenerate exactly those two and then tar
# them. Racing them loses the archive to a missing member, which is the same
# class of failure the CoC.md/CHANGELOG.md entries used to cause, only
# intermittent. Sequential make happens to get this right today; .ORDER makes
# it true regardless of -j.
.ORDER: distclean hikari-${VERSION}.tar.gz

dist: distclean hikari-${VERSION}.tar.gz

install: hikari hikari-unlocker hikari-topbar share/man/man1/hikari.1
	mkdir -p ${DESTDIR}/${PREFIX}/bin
	mkdir -p ${DESTDIR}/${PREFIX}/share/man/man1
	mkdir -p ${DESTDIR}/${PREFIX}/share/backgrounds/hikari
	mkdir -p ${DESTDIR}/${PREFIX}/share/wayland-sessions
	mkdir -p ${DESTDIR}/${PREFIX}/share/xdg-desktop-portal
	mkdir -p ${DESTDIR}/${ETC_PREFIX}/etc/hikari
	mkdir -p ${DESTDIR}/${ETC_PREFIX}/etc/pam.d
	# [COMMENT] Action purpose: the trailing slash in the pattern is what keeps
	# this from corrupting the file's own documentation. A bare `s,PREFIX,...,`
	# also rewrites the `$${ETC_PREFIX}` named in the header comment, so the
	# installed configuration told the reader it lived in `$${ETC_/usr/local}`.
	# Every live use of the token is a path, so it is always followed by `/`,
	# while the prose mentions are followed by `}` or a space.
	# [COMMENT] Action purpose: Never overwrite a configuration the administrator
	# has already edited. This had been an unconditional truncating redirect, so
	# a second `make install` -- an upgrade -- silently destroyed every local
	# keybinding and setting in the project's main configuration file. The PAM
	# policy further down this same recipe already does exactly this .new dance;
	# the two config files were being treated in contradictory ways, and this is
	# the one that holds the user's work.
	#
	# Generated into a temporary file in the destination directory and moved into
	# place, rather than redirected straight at the destination. `>` truncates
	# before sed runs, so a sed that failed part way -- a full disk is the
	# ordinary way -- left a half-written configuration behind while chmod still
	# succeeded and the recipe still reported success. Adding the existence check
	# above made that worse rather than better: the next install would see the
	# truncated file, decide it was the administrator's work, and preserve it
	# instead of repairing it. set -e stops the recipe on the sed itself, the mv
	# is atomic because the temporary sits in the same directory as its target,
	# and the trap takes the temporary away on any exit that does not reach it.
	#
	# mktemp, and not a name built from $$$$, for the same reason install-user
	# below uses it. A predictable path in a directory someone less trusted can
	# write to -- which a caller-provided DESTDIR can easily be -- can be
	# pre-created as a symlink, and `>` follows it: the redirect truncates
	# whatever it points at and the chmod then sets that target's mode, both as
	# whatever user is running the install, which for a system install is root.
	# Measured: with the old name, a symlink planted at it left the victim file
	# truncated from 27 bytes to 17; mktemp creates the file itself, mode 600 and
	# never a symlink, so the same attempt leaves it untouched.
	@set -e; \
	cfg_dir="${DESTDIR}/${ETC_PREFIX}/etc/hikari"; \
	cfg_tmp=$$(mktemp "$$cfg_dir/hikari.lua.XXXXXX"); \
	trap 'rm -f "$$cfg_tmp"' EXIT HUP INT TERM; \
	sed "s,PREFIX/,${PREFIX}/," etc/hikari/hikari.lua > "$$cfg_tmp"; \
	chmod 644 "$$cfg_tmp"; \
	if [ -e "$$cfg_dir/hikari.lua" ]; then \
		mv "$$cfg_tmp" "$$cfg_dir/hikari.lua.new"; \
		echo "install: $$cfg_dir/hikari.lua exists and was NOT replaced."; \
		echo "install: wrote hikari.lua.new instead -- merge it by hand if you want it."; \
	else \
		mv "$$cfg_tmp" "$$cfg_dir/hikari.lua"; \
	fi
	install -m ${PERMS} hikari ${DESTDIR}/${PREFIX}/bin
	install -m 555 start-hikari.sh ${DESTDIR}/${PREFIX}/bin/start-hikari
	install -m 4555 hikari-unlocker ${DESTDIR}/${PREFIX}/bin
	# [COMMENT] Action purpose: Install the top bar helper unprivileged (555).
	# Unlike the unlocker it needs no elevated rights -- it only reads sysctls
	# and runs user-level query tools.
	install -m 555 hikari-topbar ${DESTDIR}/${PREFIX}/bin
	install -m 644 share/man/man1/hikari.1 ${DESTDIR}/${PREFIX}/share/man/man1
	# [COMMENT] Action purpose: Install the default wallpaper to the path the
	# sed-rewritten outputs.background configuration points at
	# (${PREFIX}/share/backgrounds/hikari/hikari_wallpaper.png).
	install -m 644 share/backgrounds/hikari/hikari_wallpaper.png ${DESTDIR}/${PREFIX}/share/backgrounds/hikari/hikari_wallpaper.png
	# [COMMENT] Action purpose: Rewrite the desktop entry Exec= value to use the
	# absolute installed path so display managers resolve the wrapper correctly.
	sed "s,Exec=start-hikari,Exec=${PREFIX}/bin/start-hikari," share/wayland-sessions/hikari.desktop > ${DESTDIR}/${PREFIX}/share/wayland-sessions/hikari.desktop
	# [COMMENT] Action purpose: The second session entry, listed as "Hikari
	# Sakura", which runs the compositor binary rather than the wrapper script.
	# Only the binary is rewritten to an absolute path -- dbus-run-session is
	# resolved from PATH, because it belongs to the dbus package and its prefix
	# is not ours to assume.
	#
	# The dbus-run-session prefix is not optional and must not be trimmed to
	# "just the binary": it is what creates the session message bus. Without one
	# xdg-desktop-portal cannot be activated (no screen sharing or screenshots),
	# no process can own org.kde.StatusNotifierWatcher (no system tray) and
	# nothing can serve org.freedesktop.Notifications (no notifications). The
	# wrapper script provides the same bus for the other entry.
	sed "s,Exec=dbus-run-session hikari,Exec=dbus-run-session ${PREFIX}/bin/hikari," share/wayland-sessions/hikari-sakura.desktop > ${DESTDIR}/${PREFIX}/share/wayland-sessions/hikari-sakura.desktop
	# [COMMENT] Action purpose: Set desktop entry file permissions to read-only
	# (644) matching freedesktop.org wayland-sessions convention.
	chmod 644 ${DESTDIR}/${PREFIX}/share/wayland-sessions/hikari.desktop
	chmod 644 ${DESTDIR}/${PREFIX}/share/wayland-sessions/hikari-sakura.desktop
	# [COMMENT] Action purpose: Install the portal backend map. This is what
	# lets XDG_CURRENT_DESKTOP be the single name "Sakura": backend selection
	# reads this file instead of matching that variable against the UseIn= list
	# in xdg-desktop-portal-wlr's portal file, which contains no Sakura name.
	# Omitting it while the identity stays "Sakura" costs screen sharing and
	# screenshots, with nothing logged to say why -- the two changes are a pair.
	install -m 644 share/xdg-desktop-portal/sakura-portals.conf ${DESTDIR}/${PREFIX}/share/xdg-desktop-portal/sakura-portals.conf
# [COMMENT] Action purpose: FreeBSD installs one policy chosen by OS and is
# unchanged by this block. Linux cannot: the include target differs by family,
# so the stack is detected on the target system and the matching template
# installed. See PAM_CONFDIR and PAM_STACK at the top of this file.
.if ${OS} == "Linux"
	@set -u; \
	pam_confdir="${PAM_CONFDIR}"; \
	pam_stack="${PAM_STACK}"; \
	pam_dest="${DESTDIR}/${ETC_PREFIX}/etc/pam.d/hikari-unlocker"; \
	pam_usable() { \
		[ -f "$$1" ] && [ -r "$$1" ] && \
		grep -qE '^[[:space:]]*(-?[Aa][Uu][Tt][Hh][[:space:]]|@include[[:space:]])' "$$1"; \
	}; \
	pam_debian_family() { \
		[ -x /usr/sbin/pam-auth-update ] || [ -d /usr/share/pam-configs ]; \
	}; \
	if [ -n "$$pam_stack" ]; then \
		echo "install: PAM_STACK=$$pam_stack given; not probing $$pam_confdir"; \
	elif [ ! -d "$$pam_confdir" ]; then \
		echo "install: $$pam_confdir is not a directory, so this system does not use" >&2; \
		echo "install: the Linux-PAM pam.d layout. Refusing to guess an unlock policy." >&2; \
		echo "install: Re-run as  make install PAM_STACK=common-auth  (Debian, Ubuntu," >&2; \
		echo "install: openSUSE) or  PAM_STACK=system-auth  (Fedora, RHEL, Arch)." >&2; \
		exit 1; \
	else \
		if pam_usable "$$pam_confdir/common-auth"; then pam_common=yes; else pam_common=no; fi; \
		if pam_usable "$$pam_confdir/system-auth"; then pam_system=yes; else pam_system=no; fi; \
		if [ "$$pam_common" = yes ] && pam_debian_family; then \
			pam_stack=common-auth; \
		elif [ "$$pam_system" = yes ]; then \
			pam_stack=system-auth; \
		elif [ "$$pam_common" = yes ]; then \
			pam_stack=common-auth; \
		else \
			echo "install: cannot identify this system's PAM authentication stack." >&2; \
			echo "install: Looked for a usable $$pam_confdir/common-auth (Debian, Ubuntu," >&2; \
			echo "install: openSUSE) and $$pam_confdir/system-auth (Fedora, RHEL, Arch)" >&2; \
			echo "install: and found neither. A file that is empty, comment-only or a" >&2; \
			echo "install: dangling symlink does not count as usable." >&2; \
			echo "install:" >&2; \
			echo "install: NOT installing a PAM policy, and NOT guessing one. A guess" >&2; \
			echo "install: such as 'auth required pam_unix.so' would replace a working" >&2; \
			echo "install: SSSD or LDAP stack with a narrower one. Leaving no policy is" >&2; \
			echo "install: unsafe too: Linux-PAM then falls back to $$pam_confdir/other," >&2; \
			echo "install: which denies every unlock on Fedora and Arch and silently" >&2; \
			echo "install: hides this bug on Ubuntu." >&2; \
			echo "install:" >&2; \
			echo "install: Name the stack yourself, e.g.:" >&2; \
			echo "install:     make install PAM_STACK=system-auth" >&2; \
			echo "install: or hand-write $$pam_confdir/hikari-unlocker." >&2; \
			exit 1; \
		fi; \
		echo "install: detected PAM stack '$$pam_stack' from $$pam_confdir"; \
	fi; \
	pam_src="etc/pam.d/hikari-unlocker.Linux.$$pam_stack"; \
	if [ ! -f "$$pam_src" ]; then \
		echo "install: no policy template $$pam_src for stack '$$pam_stack'." >&2; \
		echo "install: PAM_STACK must name one of:" >&2; \
		for pam_t in etc/pam.d/hikari-unlocker.Linux.*; do \
			echo "install:     $${pam_t#etc/pam.d/hikari-unlocker.Linux.}" >&2; \
		done; \
		echo "install: or hand-write $$pam_confdir/hikari-unlocker." >&2; \
		exit 1; \
	fi; \
	if [ -e "$$pam_dest" ]; then \
		install -m 644 "$$pam_src" "$$pam_dest.new"; \
		echo "install: $$pam_dest exists and was NOT replaced."; \
		echo "install: wrote $$pam_dest.new instead -- merge it by hand if you want it."; \
	else \
		install -m 644 "$$pam_src" "$$pam_dest"; \
		echo "install: wrote $$pam_dest (stack $$pam_stack)"; \
	fi
.else
	install -m 644 etc/pam.d/hikari-unlocker.${OS} ${DESTDIR}/${ETC_PREFIX}/etc/pam.d/hikari-unlocker
.endif

uninstall:
	-rm ${DESTDIR}/${PREFIX}/bin/hikari
	-rm ${DESTDIR}/${PREFIX}/bin/start-hikari
	-rm ${DESTDIR}/${PREFIX}/bin/hikari-unlocker
	-rm ${DESTDIR}/${PREFIX}/bin/hikari-topbar
	-rm ${DESTDIR}/${PREFIX}/share/man/man1/hikari.1
	-rm ${DESTDIR}/${PREFIX}/share/backgrounds/hikari/hikari_wallpaper.png
	-rm ${DESTDIR}/${PREFIX}/share/wayland-sessions/hikari.desktop
	-rm ${DESTDIR}/${PREFIX}/share/wayland-sessions/hikari-sakura.desktop
	-rm ${DESTDIR}/${PREFIX}/share/xdg-desktop-portal/sakura-portals.conf
	-rm ${DESTDIR}/${ETC_PREFIX}/etc/pam.d/hikari-unlocker
	-rm ${DESTDIR}/${ETC_PREFIX}/etc/hikari/hikari.lua
	-rmdir ${DESTDIR}/${ETC_PREFIX}/etc/hikari
	-rmdir ${DESTDIR}/${PREFIX}/share/backgrounds/hikari
	# [COMMENT] Action purpose: Only this desktop's own directory is removed,
	# and only when empty. ${PREFIX}/share/xdg-desktop-portal is shared with the
	# portal packages themselves, so rmdir failing here is the correct and
	# expected outcome on any system that has them installed.
	-rmdir ${DESTDIR}/${PREFIX}/share/xdg-desktop-portal

# [COMMENT] Action purpose: Seed a working per-user config for the invoking
# user (run as yourself, no sudo/DESTDIR -- this writes into $HOME). Copies
# the wallpaper next to the config and pre-substitutes its path so the
# background renders out of the box; unlike `install`, it never overwrites an
# existing ~/.config/hikari/hikari.lua.
install-user: share/backgrounds/hikari/hikari_wallpaper.png etc/hikari/hikari.lua
	@test -n "${HOME}" || { echo "error: HOME is not set" >&2; exit 1; }
	mkdir -p "${HOME}/.config/hikari"
	install -m 644 share/backgrounds/hikari/hikari_wallpaper.png "${HOME}/.config/hikari/hikari_wallpaper.png"
	@if [ -e "${HOME}/.config/hikari/hikari.lua" ]; then \
		echo "install-user: ${HOME}/.config/hikari/hikari.lua already exists -- leaving it untouched"; \
	else \
		tmp=$$(mktemp "${HOME}/.config/hikari/hikari.lua.XXXXXX") && \
		sed -e "s,PREFIX/share/backgrounds/hikari,${HOME}/.config/hikari," \
		    -e "s,PREFIX/,${PREFIX}/," etc/hikari/hikari.lua > "$$tmp" && \
		chmod 644 "$$tmp" && \
		mv -f "$$tmp" "${HOME}/.config/hikari/hikari.lua" && \
		echo "install-user: wrote ${HOME}/.config/hikari/hikari.lua"; \
	fi

uninstall-user:
	@test -n "${HOME}" || { echo "error: HOME is not set" >&2; exit 1; }
	-rm "${HOME}/.config/hikari/hikari_wallpaper.png"
	@echo "uninstall-user: ${HOME}/.config/hikari/hikari.lua left in place -- remove it manually if desired"

# [COMMENT] Action purpose: Read back the dependency files -MMD wrote on the
# previous build, which is what turns them into actual rules. Kept at the very
# end of the file so that no dependency line can ever be read before `all`, and
# therefore no generated file can accidentally become the default target.
#
# `.sinclude` rather than `.include` is the load-bearing choice: on a clean tree
# none of these files exist yet, and `.include` would abort the build rather
# than proceed to create them. A `.for` loop over the object list is used in
# place of a wildcard because bmake does not glob include paths -- naming each
# file explicitly is the portable form, and it also means a .d left behind by an
# object that is no longer built is simply never read.
DEPS = ${OBJS:.o=.d}

.for _dep in ${DEPS}
.sinclude "${_dep}"
.endfor
