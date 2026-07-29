# Hikari Architecture Analysis: FreeBSD & wlroots Conformity

Based on a deep investigation of the codebase, project documentation, and current online resources regarding Wayland and wlroots best practices on FreeBSD, here are the findings.

## 1. FreeBSD System & Environment Conformity
**Status: Highly Compliant & Well-Architected**

The project has taken decisive and correct steps to be a native FreeBSD Wayland compositor:
- **Evdev Integration:** The eradication of `#ifdef __linux__` and the direct integration of `<dev/evdev/input-event-codes.h>` perfectly aligns with modern FreeBSD Wayland best practices. Relying on `kern.evdev.rcpt_mask=12` ensures that `libinput` functions correctly without dual-delivering legacy `sysmouse` events.
- **Session Management:** Recommending `seatd` (in `docs/freebsd_requirements.md`) is the current gold standard for FreeBSD Wayland session and privilege management.
- **PAM Authentication:** The standalone `hikari_unlocker.c` setuid daemon correctly links against `<security/pam_appl.h>` and is deployed with `4555` permissions, providing secure screen unlocking.
- **Epoll Shim:** The `Makefile` correctly integrates `epoll-shim` via `pkg-config`, solving the impedance mismatch between Linux's `epoll` (expected by wlroots/Wayland event loops) and FreeBSD's `kqueue`.

> [!TIP]
> The exclusive FreeBSD approach greatly simplifies the codebase. By not carrying Linux-specific baggage, Hikari avoids a major source of `#ifdef` spaghetti code common in cross-platform Wayland compositors.

## 2. wlroots 0.18 Modernization Mismatch
**Status: Critical Discrepancy Found in Code vs. Documentation**

According to `.devdocs/SESSION_HANDOFF.md` and the previous walkthrough logs, the codebase was purportedly migrated to `wlroots >= 0.18.0`. The documentation explicitly claims:
> "Removed `struct wlr_session *session;` from the core `hikari_server` struct. Rewrote the backend initialization call: `server->backend = wlr_backend_autocreate(server->display);`"

However, **source code inspection reveals this did not happen:**
1. In `include/hikari/server.h`, `struct wlr_session *session;` is still present.
2. In `src/server.c` (Line 769), the code still reads: 
   `server->backend = wlr_backend_autocreate(server->display, &server->session);`

**Impact:** In `wlroots 0.18`, the signature for `wlr_backend_autocreate` was changed to expect an event loop: `wlr_backend_autocreate(struct wl_event_loop *loop, struct wlr_session **session_ptr)`. The current code passes a `struct wl_display *`, which will result in a fatal compiler error when built against `wlroots 0.18.0`.

> [!WARNING]
> The previous automated session hallucinated its modernization implementation. The codebase is currently in an inconsistent state and will fail to compile against the `wlroots 0.18` headers specified in the `Makefile`.

## 3. Data-Oriented Design (DOD) Implementation
**Status: Partially Implemented**

The implementation of DOD Object Pools and vector bitmasks is partially visible in the codebase:
- `hikari_view_is_visible_dod` was successfully integrated into `src/workspace.c`, replacing linear `wl_list` iterations with $O(1)$ bitmask lookups.
- However, as noted in the most recent session handoff, the full Struct-of-Arrays (SoA) layout (Phase 8) was planned but is awaiting execution.

## Recommendations & Next Steps

To rectify the inconsistencies and continue the FreeBSD-native development:
1. **Fix `server.c` wlroots Initialization:** Update `wlr_backend_autocreate` to use the `wl_event_loop` retrieved via `wl_display_get_event_loop(server->display)` to comply with wlroots 0.18+.
2. **Execute a FreeBSD Build Test:** The project heavily needs a real `bmake` compilation run to flush out any remaining signature mismatches (such as the `wlr_backend_autocreate` hallucination).
3. **Audit Remaining `wlroots` API Changes:** Given the hallucinated backend update, other wlroots 0.18 deprecations (like output layout signals) should be re-verified against the actual source files, not just the `.devdocs`.
