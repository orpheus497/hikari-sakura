# Script function and purpose: Route GNU make to bmake. This file contains no
# build logic whatsoever and must never acquire any -- Makefile is the single
# source of truth for how this project builds, on every platform.
#
# Why this file exists at all: Makefile is BSD make. It uses .if/.endif,
# .for/.endfor, .sinclude, .ORDER, .PATH and the ${VAR:tu} modifier, none of
# which GNU make understands -- it fails at the first .if with a syntax error
# that says nothing about the real problem. FreeBSD's make IS bmake, so the
# first-class platform needs nothing; it is only Linux and other GNU-make
# systems that arrive at the wrong parser.
#
# Why a separate file rather than a note in the README: GNU make searches for
# GNUmakefile, then makefile, then Makefile, and stops at the first it finds --
# so this file intercepts `make` before Makefile is ever parsed. bmake's
# .MAKE.MAKEFILE_PREFERENCE is "makefile Makefile", which does not contain
# GNUmakefile, so bmake cannot read this file even by accident. Both halves
# verified directly against GNU Make 4.3 and bmake 20200710. The result is that
# `make` does the right thing on Linux while the FreeBSD build path is
# untouched, byte for byte.

BMAKE := $(shell command -v bmake 2>/dev/null)

ifeq ($(BMAKE),)
$(error \
  This project builds with BSD make, which GNU make cannot parse. \
  Install bmake and build with it: \
  Debian/Ubuntu `apt install bmake`, Fedora `dnf install bmake`, \
  Arch `pacman -S bmake`. FreeBSD needs nothing -- its make is bmake)
endif

# Action purpose: Default to `all` when invoked bare, so `make` and `bmake`
# agree on the default target rather than this file inventing one.
HIKARI_GOALS := $(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)

# Action purpose: Hand every goal to a single bmake run, not one run per goal.
# Serialising the remaining goals behind the first is what guarantees that:
# without it, `make clean all` would start two independent bmake processes,
# which race over the same object files and, for `dist`, over the same tarball
# members -- the very hazard the .ORDER directive in Makefile exists to prevent.
# For the same reason -j is deliberately NOT forwarded: parallelism is bmake's
# to schedule across the real dependency graph, which only it can see.
#
# $(MAKEOVERRIDES) carries command-line variable assignments through, so
# `make PREFIX=/usr WITH_XWAYLAND=NO install` reaches bmake intact.
HIKARI_FIRST := $(firstword $(HIKARI_GOALS))
HIKARI_REST  := $(wordlist 2,$(words $(HIKARI_GOALS)),$(HIKARI_GOALS))

.PHONY: $(HIKARI_GOALS) hikari-dispatch

$(HIKARI_FIRST): hikari-dispatch
	@:

$(HIKARI_REST): $(HIKARI_FIRST)
	@:

# Action purpose: MAKEFLAGS is cleared for this one command, and without it
# `make -j4` does not build this project at all.
#
# The paragraph above says -j is not forwarded. That was wrong, and the bug it
# caused is worth recording rather than quietly deleting. GNU make does not pass
# -j as an argument -- it EXPORTS it, along with its jobserver file descriptors,
# in the MAKEFLAGS environment variable. Running `make -j4` here sets
# MAKEFLAGS to " -j4 --jobserver-auth=3,4". bmake reads MAKEFLAGS as its own
# options, understands -j, does not understand --jobserver-auth, and takes the
# latter as -j's argument. The build then dies before Makefile is even read:
#
#     bmake[1]: illegal argument to -j -- must be positive integer!
#
# Reproduced exactly that way, and `make -j4`, `make -j8 PREFIX=/opt` and a
# plain `make` all verified working once MAKEFLAGS is cleared.
#
# Clearing it is the whole fix, and it is per-command rather than a global
# `unexport`: the goals and the variable overrides are passed to bmake as real
# arguments on the line below, so nothing is lost by emptying the variable, and
# a global unexport would also hide MAKEFLAGS from anything else this makefile
# might later run. Parallelism stays bmake's to schedule across the real
# dependency graph, which only it can see.
hikari-dispatch:
	@MAKEFLAGS= $(BMAKE) $(MAKEOVERRIDES) $(HIKARI_GOALS)
