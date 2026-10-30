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
# each with its own view of the dependency graph and its own job slots, so
# neither could order its work against the other's.
#
# One invocation is NOT a serialisation of the goals themselves. Measured:
# `bmake -j4 slow fast` on a two-target makefile runs fast entirely inside
# slow, and `make -j4 slow fast` does exactly the same -- so `make -j4 clean
# all` races here as it races under plain GNU make, and is a user error in
# both. Matching GNU make is the point of this file; serialising goals it does
# not serialise would be a divergence, not a fix.
#
# -j is deliberately not forwarded unbounded: parallelism is bmake's to
# schedule across the real dependency graph, which only it can see.
#
# $(MAKEOVERRIDES) carries command-line variable assignments through, so
# `make PREFIX=/usr WITH_XWAYLAND=NO install` reaches bmake intact.
# [COMMENT] Action purpose: The MAKEFLAGS actually handed to bmake, named here
# so the -B check below and the dispatch recipe cannot drift apart. Expanded in
# the recipe, never with `:=` -- see the note above hikari-dispatch.
HIKARI_MAKEFLAGS = $(filter-out --% -O% -j $(MAKEOVERRIDES),$(MAKEFLAGS))

# [COMMENT] Action purpose: Detect GNU make's -B, which must not reach bmake.
#
# The two makes disagree about this letter. GNU make's -B is --always-make,
# "unconditionally make all targets"; bmake's -B selects compatibility mode,
# which rebuilds nothing extra. MAKEFLAGS collects short flags into its first
# word and bmake honours MAKEFLAGS from the environment, so forwarding it
# would silently turn a forced rebuild into an ordinary incremental one and
# keep every object built with other flags.
#
# bmake has no --always-make to translate into, and deleting the user's
# objects is not what -B means either, so it is refused with a diagnostic
# naming `make clean`. Only the first word is examined: GNU make puts short
# flags there, and no other short flag it collects is an uppercase B. The test
# is the `case` in the dispatch recipe -- a make-level $(if) cannot carry the
# message, because $(if) splits its arguments on commas.

HIKARI_FIRST := $(firstword $(HIKARI_GOALS))
HIKARI_REST  := $(wordlist 2,$(words $(HIKARI_GOALS)),$(HIKARI_GOALS))

.PHONY: $(HIKARI_GOALS) hikari-dispatch

$(HIKARI_FIRST): hikari-dispatch
	@:

$(HIKARI_REST): $(HIKARI_FIRST)
	@:

# Action purpose: hand bmake the options it shares with GNU make, and only
# those. Without this filtering `make -j4` does not build the project at all.
#
# GNU make does not pass -j as an argument -- it EXPORTS it, with its jobserver
# file descriptors, in the MAKEFLAGS environment variable. bmake reads MAKEFLAGS
# as its own options, understands -j, does not understand --jobserver-auth, and
# takes the latter as -j's argument. The build dies before Makefile is read:
#
#     bmake[1]: illegal argument to -j -- must be positive integer!
#
# Clearing MAKEFLAGS outright fixes that and costs too much: it also discards
# -e, so `WITH_ALL=NO make -e` would silently build with WITH_ALL=YES, and it
# throws away -j entirely so every parallel build runs serially. Measured:
# bmake accepts `-j4`, `e`, `ks` and `n` from MAKEFLAGS unchanged, and goes
# genuinely parallel on the first, so there is no reason to drop them.
#
# What is removed, and why each:
#   --%  EVERY long option, not a list of the ones that have bitten so far.
#     bmake has no long options at all -- its usage line is a single
#     `[-BeikNnqrstWwX]` block plus short options taking arguments -- so any
#     word starting with `--` makes it print that usage and stop. GNU make puts
#     long options in MAKEFLAGS as a matter of course: its manual says the
#     value is "a possibly empty group of characters representing single-letter
#     options that take no argument, followed by a space and any options that
#     take arguments or which have long option names". Measured: `make
#     --no-print-directory` exports ` --no-print-directory`, and handing that to
#     bmake exits 2 with the usage line -- so that one ordinary flag broke the
#     build outright on Linux, which is the one thing this file exists to
#     prevent. `--warn-undefined-variables` does the same. This pattern
#     subsumes the jobserver flags (--jobserver-auth=, --jobserver-fds=) that
#     first motivated the filter, --output-sync in all its forms, and the bare
#     `--` separator.
#   -O%  the SHORT form of output synchronisation, which `--%` cannot catch.
#     It travels in MAKEFLAGS alongside -j and bmake has no equivalent.
#     Measured: `make -j4 -Otarget` exports ` -j4 -Otarget
#     --jobserver-auth=...`, and handing bmake the `-Otarget` that survived the
#     jobserver filter makes it print its usage and stop.
#   -j   the BARE form, which GNU make uses for "unlimited". bmake requires a
#     number ("option requires an argument -- j"), and picking one here would
#     be inventing a policy, so this degrades to serial. `-j4` and friends are
#     kept and work.
#   $(MAKEOVERRIDES)  the variable assignments, which are already passed to
#     bmake as real arguments on the line below. Leaving them in MAKEFLAGS too
#     would send each one twice.
#
# Expanded in the recipe, never with `:=`. At parse time MAKEFLAGS does not yet
# carry the job flags, so an immediate assignment would filter an empty string
# and silently reintroduce the serial build this is meant to prevent.
hikari-dispatch:
# The leading + marks this line recursive. GNU make skips recipe lines under
# -n, -t and -q unless it can tell they run a sub-make, and it detects that
# automatically only for $(MAKE); this line invokes $(BMAKE). Without the +,
# `make -n` prints the dispatch line and stops instead of describing the
# build. The filter above preserves n, so bmake receives it and does the dry
# run itself.
	+@case '$(firstword $(HIKARI_MAKEFLAGS))' in \
	*B*) \
		echo 'make: -B (--always-make) cannot be forwarded to bmake.' >&2; \
		echo 'make: bmake reads -B as compatibility mode and rebuilds' >&2; \
		echo 'make: nothing extra. Run "make clean" first instead.' >&2; \
		exit 1 ;; \
	esac; \
	MAKEFLAGS='$(HIKARI_MAKEFLAGS)' \
		$(BMAKE) $(MAKEOVERRIDES) $(HIKARI_GOALS)
