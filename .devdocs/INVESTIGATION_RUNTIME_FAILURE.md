# Deep-Dive Investigation: Post-Login Crash / Black-Screen + Dead Input

*Date:* 2026-08-13 04:40 (source: environment clock; shell access declined by user)
*Phase:* 18 — Runtime Failure Root-Cause Investigation
*Investigator directive (user):* "extremely deep and analytical investigation and report on the wiring, the false vs real logic, stubs placeholders, simulations, simplifications, poor implementations or hallucinations."
*Method:* 100% static analysis. Every claim below cites file:line. No code was modified.

---

## 1. Symptom Model

| Symptom | Observation | Required conditions |
|---|---|---|
| **A — crash/fail** | Process exits or aborts after login | Backend autocreate failure, config load failure, assert abort, segfault, or link failure producing no/partial binary |
| **B — black + frozen** | Compositor "loads"; screen black; keypresses ignored; mouse does not move | Wayland event loop alive, but: no outputs committed, **no input devices ever added**, cursor never painted |

Symptom B's "mouse does not move" is the decisive clue: `wlr_cursor` repaints from libinput motion events. A frozen cursor + dead keys together mean **the backend's input side never started** — not a keybinding problem (bindings only gate *actions*, not cursor motion).

---

## 2. Critical Defects (P0) — each independently explains a symptom

### P0-1. Hallucinated xkbcommon symbol — tree cannot reproduce the binary

* [`src/keyboard_config.c:354`](../src/keyboard_config.c): `compile_keymap()` calls **`xkb_map_new_from_names(...)`**.
* The real libxkbcommon API is **`xkb_keymap_new_from_names()`**. The `xkb_map_*` aliases were deprecated pre-1.0 and **removed in libxkbcommon 1.0 (2020)**. FreeBSD 15 ships 1.7+.
* Consequence: **link failure** (`undefined symbol`) on any clean build. Every startup compiles a keymap (the `"*"` default is always created by `finalize_keyboard_configs`, [`src/configuration.c:869`](../src/configuration.c)), so there is no code path that avoids this symbol.
* Devdocs contradiction: `TC-BUILD-01` is recorded "Passed ✓ (2026-08-13 — user-confirmed clean `make`)". Both cannot be true for a *clean* tree. The only consistent explanation: **stale `keyboard_config.o` from before this typo survived an incremental `make`**, so the running binary predates this source. The deployed binary is therefore *not* what the tree describes — a direct violation of the project's own audit trail.
* Classification: **hallucination** (API name invented; one-token deviation from upstream hikari).

### P0-2. `wlr_backend_start()` return value discarded — the symptom-B machine

* [`src/server.c:1054`](../src/server.c): `wlr_backend_start(hikari_server.backend);` — **no check**.
* wlroots returns `bool`; `false` means the DRM/libinput/session backend failed (seatd down, VT not acquired, DRM node permissions, etc.).
* On failure this tree falls straight into `wl_display_run()` ([`src/server.c:1060`](../src/server.c)). The compositor stays alive forever with:
  * zero real outputs (nothing ever modeset or committed → **black screen**),
  * zero input devices (`new_input` never fires → **dead keys, frozen cursor**),
  * only the 800×600 headless "noop" workspace, which is never rendered anywhere.
* Upstream hikari checks the return, prints `error: could not start backend`, and exits. This tree's version **simulates success** and runs headless-blind.
* Classification: **simplification that deleted the failure branch**; the single strongest match for symptom B.

### P0-3. `wlr_headless_backend_create()` argument type error + hallucinated justification comment

* [`src/server.c:857`](../src/server.c): `server->noop_backend = wlr_headless_backend_create(server->display);`
* The adjacent comment ([`src/server.c:853-856`](../src/server.c)) claims *"In wlroots 0.18+, wlr_headless_backend_create takes a struct wl_display \*"*.
* That claim is **false**: wlroots ≥ 0.16 (through 0.20) declares `struct wlr_backend *wlr_headless_backend_create(struct wl_event_loop *event_loop);`. The project's own `BRIEFING.md` (wlroots-0.20 fix list) records the *correct* fix — "`wlr_headless_backend_create` — now takes `wl_event_loop *` via `wl_display_get_event_loop()`" — i.e. an earlier phase fixed this and a later edit **reverted it to the wrong type while documenting the revert as a fix**. Code and devdocs contradict; the API says the code comment is the hallucination.
* Impact analysis: C compiles it (incompatible-pointer warning; release builds lack `-Werror`). At runtime wlroots registers the noop output's frame timer via `wl_event_loop_add_timer(backend->event_loop, ...)` with a `wl_display *` in that slot — **undefined behavior**. On current wayland/FreeBSD layouts it degrades to `epoll_ctl(EBADF)` and a NULL timer (silent), but it is layout-dependent UB that can equally segfault inside `init_noop_output()`, i.e. symptom A, on every launch, before the real backend starts.
* Classification: **hallucinated API contract** (comment asserts a false fact; argument is the wrong struct).

### P0-4. Default configuration and wallpaper referenced by the build do not exist

* [`Makefile:258`](../Makefile): `sed "s,PREFIX,${PREFIX}," etc/hikari/hikari.conf > ...` — **`etc/hikari/hikari.conf` is absent from the tree** (`etc/` contains only `pam.d/`).
* [`Makefile:264`](../Makefile): installs `share/backgrounds/hikari/hikari_wallpaper.png` — **`share/backgrounds/` is empty**.
* Consequences:
  * `make install` aborts at line 258 (sed can't open input) → binary/PAM/desktop-file installs after that line never run → **partial installs**, version-skewed deployments.
  * If a redirect-created *empty* `hikari.conf` ever lands, UCL parses it as a valid empty document → load "succeeds" with **zero keybindings, zero mouse bindings, no autostart** → a symptom-B lookalike even with a perfect binary (cursor would still move, distinguishing it from P0-2).
  * With no user config and no default config, [`main.c:246`](../main.c) prints `could not load configuration` and exits — symptom A from a display manager that hides stderr.
  * `dist` target ([`Makefile:242`](../Makefile)) references the same missing file.
* Classification: **false wiring** (build references phantom assets).

---

## 3. Serious Defects (P1)

### P1-5. `load_xkb_file()` stores a keymap but tags it `HIKARI_XKB_TYPE_RULES`

* [`src/keyboard_config.c:112-113`](../src/keyboard_config.c): after compiling a keymap from a file, sets `xkb->type = HIKARI_XKB_TYPE_RULES; xkb->value.keymap = keymap;` — the tag must be `HIKARI_XKB_TYPE_KEYMAP` (see the union in [`include/hikari/keyboard_config.h:27-34`](../include/hikari/keyboard_config.h)).
* Trigger: user config with the string form `xkb = "/path/to/keymap"`.
* Failure chain: `finalize_keyboard_configs` → [`hikari_keyboard_config_compile_keymap()`](../src/keyboard_config.c:360) sees RULES → `compile_keymap()` reinterprets the keymap pointer as a rules struct → garbage `rules/model/...` pointers → xkbcommon returns NULL or crashes → config load fails → `server_init` exits; the failure path itself then calls `xkb_fini()` on the same garbage (`free()` of non-heap pointers → heap corruption/abort). Debug builds additionally hit `assert(false)` in [`load_keymap()`](../src/keyboard.c:178) on first keyboard add.
* Classification: **logic bug** (union type-tag lie). Note the intentional-looking `assert(false)` + fallthrough in [`load_keymap()`](../src/keyboard.c:178-183) is only safe *because* finalize compiles everything — the tag bug defeats that invariant.

### P1-6. Numeric mouse bindings parsed, validated, then thrown away

* [`src/binding_config.c:136-148`](../src/binding_config.c): the `L-272`-style branch `strtol`s the value, range-checks it, and **never assigns `binding_key->value.keycode`** (compare the keyboard branch, [`src/binding_config.c:74`](../src/binding_config.c), which stores `value - 8`). Numeric mouse bindings silently bind keycode 0. Named buttons (`+left` etc.) are unaffected.
* Classification: **logic bug / incomplete implementation**.

### P1-7. Layer-shell surfaces are never attached to the scene graph

* [`src/layer_shell.c`](../src/layer_shell.c): `hikari_layer_init()` configures and lists the surface but **never calls `wlr_scene_layer_surface_v1_create()`** (nor any other scene attach). All draw requests route through `damage()` → `hikari_output_add_damage()`, which post-scene-migration is a **schedule-only shim** ([`include/hikari/output.h:83-113`](../include/hikari/output.h) — region argument deliberately unused).
* Net effect: any layer-shell client (panel, swaybg, wofi, notifications) is protocol-configured and "mapped" but **can never become visible**. This is a half-finished migration: damage bookkeeping survived, scene attachment was never written. (Compiled only with `WITH_LAYERSHELL`.)
* Classification: **simulation/placeholder wiring** — the render path for an entire shell is vestigial.

---

## 4. Moderate / Latent Defects (P2)

| # | Location | Issue | Impact |
|---|---|---|---|
| P2-8 | [`src/workspace.c:43`](../src/workspace.c) | `hikari_workspace_init()` re-initializes the **global** `hikari_server.visible_groups` list on every output init | Multi-monitor hotplug orphans existing links → list corruption/leak; should not be here (server_init owns it) |
| P2-9 | [`src/server.c:226-229`](../src/server.c) | `new_output_handler` hard-`exit(EXIT_FAILURE)` on `wlr_output_init_render` failure, with no diagnostic | One troublesome output kills the whole session silently (symptom-A variant); upstream at least prints |
| P2-10 | [`src/server.c:911-917`](../src/server.c) | Config-load failure exits with no hikari-side message (only ucl's own errors print) | "crashes and fails" with empty logs under a display manager |
| P2-11 | [`src/xdg_view.c:134`](../src/xdg_view.c), [`src/xwayland_view.c:149`](../src/xwayland_view.c) | `first_map(..., bool *focus)` never writes `*focus`; focus is actually decided inside `hikari_view_map` from view properties | Dead parameter; misleading signature (works by accident) |
| P2-12 | [`Makefile:189-190`](../Makefile) | `version.h` rule appends (`>>`) without regeneration guard | Rebuilds without `clean` accumulate duplicate `#define HIKARI_VERSION` (warnings) |
| P2-13 | [`include/hikari/view.h:1`](../include/hikari/view.h) | Stale `##Script function and purpose:` prefix survived the Phase-13 migration | AGENTS.md documentation-standard violation; also 48/55 `src/` files still lack the mandated script header (already tracked) |
| P2-14 | [`src/output.c:176-207`](../src/output.c) | `hikari_output_enable()` re-enables without setting a mode, relying on persisted `current_mode` | Verify at runtime: if wlroots cleared the mode on disable, lock-mode Ctrl+C → outputs stay dark |
| P2-15 | [`src/server.c:1036-1039`](../src/server.c) | `sig_handler` → `hikari_server_terminate` → `wl_event_loop_add_timer` (malloc) inside a signal handler | Async-signal-unsafe (inherited from upstream design; noted for completeness) |

---

## 5. Verified-Real Wiring (contrast set — checked and *correct*)

These were read end-to-end and match upstream hikari architecture and wlroots 0.20 contracts:

* **Entry/config resolution** — [`main.c`](../main.c): CLI parsing, user→system config fallback, privilege-drop asserts.
* **Server init order** — [`server_init()`](../src/server.c:898): config → renderer → allocator → socket → compositor/subcompositor/data-device → output layout → scene attach → cursor → seat/selections → decorations → xdg/layer shells → mode vtables → noop workspace. List inits precede use; seat created before cursor activation.
* **First-real-output migration** — [`src/output.c:399-403`](../src/output.c) merges the noop workspace into the first real output and [`hikari_workspace_focus_view()`](../src/workspace.c:379) updates `hikari_server.workspace` (line 438). Real, not simulated.
* **Frame loop** — [`frame_handler()`](../src/output.c:247): `wlr_scene_output_commit` + `send_frame_done`; modeset with preferred mode at init; `request_state` passthrough present.
* **XDG 0.20 lifecycle** — [`src/xdg_view.c`](../src/xdg_view.c): commit listener registered at `new_toplevel`, `initial_commit` answered with `wlr_xdg_toplevel_set_size(0,0)`, popup `initial_commit` answered with `wlr_xdg_surface_schedule_configure`, fullscreen guarded by `initialized` — matches tinywl's 0.20 pattern exactly.
* **Normal-mode dispatch** — [`src/normal_mode.c`](../src/normal_mode.c): pending-action handling, binding lookup by modifier mask, focus-follows-cursor via `node_at`, seat enter/motion/notify symmetry.
* **View state machine** — [`src/view.c`](../src/view.c) (read in full): map/unmap/show/hide/raise/lower, group/sheet list choreography, dirty/serial pending-operation protocol, maximize/tile queue+commit pairs, evacuate/migrate. Internally consistent with its own invariants (asserts form a coherent contract).
* **Input devices** — [`src/keyboard.c`](../src/keyboard.c), [`src/pointer.c`](../src/pointer.c), [`src/switch.c`](../src/switch.c): listener registration, seat keyboard assignment, libinput config application all real.
* **Lock mode & unlocker** — [`src/lock_mode.c`](../src/lock_mode.c): non-blocking PAM IPC via `wl_event_loop_add_fd`, pipe hygiene, WNOHANG-then-blocking reap discipline, output disable/enable timer; [`hikari_unlocker.c`](../hikari_unlocker.c): framed NUL-terminated password read, overflow drain, `explicit_bzero`, PAM fatal-vs-retry discrimination. Solid.
* **Scene widgets** — [`src/border.c`](../src/border.c), [`src/indicator_frame.c`](../src/indicator_frame.c), [`src/indicator_bar.c`](../src/indicator_bar.c), [`src/lock_indicator.c`](../src/lock_indicator.c), background loader ([`src/output.c:60`](../src/output.c)): real `wlr_scene_rect`/`wlr_scene_buffer` nodes with correct enable/position lifecycle and `wlr_buffer` data-ptr copies.
* **Modal set** — move/resize/dnd/input-grab/sheet-assign (read in full) are coherent vtable implementations; empty handlers are deliberate no-ops, not stubs.
* **Action/config surface** — [`src/action.c`](../src/action.c) full verb table; UCL parsers for pointers/keyboards/switches/outputs/ui/layouts with defaults-and-merge discipline.

*Coverage note:* `geometry.c`, `tile.c`, `split.c`, `layout.c`, `maximized_state.c`, `completion.c`, `binding_group.c`, the remaining assign/select modes, the `*_config.c` value objects, and `xwayland_unmanaged_view.c` were verified through their call sites and representative reads; every sampled member matched upstream patterns. No additional defects were observed in those samples, but they were not all read line-by-line.

---

## 6. False vs Real — documentation ledger corrections

| Devdocs claim | Reality in tree | Verdict |
|---|---|---|
| BRIEFING: headless fix "takes `wl_event_loop *` via `wl_display_get_event_loop()`" | [`src/server.c:857`](../src/server.c) passes `server->display`; comment claims API takes `wl_display *` | **Code regressed; briefing orphaned** |
| TC-BUILD-01 "Passed — user-confirmed clean `make`" | [`xkb_map_new_from_names`](../src/keyboard_config.c:354) cannot link against xkbcommon ≥ 1.0 | **Claim untenable for a clean tree; revalidate after `make clean`** |
| "Comprehensive audit … ~93% correctly wired" | P0-1..P0-4 open | **Overstated** |
| `wlr_output_effective_resolution()` "closed — verified by successful build" | Function genuinely exists in wlroots 0.20 — **but** layer shell only compiles under `WITH_LAYERSHELL`, and the build claim is itself suspect (P0-1) | **Closure basis invalid** |
| BRIEFING "Overall progress: 99%" | Startup path contains 4 release-blocking defects | **Reset to blocked (see updated BRIEFING)** |

---

## 7. Root-Cause Attribution

* **Symptom A (crash/fail):** P0-1 (no reproducible binary → stale/partial artifact), P0-4 (missing config → exit; empty config → dead session), P1-5 (xkb-file configs), P0-3 (UB; build/layout-dependent), any `assert` in debug builds (e.g. [`keyboard.c:180`](../src/keyboard.c)).
* **Symptom B (black + dead input + frozen mouse):** **P0-2 is the prime suspect** — the only defect that simultaneously removes outputs *and* input devices *and* the cursor while keeping the process alive. P0-4-empty-config is the secondary suspect (dead keys, but cursor would still move).

## 8. Remediation Plan (proposed — awaiting approval per AGENTS.md)

1. `keyboard_config.c:354`: `xkb_map_new_from_names` → `xkb_keymap_new_from_names`.
2. `server.c:1054`: check `wlr_backend_start()`; on failure print diagnostic, destroy backend+display, `exit(EXIT_FAILURE)`.
3. `server.c:857`: pass `wl_display_get_event_loop(server->display)`; delete the false comment (verify against installed header at build time).
4. Restore `etc/hikari/hikari.conf` (upstream default) and `share/backgrounds/hikari/hikari_wallpaper.png`, or remove/guard their install+dist references.
5. `keyboard_config.c:112`: tag `HIKARI_XKB_TYPE_KEYMAP`.
6. `binding_config.c:148`: store the parsed numeric mouse keycode.
7. `layer_shell.c`: attach via `wlr_scene_layer_surface_v1_create()` (+destroy on fini, position on configure) or formally de-scope layer shell.
8. `workspace.c:43`: drop the global re-init.
9. `make clean && make` from scratch; delete stale objects; re-run TC-BUILD-01 honestly; then runtime test with seatd up and watch stderr.

---

## 9. Remediation Register (Phase 18 execution — 2026-08-13 05:41)

All remediation steps were user-approved and executed. **Builds validated from scratch in BOTH configurations:** default (`make clean && make`, zero errors) and full-feature (`WITH_XWAYLAND/LAYERSHELL/SCREENCOPY/GAMMACONTROL/VIRTUAL_INPUT=YES`, zero errors, links clean).

| Finding | Fix applied | File(s) |
|---|---|---|
| P0-1 | `xkb_map_new_from_names` → `xkb_keymap_new_from_names` | `src/keyboard_config.c` |
| P0-2 | `wlr_backend_start()` result checked; diagnostic + `exit(EXIT_FAILURE)` on failure | `src/server.c` |
| P0-3 | `wlr_headless_backend_create(server->event_loop)`; false comment replaced with the correct 0.20 contract | `src/server.c` |
| P0-4 | Created `etc/hikari/hikari.conf` (verified against the parsers and `action.c` verb table; `PREFIX` token preserved for the install-time sed); wallpaper install now guarded (`test -f` + skip-warning); `version.h` rule made regenerating (`>` instead of `>>`) | `etc/hikari/hikari.conf` (new), `Makefile` |
| P1-5 | File-loaded keymaps tagged `HIKARI_XKB_TYPE_KEYMAP` | `src/keyboard_config.c` |
| P1-6 | Numeric mouse keycode stored (raw evdev code, no xkb +8 offset) | `src/binding_config.c` |
| P1-7 | Layer surfaces attached via `wlr_scene_layer_surface_v1_create()`; z-order by layer (overlay/top raised, bottom/background lowered); node positioned in `calculate_geometry()` (layout-global coords); enabled at map / disabled at unmap; destroyed at fini | `src/layer_shell.c`, `include/hikari/layer_shell.h` |
| P2-8 | Global `visible_groups` re-init removed from `hikari_workspace_init()` | `src/workspace.c` |
| P2-9 | Output render-init failure now prints the output name before exit | `src/server.c` |
| P2-10 | Config-load failure now prints the config path before exit | `src/server.c` |
| P2-11 | Dead `focus` out-params removed from `first_map()`/`map_handler()` | `src/xdg_view.c`, `src/xwayland_view.c` |
| P2-12 | `version.h` append → regenerate | `Makefile` |
| P2-13 | Stale `##` prefixes converted to the AGENTS.md standard | `include/hikari/view.h` |
| P2-14 | Left as runtime-verification item (wlroots retains `current_mode` across disable; expected OK) | — |
| P2-15 | Left as-is (upstream-inherited design; noted) | — |

### New defects discovered DURING remediation (feature-build validation)

The full-feature build (never previously exercised in this tree — further evidence that earlier "clean build" claims covered only the default configuration) exposed three more stale/hallucinated wlroots API usages, all fixed:

* **P1-16 (build):** `src/layer_shell.c` `damage_popup()` used the flat `wlr_xdg_popup.geometry` field, removed in modern wlroots. First migrated to `wlr_xdg_popup_get_geometry()` — the linker proved that symbol was *also* removed in 0.20. Final fix: `popup->current.geometry`, whose header documentation ("position of the popup relative to the upper left corner of the window geometry of the parent surface") matches the removed field's semantics exactly.
* **P1-17 (build):** `src/xwayland_view.c` `constraints()` dereferenced `struct wlr_xwayland_surface_size_hints`, which wlroots 0.20 replaced with the raw XCB type `xcb_size_hints_t *`. Fixed (same field names; `<xcb/xcb_icccm.h>` include added).
* **P1-18 (lifecycle):** `src/xwayland_view.c` registered `map`/`unmap` on `wlr_xwayland_surface.events` — those signals were removed; the underlying `wlr_surface` owns them now. Because the `wlr_surface` is NULL until the `associate` event in the 0.20 lifecycle, registration is deferred via new `associate`/`dissociate` listeners (with pre-initialised links so removal is always safe). `include/hikari/xwayland_view.h` gained the two listener fields.

### Verification status after remediation

* TC-BUILD-01: **Passed** — default clean build, 0 errors (2026-08-13 05:41).
* TC-BUILD-02 (new): **Passed** — full-feature clean build + link, 0 errors (2026-08-13 05:38).
* Remaining benign warnings: enum-compare at `src/dnd_mode.c:63` and `src/move_mode.c:78` (`wlr_button_state` vs `wl_pointer_button_state` — value-identical constants; cosmetic only).
* Runtime behaviour (seatd present, VT switch, clients, lock/unlock, layer clients) still requires a live TTY session test — the code paths are now wired correctly per the wlroots 0.20 contracts, but no static analysis can substitute for the live run.

*End of report.*
