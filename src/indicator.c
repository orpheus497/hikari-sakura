#include <hikari/indicator.h>
#include <hikari/indicator_frame.h>




#include <hikari/configuration.h>
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

  /* [COMMENT] Action purpose: hikari_indicator_bar_update() destroys and
  recreates each bar's scene buffer, which defaults to position (0,0) and
  enabled. Without repositioning here, every content update makes the
  indicator bars flash at the layout origin until something unrelated (e.g.
  move/resize) happens to reposition them. */
  hikari_indicator_position(indicator, view);
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

  // [COMMENT] Action purpose: Show the indicator frame overlay around the view when indicators are active.
  hikari_indicator_frame_show(&view->indicator_frame, indicator->title.color);
}
