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
# stringified expansion in main.c appends "/etc/hikari/hikari.conf". Verified
# that an empty -D value stringifies to "" and yields exactly
# /etc/hikari/hikari.conf under -std=c11 -Wall -Wextra -pedantic.
#
# Still `?=`, so a distribution packager can override it as before.
.if ${OS} == "Linux"
ETC_PREFIX ?=
.else
ETC_PREFIX ?= ${PREFIX}
.endif

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
TOPBAR_CFLAGS := ${CFLAGS} -std=gnu11 -Wall

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

UCL_CFLAGS != ${PKG_CONFIG} --cflags libucl
UCL_LIBS != ${PKG_CONFIG} --libs libucl

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
	${UCL_CFLAGS} \
	${EPOLL_SHIM_CFLAGS}

LIBS = \
	${WLROOTS_LIBS} \
	${PANGO_LIBS} \
	${CAIRO_LIBS} \
	${PIXMAN_LIBS} \
	${XKBCOMMON_LIBS} \
	${WAYLAND_LIBS} \
	${LIBINPUT_LIBS} \
	${UCL_LIBS} \
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

hikari: version.h ${PROTOCOL_HEADERS} ${OBJS}
	${CC} ${LDFLAGS} ${CFLAGS} ${INCLUDES} -o ${.TARGET} ${OBJS} ${LIBS}

xdg-shell-protocol.h:
	wayland-scanner server-header ${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml ${.TARGET}

wlr-layer-shell-unstable-v1-protocol.h:
	wayland-scanner server-header protocol/wlr-layer-shell-unstable-v1.xml ${.TARGET}

hikari-unlocker: hikari_unlocker.c
	${CC} ${CFLAGS_EXTRA} ${LDFLAGS_EXTRA} -o hikari-unlocker hikari_unlocker.c -lpam

# [COMMENT] Action purpose: Build the top bar telemetry helper as its own
# binary. It is deliberately NOT linked into the compositor: it samples sensors
# with blocking popen() calls, which would stall the Wayland event loop if run
# in-process. hikari reads its swaybar-protocol output over a non-blocking pipe.
# It shares the project's DEBUG/ASAN/warning/hardening/GNU11 settings via the
# TOPBAR_CFLAGS snapshot above, but none of the compositor's feature macros or
# wlroots include paths. Note that topbar.c relies on __BSD_VISIBLE staying set
# for u_int, IFF_UP and usleep -- no feature-test macro may be defined for this
# target (see the comment atop src/topbar.c).
hikari-topbar: src/topbar.c
	${CC} ${LDFLAGS} ${TOPBAR_CFLAGS} -o hikari-topbar src/topbar.c

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
		etc/hikari/hikari.conf \
		etc/pam.d/hikari-unlocker.*

# [COMMENT] Action purpose: The `dist` tarball rewrites every member path to sit
# under a hikari-${VERSION}/ directory. `-s` is a bsdtar substitution flag and is
# what FreeBSD's base tar understands; GNU tar, which is what a Linux system has,
# spells the same operation --transform and takes a sed expression. Selecting
# here rather than requiring bsdtar on Linux keeps `make dist` working with the
# tar that is already installed.
#
# This is branched at the RECIPE rather than hoisted into a variable, and that
# is the whole point: make(1) COMMENTS says a comment begins "anywhere but in a
# shell command line", so the `#` delimiters are literal here but would start a
# comment in an assignment -- truncating the expression to `--transform "s` with
# no error. Escaping as `\#` does work, but make(1) documents no such escape, so
# the recipe form is preferred as the one the manual actually guarantees.
#
# The FreeBSD line is byte-identical to the one that preceded this change. Only
# the Linux line is new. Double quotes are not optional on either: VERSION
# carries its own literal quotes (`VERSION ?= "CURRENT"`), and only under double
# quotes does the shell concatenate the adjacent segments and strip the inner
# pair -- single quotes would name the archive root hikari-"CURRENT"/. Verified
# that both tars emit byte-identical member lists.
hikari-${VERSION}.tar.gz: version.h doc
.if ${OS} == "Linux"
	@tar --transform "s#^#hikari-${VERSION}/#" -czf hikari-${VERSION}.tar.gz ${DIST_FILES}
.else
	@tar -s "#^#hikari-${VERSION}/#" -czf hikari-${VERSION}.tar.gz ${DIST_FILES}
.endif

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
	sed "s,PREFIX,${PREFIX}," etc/hikari/hikari.conf > ${DESTDIR}/${ETC_PREFIX}/etc/hikari/hikari.conf
	chmod 644 ${DESTDIR}/${ETC_PREFIX}/etc/hikari/hikari.conf
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
	install -m 644 etc/pam.d/hikari-unlocker.${OS} ${DESTDIR}/${ETC_PREFIX}/etc/pam.d/hikari-unlocker

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
	-rm ${DESTDIR}/${ETC_PREFIX}/etc/hikari/hikari.conf
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
# existing ~/.config/hikari/hikari.conf.
install-user: share/backgrounds/hikari/hikari_wallpaper.png etc/hikari/hikari.conf
	@test -n "${HOME}" || { echo "error: HOME is not set" >&2; exit 1; }
	mkdir -p "${HOME}/.config/hikari"
	install -m 644 share/backgrounds/hikari/hikari_wallpaper.png "${HOME}/.config/hikari/hikari_wallpaper.png"
	@if [ -e "${HOME}/.config/hikari/hikari.conf" ]; then \
		echo "install-user: ${HOME}/.config/hikari/hikari.conf already exists -- leaving it untouched"; \
	else \
		tmp=$$(mktemp "${HOME}/.config/hikari/hikari.conf.XXXXXX") && \
		sed "s,PREFIX,${PREFIX}," etc/hikari/hikari.conf | \
		  sed "s,/share/backgrounds/hikari,${HOME}/.config/hikari," > "$$tmp" && \
		chmod 644 "$$tmp" && \
		mv -f "$$tmp" "${HOME}/.config/hikari/hikari.conf" && \
		echo "install-user: wrote ${HOME}/.config/hikari/hikari.conf"; \
	fi

uninstall-user:
	@test -n "${HOME}" || { echo "error: HOME is not set" >&2; exit 1; }
	-rm "${HOME}/.config/hikari/hikari_wallpaper.png"
	@echo "uninstall-user: ${HOME}/.config/hikari/hikari.conf left in place -- remove it manually if desired"

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
