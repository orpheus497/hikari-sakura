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
.endif

OS != uname
VERSION ?= "CURRENT"
PREFIX ?= /usr/local
PKG_CONFIG ?= pkg-config
ETC_PREFIX ?= ${PREFIX}

OBJS = \
	action.o \
	action_config.o \
	binding_config.o \
	binding_group.o \
	bar.o \
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
	layout_select_mode.o \
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
	platform.o \
	pointer.o \
	pointer_config.o \
	position_config.o \
	resize_mode.o \
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

clean-doc:
	@test -e _darcs && echo "cleaning manpage" ||:
	@test -e _darcs && rm share/man/man1/hikari.1 2> /dev/null ||:

clean: clean-doc
	@echo "cleaning headers"
	@test -e _darcs && rm version.h 2> /dev/null ||:
	@rm ${PROTOCOL_HEADERS} 2> /dev/null ||:
	@echo "cleaning object files"
	@rm ${OBJS} 2> /dev/null ||:
	@echo "cleaning executables"
	@rm hikari 2> /dev/null ||:
	@rm hikari-unlocker 2> /dev/null ||:
	@rm hikari-topbar 2> /dev/null ||:

share/man/man1/hikari.1:
	pandoc -M title:"HIKARI(1) ${VERSION} | hikari - Wayland Compositor" -s \
		--to man -o share/man/man1/hikari.1 share/man/man1/hikari.md

doc: share/man/man1/hikari.1

hikari-${VERSION}.tar.gz: version.h share/man/man1/hikari.1
	@darcs revert
	@tar -s "#^#hikari-${VERSION}/#" -czf hikari-${VERSION}.tar.gz \
		version.h \
		main.c \
		hikari_unlocker.c \
		include/hikari/*.h \
		src/*.c \
		protocol/*.xml \
		Makefile \
		LICENSE \
		README.md \
		CoC.md \
		start-hikari.sh \
		CHANGELOG.md \
		share/man/man1/hikari.md \
		share/man/man1/hikari.1 \
		share/backgrounds/hikari/hikari_wallpaper.png \
		share/wayland-sessions/hikari.desktop \
		etc/hikari/hikari.conf \
		etc/pam.d/hikari-unlocker.*

distclean: clean-doc
	@test -e _darcs && echo "cleaning version.h" ||:
	@test -e _darcs && rm version.h ||:

dist: distclean hikari-${VERSION}.tar.gz

install: hikari hikari-unlocker hikari-topbar share/man/man1/hikari.1
	mkdir -p ${DESTDIR}/${PREFIX}/bin
	mkdir -p ${DESTDIR}/${PREFIX}/share/man/man1
	mkdir -p ${DESTDIR}/${PREFIX}/share/backgrounds/hikari
	mkdir -p ${DESTDIR}/${PREFIX}/share/wayland-sessions
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
	# [COMMENT] Action purpose: Set desktop entry file permissions to read-only
	# (644) matching freedesktop.org wayland-sessions convention.
	chmod 644 ${DESTDIR}/${PREFIX}/share/wayland-sessions/hikari.desktop
	install -m 644 etc/pam.d/hikari-unlocker.${OS} ${DESTDIR}/${ETC_PREFIX}/etc/pam.d/hikari-unlocker

uninstall:
	-rm ${DESTDIR}/${PREFIX}/bin/hikari
	-rm ${DESTDIR}/${PREFIX}/bin/start-hikari
	-rm ${DESTDIR}/${PREFIX}/bin/hikari-unlocker
	-rm ${DESTDIR}/${PREFIX}/bin/hikari-topbar
	-rm ${DESTDIR}/${PREFIX}/share/man/man1/hikari.1
	-rm ${DESTDIR}/${PREFIX}/share/backgrounds/hikari/hikari_wallpaper.png
	-rm ${DESTDIR}/${PREFIX}/share/wayland-sessions/hikari.desktop
	-rm ${DESTDIR}/${ETC_PREFIX}/etc/pam.d/hikari-unlocker
	-rm ${DESTDIR}/${ETC_PREFIX}/etc/hikari/hikari.conf
	-rmdir ${DESTDIR}/${ETC_PREFIX}/etc/hikari
	-rmdir ${DESTDIR}/${PREFIX}/share/backgrounds/hikari

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
