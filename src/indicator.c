#include <hikari/indicator.h>
#include <hikari/indicator_frame.h>




#include <hikari/configuration.h>
#include <hikari/group.h>
#include <hikari/mark.h>

#include <hikari/sheet.h>
#include <hikari/view.h>

void
hikari_indicator_init(struct hikari_indicator *indicator, float color[static 4])
{
  int bar_height = hikari_configuration->font.height;

  int offset = 5;
  hikari_indicator_bar_init(&indicator->title, indicator, offset, color);
  offset += bar_height + 5;
  hikari_indicator_bar_init(&indicator->sheet, indicator, offset, color);
  offset += bar_height + 5;
  hikari_indicator_bar_init(&indicator->group, indicator, offset, color);
  offset += bar_height + 5;
  hikari_indicator_bar_init(&indicator->mark, indicator, offset, color);
}

void
hikari_indicator_fini(struct hikari_indicator *indicator)
{
  hikari_indicator_bar_fini(&indicator->title);
  hikari_indicator_bar_fini(&indicator->sheet);
  hikari_indicator_bar_fini(&indicator->group);
  hikari_indicator_bar_fini(&indicator->mark);
}

// [COMMENT] Function purpose: Finalize indicator and hide the indicator frame on the associated view.
void
hikari_indicator_fini_for_view(
    struct hikari_indicator *indicator, struct hikari_view *view)
{
  hikari_indicator_fini(indicator);
  if (view != NULL) {
    hikari_indicator_frame_hide(&view->indicator_frame);
  }
}

void
hikari_indicator_update(
    struct hikari_indicator *indicator, struct hikari_view *view)
{
  assert(view != NULL);

  struct hikari_output *output = view->output;

  hikari_indicator_update_title(indicator, output, view->title);
  hikari_indicator_update_sheet(indicator, output, view->sheet, view->flags);
  hikari_indicator_update_group(indicator, output, view->group->name);

  if (view->mark != NULL) {
    hikari_indicator_update_mark(indicator, output, view->mark->name);
  } else {
    hikari_indicator_update_mark(indicator, output, "");
  }

  /* [COMMENT] Action purpose: Re-assert the current Logo-key state instead of
  assuming it. This runs on every focus change and on every keystroke while
  assigning a mark/group/sheet, so a content update must not by itself put the
  overlay on screen -- doing exactly that is what made the indicators permanent.
  hikari_indicator_show() repositions before enabling, which also covers the
  fact that hikari_indicator_bar_update() recreates each scene buffer at
  position (0,0). See DECISIONS_LOG Phase 59. */
  if (hikari_server_is_indicating()) {
    hikari_indicator_show(indicator, view);
  } else {
    hikari_indicator_hide(indicator, view);
  }
}

void
hikari_indicator_set_color(
    struct hikari_indicator *indicator, float color[static 4])
{
  hikari_indicator_set_color_title(indicator, color);
  hikari_indicator_set_color_sheet(indicator, color);
  hikari_indicator_set_color_group(indicator, color);
  hikari_indicator_set_color_mark(indicator, color);
}

static char
sheet_name(struct hikari_sheet *sheet)
{
  return sheet->nr + 48;
}

void
hikari_indicator_update_sheet(struct hikari_indicator *indicator,
    struct hikari_output *output,
    struct hikari_sheet *sheet,
    uint16_t flags)
{
  bool invisible = flags & hikari_view_invisible_flag;
  bool floating = flags & hikari_view_floating_flag;
  bool publicview = flags & hikari_view_public_flag;
  char *output_name = sheet->workspace->output->wlr_output->name;
  int i = 0;

  char *text = hikari_malloc(strlen(output_name) + 13);

  if (publicview) {
    text[i++] = '!';
  }

  if (floating) {
    text[i++] = '~';
  }

  if (invisible) {
    text[i++] = '[';
    text[i++] = sheet_name(sheet);
    text[i++] = ']';
  } else {
    text[i++] = sheet_name(sheet);
  }

  if (sheet->workspace->sheet != sheet) {
    text[i++] = ' ';
    text[i++] = '@';
    text[i++] = ' ';
    text[i++] = sheet_name(sheet->workspace->sheet);
  }

  text[i++] = ' ';
  text[i++] = '-';
  text[i++] = ' ';

  strcpy(&text[i], output_name);

  hikari_indicator_bar_update(&indicator->sheet, output, text);

  // [COMMENT] Action purpose: Do not reposition from hikari_server.workspace
  // here. This function receives output/sheet as parameters specifically so
  // it can be called for a non-current workspace (e.g. during
  // hikari_server_migrate_focus_view, before hikari_server.workspace is
  // reassigned to the destination output), and hikari_indicator_position
  // already positions all four bars from the correct view afterward.

  hikari_free(text);
}

/* [COMMENT] Function purpose: Paint the indicator frames of the focused view's
GROUP siblings, and restore the two colourscheme keys that had no consumer.

`grouped` and `first` were parsed, validated, documented and defaulted, and
nothing in the tree ever read either of them -- so configuring them did nothing.
This is where they belonged. The corroborating evidence is already in the tree:
src/normal_mode.c brackets both indicator transitions with
hikari_group_damage(focus_view->group), which damages every visible view in the
focused group. That call only makes sense if showing the indicator changes how
those views are drawn, and until now it did not.

`first` marks the group's anchor -- the view hikari_group_first_view() returns,
which is where the group-cycling actions start from -- and `grouped` marks the
rest. The focused view itself is skipped: hikari_indicator_show() has already
given it the `selected` frame, and overpainting it here would lose the
distinction the three colours exist to draw. */
static void
show_group_frames(struct hikari_view *view)
{
  struct hikari_group *group = view->group;

  if (group == NULL) {
    return;
  }

  struct hikari_view *first = hikari_group_first_view(group);

  struct hikari_view *sibling;
  wl_list_for_each (sibling, &group->visible_views, visible_group_views) {
    if (sibling == view) {
      continue;
    }

    float *color = sibling == first ? hikari_configuration->indicator_first
                                    : hikari_configuration->indicator_grouped;

    hikari_indicator_frame_show(&sibling->indicator_frame, color);
  }
}

/* [COMMENT] Function purpose: The exact inverse. Walks visible_views rather
than views, matching what show_group_frames() painted -- a view that left
visibility in between had its frame hidden by that transition already. */
static void
hide_group_frames(struct hikari_view *view)
{
  struct hikari_group *group = view->group;

  if (group == NULL) {
    return;
  }

  struct hikari_view *sibling;
  wl_list_for_each (sibling, &group->visible_views, visible_group_views) {
    if (sibling == view) {
      continue;
    }

    hikari_indicator_frame_hide(&sibling->indicator_frame);
  }
}

void
hikari_indicator_position(
    struct hikari_indicator *indicator, struct hikari_view *view)
{
  assert(indicator != NULL);
  assert(view != NULL);

  struct wlr_box *geometry = hikari_view_border_geometry(view);
  struct hikari_output *output = view->output;

  hikari_indicator_bar_position(&indicator->title, output, geometry);
  hikari_indicator_bar_position(&indicator->sheet, output, geometry);
  hikari_indicator_bar_position(&indicator->group, output, geometry);
  hikari_indicator_bar_position(&indicator->mark, output, geometry);

  /* [COMMENT] Action purpose: Positioning is geometry only. This function
  previously ended with an unconditional hikari_indicator_frame_show(), which
  meant every caller that merely wanted to reposition -- move, resize, tile,
  commit, focus change -- also forced the frame visible, so it never went away.
  Visibility is now owned solely by hikari_indicator_show/hide below. See
  DECISIONS_LOG Phase 59. */
}

/* Function purpose: Make the whole indicator overlay visible for a view -- all
four text bars plus the frame around the view -- and position it. Called when
the Logo key goes down, and on a focus/content change that happens while it is
already held. */
void
hikari_indicator_show(
    struct hikari_indicator *indicator, struct hikari_view *view)
{
  assert(indicator != NULL);

  if (view == NULL) {
    return;
  }

  hikari_indicator_position(indicator, view);

  hikari_indicator_bar_show(&indicator->title);
  hikari_indicator_bar_show(&indicator->sheet);
  hikari_indicator_bar_show(&indicator->group);
  hikari_indicator_bar_show(&indicator->mark);

  hikari_indicator_frame_show(&view->indicator_frame, indicator->title.color);

  show_group_frames(view);
}

/* Function purpose: Inverse of hikari_indicator_show. The bars are global to
the server while the frame belongs to the view, so a NULL view still hides the
bars -- that case arises when the Logo key is released with no focused view. */
void
hikari_indicator_hide(
    struct hikari_indicator *indicator, struct hikari_view *view)
{
  assert(indicator != NULL);

  hikari_indicator_bar_hide(&indicator->title);
  hikari_indicator_bar_hide(&indicator->sheet);
  hikari_indicator_bar_hide(&indicator->group);
  hikari_indicator_bar_hide(&indicator->mark);

  if (view != NULL) {
    hikari_indicator_frame_hide(&view->indicator_frame);
    hide_group_frames(view);
  }
}
