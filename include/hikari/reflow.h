/* [COMMENT] Script function and purpose: Deferred re-tiling of a sheet whose
set of views has changed.

Why this is a scheduler and not a direct call. A reflow can only run when every
view it is about to lay out is CLEAN: hikari_view_is_tileable() returns false
for a dirty view, so a view with a resize in flight is silently skipped by
scan_next_tileable_view(), and hikari_sheet_apply_split() refuses outright while
any tile in the layout is dirty. A newly mapped window is dirty from its own
first configure -- so calling the reflow straight out of hikari_view_map() would
lay out every window EXCEPT the one that triggered it. That is a silent,
timing-dependent wrong answer, which is the worst shape a bug can take here.

So a reflow is REQUESTED, never performed, by the callers. The request is parked
on the sheet, an idle source drains what it can, and anything it had to defer is
retried from hikari_reflow_settle() -- called once from
hikari_view_commit_pending_operation(), the single point every geometry
operation in view.c converges on. */

#if !defined(HIKARI_REFLOW_H)
#define HIKARI_REFLOW_H

struct hikari_sheet;

/* [COMMENT] Function purpose: Ask for `sheet` to be re-tiled once it is quiet.
Cheap and idempotent -- a sheet already queued is not queued twice, and the call
returns immediately when the policy is off, so callers on hot paths (map, unmap,
sheet display) need no guard of their own. */
void
hikari_reflow_schedule(struct hikari_sheet *sheet);

/* [COMMENT] Function purpose: Note that a view has finished a geometry
operation, and re-arm the drain if anything is still waiting on it. Called from
hikari_view_commit_pending_operation(); returns immediately when nothing is
queued, which is the overwhelmingly common case. */
void
hikari_reflow_settle(void);

/* [COMMENT] Function purpose: Withdraw a pending request. Must be called before
a sheet's storage goes away -- the queue links into the sheet itself, so a freed
sheet left queued corrupts the list. */
void
hikari_reflow_cancel(struct hikari_sheet *sheet);

/* Function purpose: Drop the idle source at shutdown. Idempotent. */
void
hikari_reflow_fini(void);

#endif
