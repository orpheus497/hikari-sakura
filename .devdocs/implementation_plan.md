# Implementation Plan: Audit Findings and Code Refactoring

This document outlines the planned fixes for the user-provided audit findings. We verified each finding against the current codebase to identify which issues are still valid and which have already been resolved.

## User Review Required

Please review the proposed changes. Several findings (e.g. `BLUEPRINT.md` scope, `damage_ring` migration, `server->toplevels` initialization) have already been fixed in the current tree and will be skipped. Only the verified valid issues are addressed below.

## Open Questions

- None at this stage. All fixes are straightforward corrections of validated bugs or documentation oversights.

## Proposed Changes

### Documentation and Tracking (`.devdocs/`)

#### [MODIFY] .devdocs/PROGRESS.md
- Update the "Overall Project Completion" math to accurately reflect a simple average of 87.5% (7 completed phases at 100%, 1 planned phase at 0%).

#### [MODIFY] .devdocs/SESSION_HANDOFF.md
- Update the "Next Steps" entry to remove any mention of a Linux build-target fallback.
- Explicitly record that complete build verification is blocked until the environment supports compiling FreeBSD targets, preserving FreeBSD as the exclusive target.
- Update references to ensure tracking artifacts (`task.md`, `walkthrough.md`, `implementation_plan.md`) are properly listed under `.devdocs/` or explicitly reclassified.

#### [MODIFY] .devdocs/TESTS.md
- **Test Protocol 3**: Update to specify identifying the canonical absolute path of `hikari-unlocker`, verifying root ownership, exact expected permissions (4555), and trusted package provenance *before* enabling setuid.

#### [MODIFY] .devdocs/TODOS.md
- Move the completed Phase C (DOD Struct-of-Arrays refactoring) items out of the active backlog and ensure they are synchronized with the implementation registry.

#### [NEW] .devdocs/implementation_plan.md
- Place a copy of this implementation plan inside `.devdocs/` as requested to explicitly reclassify it from root into the AI documentation tracking directory.

### Project Documentation (`docs/`) & Meta (`AGENTS.md`)

#### [MODIFY] AGENTS.md
- Update the ordered list near the workflow section to match the repository's configured numbering style.
- Ensure blank lines surround the shell code fence.
- Verify the document is markdownlint-clean.

#### [MODIFY] docs/freebsd_requirements.md
- Update the `XDG_RUNTIME_DIR` environment variable instructions to explicitly validate that the unique directory is owned by the current user and has 0700 permissions before exporting it.

#### [MODIFY] docs/modernization_guide.md
- Update the "wlroots API Modernization Plan" to explicitly state the supported target is wlroots 0.18.0 or newer, separating out any 0.17.x migration notes.

#### [MODIFY] docs/architecture_wiring.md
- Fix Markdown structural formatting by adding blank lines after affected headings and around the diagram fence.

#### [MODIFY] docs/data_oriented_design.md
- Add blank lines around headings and code fences to fix structural formatting.

### Core Source Code (`src/`, `include/`)

#### [MODIFY] Makefile
- Update `EPOLL_SHIM_CFLAGS` and `EPOLL_SHIM_LIBS` assignments to remove `|| true`, ensuring the build fails explicitly when `epoll-shim` is unavailable.

#### [MODIFY] main.c
- Update the `XDG_CONFIG_HOME`/`HOME` lookup to return an error when both environment variables are unset.
- Check the `malloc` result before writing to the buffer.
- Construct the path using `snprintf` to safely verify the destination size and report failure appropriately.

#### [MODIFY] hikari_unlocker.c
- Check `pam_start` failure and properly abort or apply bounded failure handling instead of infinitely retrying in the main loop.
- Check the result of `read(0, ...)` before calling `pam_authenticate`. Retry on `EINTR`, detect truncation if input exceeds buffer, and reject invalid states.
- Validate `getpwuid(getuid()) != NULL` before dereferencing `passwd->pw_name`.
- Validate `malloc` and `mlock` on `input_buffer` before proceeding, and ensure secure cleanup on failure.
- Replace `memset` with a guaranteed non-optimizable secure erasure (e.g. `explicit_bzero`) for the password buffer.
- Fix the memory leak in the `conversation_handler` failure path to free `pam_reply` and allocated strings before returning `PAM_ABORT`.

#### [MODIFY] src/output.c
- Update `frame_handler` to explicitly check for a `NULL` result from `wlr_scene_get_scene_output` and return immediately, avoiding a segfault.

#### [MODIFY] src/pool.c
- Handle NULL returns at `hikari_pool_alloc` call sites by rejecting or cleanly aborting the requested operation when exhausted instead of dereferencing NULL.

#### [MODIFY] src/renderer.c
- Update `renderer_end` to populate `frame_damage` using the wlroots 0.18 damage API instead of relying on deprecated `wlr_output_damage` logic.

#### [MODIFY] src/server.c
- In `node_at()`, fix undeclared `ox`/`oy` arguments by updating the hit-testing calls to use the correct local coordinates (`lx`, `ly`).
- In `server_decoration_handler`, check if `wlr_xdg_surface_try_from_wlr_surface` returns `NULL` before accessing `xdg_surface->data`.
- Move destruction of the object pools to after `wl_display_destroy_clients()` in the server shutdown sequence.

#### [MODIFY] src/switch.c
- In `hikari_switch_configure`, restore initialization for the `toggle.link` node.
- Register the missing switch destroy listener (`wl_signal_add(&wlr_switch->events.destroy, &swtch->destroy)`) in the initialization path and properly unlink it during teardown.

#### [MODIFY] src/view.c
- Add a helper function to centralize the sheet-mask assignment and replace duplicated inline conditionals in `hikari_view_evacuate`, `hikari_view_pin_to_sheet`, `migrate_view`, and `hikari_view_configure`.

#### [MODIFY] src/xdg_view.c
- In `surface_at`, subtract the view's geometry origin from the layout coordinates before calling `wlr_xdg_surface_surface_at`.
- Update the `map` and `unmap` lifecycle listeners in `hikari_xdg_view_init()` to attach to `xdg_surface->events.map` instead of `xdg_surface->surface->events.map`.

#### [MODIFY] src/xwayland_view.c
- Update constraints to use `struct wlr_xwayland_surface_size_hints` instead of `xcb_size_hints_t`.
- Fix the lifecycle mismatch in `destroy_handler` by ensuring map/unmap signal registrations are correctly balanced.

### Skipped (Already Fixed / Not Applicable)
- **.devdocs/BLUEPRINT.md**: Scope and `_Alignas` issues are already fixed.
- **.devdocs/DECISIONS_LOG.md**: Docs reference is already correct.
- **.devdocs/PLANS.md**: Links are already repository-relative.
- **docs/architecture_wiring.md**: `geteuid()` and file links already fixed.
- **docs/data_oriented_design.md**: Claims and alignment syntax already fixed.
- **src/layer_shell.c**: Functional damage handling is already present.
- **src/lock_mode.c** / **src/move_mode.c**: `##Function purpose:` prefixes already applied.
- **include/hikari/output.h**: `damage_ring` already used.
- **include/hikari/server.h**: `server->toplevels` already initialized.
- **fix_comments.py**: File is not present in the workspace.

## Verification Plan
### Automated Tests
- Validate markdown files with a linter where available.
- Ensure the build completes cleanly.

### Manual Verification
- Review modified C source to verify NULL pointer handling, proper variable scoping, and wlroots API adherence.
