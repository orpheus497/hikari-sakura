// [COMMENT] Script function and purpose: Core Hikari Wayland server
// initialization, signal management, and main loop handling.

#include <hikari/server.h>

#include <libinput.h>
#include <signal.h>
#include <stdint.h>
// [COMMENT] Action purpose: setgroups() lives in <unistd.h> on FreeBSD and in
// <grp.h> on glibc; include both so the privilege drop compiles on either.
#include <grp.h>
#include <unistd.h>
#include <wayland-server-core.h>

#include <drm_fourcc.h>
#include <wlr/backend.h>
// [COMMENT] Action purpose: wlr_buffer_init and struct wlr_buffer_impl are
// declared here; required for the CPU-backed ARGB8888 buffer below.
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_content_type_v1.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_shm.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/log.h>

#ifdef HAVE_LAYERSHELL
#include <wlr/types/wlr_layer_shell_v1.h>
#endif

#ifdef HAVE_GAMMACONTROL
#include <wlr/types/wlr_gamma_control_v1.h>
#endif

#ifdef HAVE_SCREENCOPY
#include <wlr/types/wlr_screencopy_v1.h>
#endif

#ifdef HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif

#include <hikari/border.h>
#include <hikari/command.h>
#include <hikari/configuration.h>
#include <hikari/decoration.h>
#include <hikari/exec.h>
#include <hikari/indicator_frame.h>
#include <hikari/keyboard.h>
#include <hikari/layout.h>
#include <hikari/mark.h>
#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/pointer.h>
#include <hikari/pointer_config.h>
#include <hikari/sheet.h>
#include <hikari/switch.h>
#include <hikari/touch.h>
#include <hikari/workspace.h>
#include <hikari/xdg_view.h>

#ifdef HAVE_XWAYLAND
#include <hikari/xwayland_unmanaged_view.h>
#include <hikari/xwayland_view.h>
#endif

static void
add_pointer(struct hikari_server *server, struct wlr_input_device *device)
{
  struct hikari_pointer *pointer = hikari_malloc(sizeof(struct hikari_pointer));
  hikari_pointer_init(pointer, device);

  struct hikari_pointer_config *pointer_config =
      hikari_configuration_resolve_pointer_config(
          hikari_configuration, device->name);

  if (pointer_config != NULL) {
    hikari_pointer_configure(pointer, pointer_config);
  }

  wlr_cursor_attach_input_device(server->cursor.wlr_cursor, device);
  wlr_cursor_map_input_to_output(server->cursor.wlr_cursor, device, NULL);
}

static void
add_keyboard(struct hikari_server *server, struct wlr_input_device *device)
{
  struct hikari_keyboard *keyboard =
      hikari_malloc(sizeof(struct hikari_keyboard));

  hikari_keyboard_init(keyboard, device);

  struct hikari_keyboard_config *keyboard_config =
      hikari_configuration_resolve_keyboard_config(
          hikari_configuration, device->name);

  assert(keyboard_config != NULL);
  hikari_keyboard_configure(keyboard, keyboard_config);

  hikari_keyboard_configure_bindings(
      keyboard, &hikari_configuration->keyboard_binding_configs);
}

static void
add_switch(struct hikari_server *server, struct wlr_input_device *device)
{
  struct hikari_switch *swtch = hikari_malloc(sizeof(struct hikari_switch));

  hikari_switch_init(swtch, device);

  struct hikari_switch_config *switch_config =
      hikari_configuration_resolve_switch_config(
          hikari_configuration, device->name);

  if (switch_config != NULL) {
    hikari_switch_configure(swtch, switch_config);
  }
}

// [COMMENT] Function purpose: Resolve the hikari_output whose wlr_output
// name matches a touch device's reported fused-panel output name, so
// wlr_cursor_absolute_to_layout_coords confines the device to that output
// instead of the whole layout on multi-output setups.
static struct hikari_output *
find_output_by_name(struct hikari_server *server, const char *name)
{
  struct hikari_output *output;

  wl_list_for_each (output, &server->outputs, server_outputs) {
    if (!strcmp(output->wlr_output->name, name)) {
      return output;
    }
  }

  return NULL;
}

static void
map_touch_to_output(struct hikari_server *server, struct wlr_input_device *device)
{
  struct wlr_touch *wlr_touch = wlr_touch_from_input_device(device);
  struct wlr_output *mapped_output = NULL;

  if (wlr_touch->output_name != NULL) {
    struct hikari_output *output =
        find_output_by_name(server, wlr_touch->output_name);

    if (output != NULL) {
      mapped_output = output->wlr_output;
    }
  }

  wlr_cursor_map_input_to_output(server->cursor.wlr_cursor, device, mapped_output);
}

static void
add_touch(struct hikari_server *server, struct wlr_input_device *device)
{
  struct hikari_touch *touch = hikari_malloc(sizeof(struct hikari_touch));

  hikari_touch_init(touch, device);

  wlr_cursor_attach_input_device(server->cursor.wlr_cursor, device);

  map_touch_to_output(server, device);
}

static void
add_input(struct hikari_server *server, struct wlr_input_device *device)
{

  switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
      add_keyboard(server, device);
      break;

    case WLR_INPUT_DEVICE_POINTER:
      add_pointer(server, device);
      break;

    case WLR_INPUT_DEVICE_SWITCH:
      add_switch(server, device);
      break;

    case WLR_INPUT_DEVICE_TOUCH:
      add_touch(server, device);
      break;

    default:
      break;
  }

  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (!wl_list_empty(&server->keyboards)) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  if (!wl_list_empty(&server->touches)) {
    caps |= WL_SEAT_CAPABILITY_TOUCH;
  }
  wlr_seat_set_capabilities(server->seat, caps);

  if ((caps & WL_SEAT_CAPABILITY_POINTER) != 0) {
    hikari_cursor_reset_image(&server->cursor);
  }
}

static void
new_input_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server = wl_container_of(listener, server, new_input);
  struct wlr_input_device *device = data;

  add_input(server, device);
}

#ifdef HAVE_VIRTUAL_INPUT
static void
new_virtual_keyboard_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server =
      wl_container_of(listener, server, new_virtual_keyboard);
  struct wlr_virtual_keyboard_v1 *keyboard = data;
  struct wlr_input_device *device = &keyboard->keyboard.base;

  add_input(server, device);
}

static void
setup_virtual_keyboard(struct hikari_server *server)
{
  server->virtual_keyboard =
      wlr_virtual_keyboard_manager_v1_create(server->display);
  // [COMMENT] Action purpose: Guard against manager allocation failure. The
  // wl_signal_add below takes the address of a member, so a NULL manager
  // becomes an offset from NULL that is then written through.
  if (server->virtual_keyboard == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  wl_signal_add(&server->virtual_keyboard->events.new_virtual_keyboard,
      &server->new_virtual_keyboard);
  server->new_virtual_keyboard.notify = new_virtual_keyboard_handler;
}

static void
new_virtual_pointer_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server =
      wl_container_of(listener, server, new_virtual_pointer);
  struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
  struct wlr_virtual_pointer_v1 *pointer = event->new_pointer;
  struct wlr_input_device *device = &pointer->pointer.base;

  add_input(server, device);

  if (event->suggested_output) {
    /* [COMMENT] Action purpose: Confine only the newly created virtual
    pointer to its suggested output. wlr_cursor_map_to_output() maps the
    *whole* cursor, so a client binding zwlr_virtual_pointer_v1 with a
    suggested output would also trap the physical mouse, touchpad and
    touchscreen on that output, with no way back short of restarting the
    compositor. The per-device call mirrors add_pointer() and
    map_touch_to_output(); the device is already attached to the cursor by
    add_input() above, which the API requires, and this narrows the
    whole-layout (NULL) mapping add_pointer() just installed. */
    wlr_cursor_map_input_to_output(
        server->cursor.wlr_cursor, device, event->suggested_output);
  }
}

static void
setup_virtual_pointer(struct hikari_server *server)
{
  server->virtual_pointer =
      wlr_virtual_pointer_manager_v1_create(server->display);
  // [COMMENT] Action purpose: Guard against manager allocation failure, as in
  // setup_virtual_keyboard above.
  if (server->virtual_pointer == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  wl_signal_add(&server->virtual_pointer->events.new_virtual_pointer,
      &server->new_virtual_pointer);
  server->new_virtual_pointer.notify = new_virtual_pointer_handler;
}
#endif

/* [COMMENT] Function purpose: Listener for the backend's new_output signal --
allocate and initialise the hikari_output wrapper for a hotplugged
physical/virtual output, wire up rendering, and reset the cursor image.
Exits loudly when render init fails. */
static void
new_output_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server = wl_container_of(listener, server, new_output);

  assert(server == &hikari_server);

  struct wlr_output *wlr_output = data;
  struct hikari_output *output = hikari_malloc(sizeof(struct hikari_output));
  output->wlr_output = wlr_output;
  output->server = server;

  if (!wlr_output_init_render(
          wlr_output, server->allocator, server->renderer)) {
    /* [COMMENT] Action purpose: Report which output failed render init before
    bailing out -- a silent exit is indistinguishable from a crash in logs. */
    fprintf(stderr,
        "error: could not initialize rendering for output \"%s\"\n",
        wlr_output->name);
    exit(EXIT_FAILURE);
  }

  hikari_output_init(output, wlr_output);
  hikari_cursor_reset_image(&server->cursor);

  // Action purpose: A touch device named after an output that had not yet
  // connected when it was attached is left unmapped by add_touch() (its
  // find_output_by_name() lookup fails). Retry every tracked touch device's
  // mapping now that a new output is available, so it gets confined once its
  // named output actually appears.
  struct hikari_touch *touch;
  wl_list_for_each (touch, &server->touches, server_touches) {
    map_touch_to_output(server, touch->device);
  }
}

static bool
surface_at(struct hikari_node *node,
    double ox,
    double oy,
    struct wlr_surface **surface,
    double *sx,
    double *sy)
{
  double out_sx, out_sy;

  struct wlr_surface *out_surface =
      hikari_node_surface_at(node, ox, oy, &out_sx, &out_sy);

  if (out_surface != NULL) {
    *sx = out_sx;
    *sy = out_sy;
    *surface = out_surface;
    return true;
  }

  return false;
}

#ifdef HAVE_LAYERSHELL
static bool
layer_at(struct wl_list *layers,
    double ox,
    double oy,
    struct wlr_surface **surface,
    double *sx,
    double *sy,
    struct hikari_node **node)
{
  double out_sx, out_sy;

  struct hikari_layer *layer;
  wl_list_for_each (layer, layers, layer_surfaces) {
    struct hikari_node *out_node = (struct hikari_node *)layer;

    struct wlr_surface *out_surface =
        hikari_node_surface_at(out_node, ox, oy, &out_sx, &out_sy);

    if (out_surface != NULL) {
      *sx = out_sx;
      *sy = out_sy;
      *surface = out_surface;
      *node = out_node;
      return true;
    }
  }

  return false;
}

static bool
topmost_of(struct wl_list *layers,
    double ox,
    double oy,
    struct wlr_surface **surface,
    double *sx,
    double *sy,
    struct hikari_node **node)
{
  double out_sx, out_sy;

  struct hikari_layer *layer;
  wl_list_for_each (layer, layers, layer_surfaces) {
    struct hikari_node *out_node = (struct hikari_node *)layer;

    struct wlr_layer_surface_v1_state *state = &layer->surface->current;

    struct wlr_surface *out_surface =
        hikari_node_surface_at(out_node, ox, oy, &out_sx, &out_sy);

    if (state->keyboard_interactive) {
      if (out_surface != NULL) {
        *surface = out_surface;
      } else {
        *surface = layer->surface->surface;
      }

      *sx = out_sx;
      *sy = out_sy;
      *node = out_node;

      return true;
    } else if (out_surface != NULL) {
      *surface = out_surface;

      *sx = out_sx;
      *sy = out_sy;
      *node = out_node;

      return true;
    }
  }

  return false;
}
#endif

static struct hikari_node *
node_at(double lx,
    double ly,
    struct wlr_surface **surface,
    struct hikari_workspace **workspace,
    double *sx,
    double *sy)
{
  assert(hikari_server.workspace != NULL);

  /* [COMMENT] Action purpose: Set the "nothing hit" result before any early
  return can skip it. All five callers pass uninitialised locals, so the miss
  paths previously left them indeterminate. */
  *surface = NULL;
  *sx = 0;
  *sy = 0;

  struct wlr_output *wlr_output =
      wlr_output_layout_output_at(hikari_server.output_layout, lx, ly);

  if (wlr_output == NULL) {
    *workspace = hikari_server.workspace;
    return NULL;
  }

  struct hikari_output *output = wlr_output->data;
  struct hikari_workspace *output_workspace = output->workspace;

  *workspace = output_workspace;

  struct hikari_node *node;

#ifdef HAVE_LAYERSHELL
  if (topmost_of(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
          lx - output->geometry.x,
          ly - output->geometry.y,
          surface,
          sx,
          sy,
          &node)) {
    return node;
  }
#endif

#ifdef HAVE_XWAYLAND
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view = NULL;
  wl_list_for_each (xwayland_unmanaged_view,
      &output->unmanaged_xwayland_views,
      unmanaged_output_views) {
    node = (struct hikari_node *)xwayland_unmanaged_view;

    if (surface_at(node,
            lx - output->geometry.x,
            ly - output->geometry.y,
            surface,
            sx,
            sy)) {
      return node;
    }
  }
#endif

#ifdef HAVE_LAYERSHELL
  if (topmost_of(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
          lx - output->geometry.x,
          ly - output->geometry.y,
          surface,
          sx,
          sy,
          &node)) {
    return node;
  }
#endif

  struct hikari_view *view = NULL;
  wl_list_for_each (view, &output_workspace->views, workspace_views) {
    node = (struct hikari_node *)view;

    if (surface_at(node,
            lx - output->geometry.x,
            ly - output->geometry.y,
            surface,
            sx,
            sy)) {
      return node;
    }
  }

#ifdef HAVE_LAYERSHELL
  if (layer_at(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
          lx - output->geometry.x,
          ly - output->geometry.y,
          surface,
          sx,
          sy,
          &node)) {
    return node;
  }
#endif

  return NULL;
}

struct hikari_node *
hikari_server_node_at(double x,
    double y,
    struct wlr_surface **surface,
    struct hikari_workspace **workspace,
    double *sx,
    double *sy)
{
  return node_at(x, y, surface, workspace, sx, sy);
}

void
hikari_server_cursor_focus(void)
{
  // [COMMENT] Action purpose: Retrieve monotonic clock time and convert to
  // milliseconds for Wayland event timestamping.
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  uint32_t time_msec =
      (uint32_t)(now.tv_sec * 1000LL + now.tv_nsec / 1000000LL);
  hikari_server.mode->cursor_move(time_msec);
}

static void
request_set_primary_selection_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server =
      wl_container_of(listener, server, request_set_primary_selection);

  struct wlr_seat_request_set_primary_selection_event *event = data;

  wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

static void
request_set_selection_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server =
      wl_container_of(listener, server, request_set_selection);

  struct wlr_seat_request_set_selection_event *event = data;

  wlr_seat_set_selection(server->seat, event->source, event->serial);
}

#ifdef HAVE_XWAYLAND
/* Function purpose: Wrap an X11 surface in whichever hikari view type its
current override_redirect flag calls for, and hand it the active workspace.

Shared by new_xwayland_surface_handler and by both set_override_redirect
handlers, which re-adopt a surface whose flag changed after creation. Keeping
the managed/unmanaged decision in one place is what makes re-adoption possible
at all -- previously the choice was made once, inline, and never revisited. */
void
hikari_server_adopt_xwayland_surface(
    struct wlr_xwayland_surface *wlr_xwayland_surface)
{
  struct hikari_workspace *workspace = hikari_server.workspace;

  /* [COMMENT] Action purpose: Safe-bail rather than fault. hikari_output_fini()
  sets hikari_server.workspace to NULL while tearing down the noop output, and
  every view wrapper dereferences the workspace it is handed (->workspace->output)
  on its very first map. An X11 client connecting during that window would
  otherwise segfault the compositor; declining to wrap the surface merely leaves
  it unmanaged, which is recoverable. */
  if (workspace == NULL) {
    wlr_log(WLR_ERROR,
        "hikari_server_adopt_xwayland_surface: no active workspace, ignoring "
        "surface");
    return;
  }

  if (wlr_xwayland_surface->override_redirect) {
    struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view =
        hikari_malloc(sizeof(struct hikari_xwayland_unmanaged_view));

    hikari_xwayland_unmanaged_view_init(
        xwayland_unmanaged_view, wlr_xwayland_surface, workspace);
  } else {
    struct hikari_xwayland_view *xwayland_view =
        hikari_malloc(sizeof(struct hikari_xwayland_view));

    hikari_xwayland_view_init(xwayland_view, wlr_xwayland_surface, workspace);
  }
}

static void
new_xwayland_surface_handler(struct wl_listener *listener, void *data)
{
  struct wlr_xwayland_surface *wlr_xwayland_surface = data;

  hikari_server_adopt_xwayland_surface(wlr_xwayland_surface);
}

// [COMMENT] Function purpose: Handle XWayland ready event, setting the DISPLAY environment variable.
static void
xwayland_ready_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server =
      wl_container_of(listener, server, xwayland_ready);

  /* [COMMENT] Action purpose: Re-export DISPLAY once Xwayland is actually up.
  setup_xwayland() already exported it, so this is a no-op in the normal case
  and only carries a value when wlroots has restarted Xwayland under a
  different display number. Failure is logged rather than fatal, deliberately
  unlike the setup_xwayland() call site: this runs from a live event handler
  long after startup, DISPLAY already holds a valid value from setup, and
  tearing down a running session over a failed re-set would destroy more than
  it protects. */
  if (setenv("DISPLAY", server->xwayland->display_name, true) != 0) {
    wlr_log(WLR_ERROR,
        "could not update DISPLAY on XWayland ready; keeping previous value");
  }
}

static void
setup_xwayland(struct hikari_server *server)
{
  server->xwayland =
      wlr_xwayland_create(server->display, server->compositor, true);
  if (server->xwayland == NULL) {
    // [COMMENT] Action purpose: Fail fast instead of calling
    // hikari_server_stop(). setup_xwayland runs before setup_scene_graph,
    // setup_decorations, setup_selection, setup_xdg_shell, setup_layer_shell,
    // wl_list_init(&server->toplevels), and hikari_topbar_source_init, so the
    // full shutdown path would wl_list_remove uninitialised listener links,
    // finalise an uninitialised topbar source, and touch a NULL seat.
    fprintf(stderr, "error: failed to create XWayland server\n");
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  /* [COMMENT] Action purpose: Export DISPLAY as soon as the X socket exists,
  rather than waiting for the ready event. wlroots populates display_name
  inside wlr_xwayland_create() (server_start_display() runs unconditionally and
  opens the socket), but this compositor requests lazy mode, so Xwayland is not
  executed until a client actually connects. Setting DISPLAY only from the
  ready handler therefore deadlocks: no client can connect without DISPLAY, so
  the lazy start never triggers, so ready never fires, so DISPLAY is never set.
  wlr_xwayland.display_name is documented as "value the DISPLAY environment
  variable should be set to by the compositor" and is valid from here on.
  Placed before run_autostart() so autostarted X clients inherit it.

  The return value is checked because a failed setenv() here is silent and
  reintroduces exactly the deadlock this call exists to prevent: DISPLAY stays
  as it was, so either no X client can connect at all, or -- when hikari is
  launched directly rather than through start-hikari.sh, which unsets it --
  DISPLAY still names a foreign X server and every X client connects there
  instead. Fail fast alongside the creation failure above rather than let
  run_autostart() spawn clients against a DISPLAY that is not ours. */
  if (setenv("DISPLAY", server->xwayland->display_name, true) != 0) {
    wlr_log(WLR_ERROR, "could not set DISPLAY for XWayland");
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  server->new_xwayland_surface.notify = new_xwayland_surface_handler;
  wl_signal_add(
      &server->xwayland->events.new_surface, &server->new_xwayland_surface);

  server->xwayland_ready.notify = xwayland_ready_handler;
  wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);
}
#endif

static void
setup_cursor(struct hikari_server *server)
{
  hikari_cursor_init(&hikari_server.cursor, server->output_layout);

  hikari_cursor_configure_bindings(
      &hikari_server.cursor, &hikari_configuration->mouse_binding_configs);
}

static void
server_decoration_mode_handler(struct wl_listener *listener, void *data)
{
  struct hikari_view_decoration *decoration =
      wl_container_of(listener, decoration, mode);
  struct hikari_view *view = decoration->view;

  view->use_csd = decoration->wlr_decoration->mode ==
                  WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;

  if (view->use_csd) {
    view->border.state = HIKARI_BORDER_NONE;
    hikari_output_damage_whole(hikari_server.workspace->output);
  }
}

// [COMMENT] Function purpose: Handle server decoration destroy event, freeing the decoration structure.
static void
server_decoration_destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_view_decoration *decoration =
      wl_container_of(listener, decoration, destroy);

  wl_list_remove(&decoration->mode.link);
  wl_list_remove(&decoration->destroy.link);
  decoration->wlr_decoration = NULL;
}

// [COMMENT] Function purpose: Resolve the hikari_xdg_view (if any) backing a
// wlr_surface, following the xdg_surface -> scene_tree -> node.data chain
// shared by request_activate_handler and server_decoration_handler.
static struct hikari_xdg_view *
hikari_xdg_view_try_from_wlr_surface(struct wlr_surface *surface)
{
  struct wlr_xdg_surface *xdg_surface =
      wlr_xdg_surface_try_from_wlr_surface(surface);

  if (xdg_surface == NULL) {
    return NULL;
  }

  struct wlr_scene_tree *scene_tree = xdg_surface->data;
  if (scene_tree == NULL) {
    return NULL;
  }

  return scene_tree->node.data;
}

static void
server_decoration_handler(struct wl_listener *listener, void *data)
{
  struct wlr_server_decoration *wlr_decoration = data;

  // [COMMENT] Action purpose: Guard against surfaces without a role — they
  // cannot be XDG toplevels.
  if (wlr_decoration->surface->role == NULL) {
    return;
  }

  // [COMMENT] Action purpose: Retrieve the hikari_xdg_view backing this
  // decoration's surface, if any (guards against non-XDG surfaces and a
  // decoration event arriving before the xdg_view is fully initialized).
  struct hikari_xdg_view *xdg_view =
      hikari_xdg_view_try_from_wlr_surface(wlr_decoration->surface);

  if (xdg_view == NULL) {
    return;
  }

  wl_signal_add(&wlr_decoration->events.mode, &xdg_view->view.decoration.mode);
  xdg_view->view.decoration.mode.notify = server_decoration_mode_handler;

  wl_signal_add(
      &wlr_decoration->events.destroy, &xdg_view->view.decoration.destroy);
  xdg_view->view.decoration.destroy.notify = server_decoration_destroy_handler;

  xdg_view->view.decoration.wlr_decoration = wlr_decoration;
  xdg_view->view.decoration.view = &xdg_view->view;
}

static void
new_toplevel_decoration_handler(struct wl_listener *listener, void *data)
{
  struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;

  struct hikari_decoration *decoration =
      hikari_malloc(sizeof(struct hikari_decoration));

  hikari_decoration_init(decoration, wlr_decoration);
}

static void
setup_decorations(struct hikari_server *server)
{
  server->decoration_manager =
      wlr_server_decoration_manager_create(server->display);
  // [COMMENT] Action purpose: Guard before the set_default_mode call below.
  // That call dereferences the manager inside wlroots, so an unchecked NULL
  // would fault in library code rather than here, making the backtrace point
  // away from the actual defect.
  if (server->decoration_manager == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  wlr_server_decoration_manager_set_default_mode(
      server->decoration_manager, WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

  wl_signal_add(&server->decoration_manager->events.new_decoration,
      &server->new_decoration);
  server->new_decoration.notify = server_decoration_handler;

  server->xdg_decoration_manager =
      wlr_xdg_decoration_manager_v1_create(server->display);
  // [COMMENT] Action purpose: Guard against manager allocation failure before
  // the wl_signal_add below takes the address of one of its members.
  if (server->xdg_decoration_manager == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  wl_signal_add(&server->xdg_decoration_manager->events.new_toplevel_decoration,
      &server->new_toplevel_decoration);
  server->new_toplevel_decoration.notify = new_toplevel_decoration_handler;
}

static void
start_drag_handler(struct wl_listener *listener, void *data)
{
  struct wlr_surface *surface;
  struct hikari_workspace *workspace;
  double sx, sy;

  struct hikari_node *node = node_at(hikari_server.cursor.wlr_cursor->x,
      hikari_server.cursor.wlr_cursor->y,
      &surface,
      &workspace,
      &sx,
      &sy);

  if (node != NULL) {
    hikari_dnd_mode_enter();
  }
}

static void
request_start_drag_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server =
      wl_container_of(listener, server, request_start_drag);
  struct wlr_seat_request_start_drag_event *event = data;

  if (wlr_seat_validate_pointer_grab_serial(
          server->seat, event->origin, event->serial)) {
    wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
  } else {
    wlr_data_source_destroy(event->drag->source);
  }
}

static void
setup_selection(struct hikari_server *server)
{
  wlr_data_control_manager_v1_create(server->display);

  wlr_primary_selection_v1_device_manager_create(server->display);

  server->seat = wlr_seat_create(server->display, "seat0");
  /* [COMMENT] Action purpose: Real, always-on guard replacing an assert().
  Release builds compile with -DNDEBUG (Makefile), so the previous
  assert(server->seat != NULL) was absent from every shipped binary -- this
  read as a guarded allocation while actually being an unguarded one. The seat
  is dereferenced immediately below and again from keyboard.c, normal_mode.c
  and lock_mode.c, so a NULL cannot be tolerated. Follows the Phase 61 policy
  decision to prefer always-on wlr_log(WLR_ERROR) plus a bail over
  debug-only assertions. */
  if (server->seat == NULL) {
    wlr_log(WLR_ERROR, "could not create seat");
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  server->request_set_primary_selection.notify =
      request_set_primary_selection_handler;
  wl_signal_add(&server->seat->events.request_set_primary_selection,
      &server->request_set_primary_selection);

  server->request_set_selection.notify = request_set_selection_handler;
  wl_signal_add(&server->seat->events.request_set_selection,
      &server->request_set_selection);

  server->request_start_drag.notify = request_start_drag_handler;
  wl_signal_add(
      &server->seat->events.request_start_drag, &server->request_start_drag);

  server->start_drag.notify = start_drag_handler;
  wl_signal_add(&server->seat->events.start_drag, &server->start_drag);
}

// [COMMENT] Function purpose: Handle new XDG toplevel creation.
static void
new_toplevel_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server =
      wl_container_of(listener, server, new_toplevel);

  struct wlr_xdg_toplevel *toplevel = data;
  struct wlr_xdg_surface *xdg_surface = toplevel->base;

  struct hikari_xdg_view *xdg_view =
      hikari_malloc(sizeof(struct hikari_xdg_view));

  hikari_xdg_view_init(xdg_view, xdg_surface, server->workspace);
}

static void
setup_scene_graph(struct hikari_server *server)
{
  server->scene = wlr_scene_create();
  // [COMMENT] Action purpose: Guard against scene root allocation failure.
  // All rendering depends on the scene tree; a NULL scene cannot be used.
  if (server->scene == NULL) {
    fprintf(stderr, "error: could not create scene graph\n");
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  server->scene_layout =
      wlr_scene_attach_output_layout(server->scene, server->output_layout);
  // [COMMENT] Action purpose: Guard against output-layout attachment failure.
  // Without a valid scene_layout, scene outputs cannot be registered and
  // frame commits will crash on a null pointer.
  if (server->scene_layout == NULL) {
    fprintf(stderr, "error: could not attach output layout to scene graph\n");
    wlr_scene_node_destroy(&server->scene->tree.node);
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }
}

static void
setup_xdg_shell(struct hikari_server *server)
{
  server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
  // [COMMENT] Action purpose: Guard against xdg-shell allocation failure. This
  // is the protocol every ordinary Wayland window depends on, so continuing
  // without it would produce a compositor no client can map a toplevel on.
  if (server->xdg_shell == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  server->new_toplevel.notify = new_toplevel_handler;
  wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_toplevel);
}

// [COMMENT] Function purpose: Focus the view a client requested activation for.
static void
request_activate_handler(struct wl_listener *listener, void *data)
{
  struct wlr_xdg_activation_v1_request_activate_event *event = data;

  struct hikari_xdg_view *xdg_view =
      hikari_xdg_view_try_from_wlr_surface(event->surface);
  if (xdg_view == NULL) {
    return;
  }

  struct hikari_view *view = (struct hikari_view *)xdg_view;

  if (hikari_view_is_mapped(view) && !hikari_view_is_hidden(view)) {
    hikari_workspace_focus_view(view->sheet->workspace, view);
  }
}

// [COMMENT] Function purpose: Register xdg-activation-v1 so clients (browsers
// opening a new window/tab, launchers) can request focus for a surface via
// xdg_activation_token_v1 instead of the compositor guessing.
static void
setup_xdg_activation(struct hikari_server *server)
{
  server->xdg_activation = wlr_xdg_activation_v1_create(server->display);
  // [COMMENT] Action purpose: Guard against manager allocation failure before
  // the wl_signal_add below takes the address of one of its members.
  if (server->xdg_activation == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  server->request_activate.notify = request_activate_handler;
  wl_signal_add(
      &server->xdg_activation->events.request_activate,
      &server->request_activate);
}

// [COMMENT] Function purpose: Per-inhibitor bookkeeping for
// zwp_idle_inhibit_manager_v1. Media players and browsers playing video hold
// one of these for the lifetime of playback to keep the session from idling;
// wlr_idle_notifier_v1_set_inhibited must stay true for as long as any
// inhibitor is alive, hence the refcount on hikari_server.
struct hikari_idle_inhibitor {
  struct wl_listener destroy;
};

static void
idle_inhibitor_destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_idle_inhibitor *inhibitor =
      wl_container_of(listener, inhibitor, destroy);

  wl_list_remove(&inhibitor->destroy.link);

  if (--hikari_server.idle_inhibitor_count == 0) {
    wlr_idle_notifier_v1_set_inhibited(hikari_server.idle_notifier, false);
  }

  hikari_free(inhibitor);
}

static void
new_idle_inhibitor_handler(struct wl_listener *listener, void *data)
{
  struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;

  struct hikari_idle_inhibitor *inhibitor =
      hikari_malloc(sizeof(struct hikari_idle_inhibitor));

  inhibitor->destroy.notify = idle_inhibitor_destroy_handler;
  wl_signal_add(&wlr_inhibitor->events.destroy, &inhibitor->destroy);

  if (++hikari_server.idle_inhibitor_count == 1) {
    wlr_idle_notifier_v1_set_inhibited(hikari_server.idle_notifier, true);
  }
}

// [COMMENT] Function purpose: Register idle-inhibit-v1 (client-requested
// "don't blank while I'm playing video") and idle-notify-v1 (the manager
// that actually tracks/reports idle state), and wire the refcount that
// bridges them.
static void
setup_idle_inhibit(struct hikari_server *server)
{
  server->idle_inhibit_manager = wlr_idle_inhibit_v1_create(server->display);
  server->idle_notifier = wlr_idle_notifier_v1_create(server->display);
  server->idle_inhibitor_count = 0;
  /* [COMMENT] Action purpose: Guard both managers here. idle_inhibit_manager
  is dereferenced by the wl_signal_add below, but idle_notifier is the more
  dangerous of the two: nothing touches it during setup, and it is first
  dereferenced by wlr_idle_notifier_v1_set_inhibited() in the inhibitor
  refcount handlers -- which only run when a client (a video player, a browser
  playing media) takes an inhibitor. An unchecked NULL there would fault
  minutes into a session with no apparent link back to initialisation, which
  is precisely the delayed-symptom signature that made the Phase 53-57 crash
  investigations so expensive. */
  if (server->idle_inhibit_manager == NULL || server->idle_notifier == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  server->new_idle_inhibitor.notify = new_idle_inhibitor_handler;
  wl_signal_add(&server->idle_inhibit_manager->events.new_inhibitor,
      &server->new_idle_inhibitor);
}

#ifdef HAVE_LAYERSHELL
static void
new_layer_shell_surface_handler(struct wl_listener *listener, void *data)
{
  struct wlr_layer_surface_v1 *wlr_layer_surface =
      (struct wlr_layer_surface_v1 *)data;
  struct hikari_layer *layer = hikari_malloc(sizeof(struct hikari_layer));

  hikari_layer_init(layer, wlr_layer_surface);
}

static void
setup_layer_shell(struct hikari_server *server)
{
  server->layer_shell = wlr_layer_shell_v1_create(server->display, 4);
  /* [COMMENT] Action purpose: Guard against layer-shell manager allocation
  failure. wl_signal_add() below takes the address of a member of
  server->layer_shell, so a NULL manager becomes an offset from NULL that is
  then written through, segfaulting during startup instead of failing
  cleanly. Matches the pointer_gestures guard in hikari_server_start(). */
  if (server->layer_shell == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  wl_signal_add(&server->layer_shell->events.new_surface,
      &server->new_layer_shell_surface);
  server->new_layer_shell_surface.notify = new_layer_shell_surface_handler;
}
#endif

struct hikari_server hikari_server;

static void
output_layout_change_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server =
      wl_container_of(listener, server, output_layout_change);

  struct hikari_output *output;
  wl_list_for_each (output, &server->outputs, server_outputs) {
    struct wlr_output *wlr_output = output->wlr_output;
    struct wlr_box output_box;
    wlr_output_layout_get_box(
        hikari_server.output_layout, wlr_output, &output_box);

    output->geometry.x = output_box.x;
    output->geometry.y = output_box.y;
    output->geometry.width = output_box.width;
    output->geometry.height = output_box.height;

    struct hikari_output_config *output_config =
        hikari_configuration_resolve_output_config(
            hikari_configuration, wlr_output->name);

    if (output_config != NULL) {
      hikari_output_load_background(output,
          output_config->background.value,
          output_config->background_fit.value);
    }

    struct hikari_view *view;
    wl_list_for_each (view, &output->views, output_views) {
      if (view->scene_node != NULL) {
        // [COMMENT] Action purpose: Use the active geometry, not view->geometry
        // directly -- maximized and tiled views position from
        // maximized_state->geometry / tile->view_geometry, which can differ
        // from view->geometry.
        struct wlr_box *geometry = hikari_view_geometry(view);
        wlr_scene_node_set_position(view->scene_node,
            geometry->x + output->geometry.x,
            geometry->y + output->geometry.y);
      }
    }

#ifdef HAVE_XWAYLAND
    hikari_output_rearrange_xwayland_views(output);
#endif
  }
}

static bool
drop_privileges(struct hikari_server *server)
{
  if (getuid() != geteuid() || getgid() != getegid()) {
    gid_t gid = getgid();

    // [COMMENT] Action purpose: Groups must go first -- once the effective UID
    // is unprivileged, setgroups() and setgid() fail with EPERM.
    if (geteuid() == 0 && setgroups(1, &gid) != 0) {
      return false;
    }

    if (setgid(gid) != 0) {
      return false;
    }

    if (setuid(getuid()) != 0) {
      return false;
    }
  }

  // [COMMENT] Action purpose: Confirm the drop took effect by comparing the
  // effective credentials against the real ones. Testing for zero instead would
  // reject a user whose real GID is legitimately 0 -- wheel is gid 0 on FreeBSD,
  // and a primary group of wheel is not a retained privilege.
  if (geteuid() != getuid() || getegid() != getgid()) {
    fprintf(stderr,
        "failed to drop privileges "
        "(uid=%ju, euid=%ju, gid=%ju, egid=%ju)\n",
        (uintmax_t)getuid(),
        (uintmax_t)geteuid(),
        (uintmax_t)getgid(),
        (uintmax_t)getegid());
    return false;
  }

  // [COMMENT] Action purpose: Refuse to run the compositor as actual root.
  if (getuid() == 0) {
    fprintf(stderr, "running as root is prohibited\n");
    return false;
  }

  return true;
}

void
hikari_server_prepare_privileged(void)
{
  bool success = false;
  struct hikari_server *server = &hikari_server;

  server->display = wl_display_create();
  if (server->display == NULL) {
    fprintf(stderr, "error: could not create display\n");
    goto done;
  }

  server->event_loop = wl_display_get_event_loop(server->display);
  if (server->event_loop == NULL) {
    fprintf(stderr, "error: could not create event loop\n");
    goto done;
  }

  server->backend =
      wlr_backend_autocreate(server->event_loop, &server->session);
  if (server->backend == NULL) {
    fprintf(stderr, "error: could not create backend\n");
    fprintf(stderr,
        "----------------------------------------------------------------------"
        "----------\n");
    fprintf(stderr,
        "Hikari failed to acquire a Wayland backend. Common causes include:\n");
    fprintf(stderr,
        "  1. Running natively without 'seatd' or seat management "
        "running/accessible.\n");
    fprintf(stderr, "  2. XDG_RUNTIME_DIR is not set in your environment.\n");
    fprintf(stderr,
        "  3. You are attempting to nest but WAYLAND_DISPLAY is invalid or "
        "inaccessible.\n");
    fprintf(stderr,
        "  4. You lack permission to access DRM nodes (/dev/dri/card*).\n");
    fprintf(stderr,
        "Please verify your session environment or use a proper wrapper "
        "script.\n");
    fprintf(stderr,
        "----------------------------------------------------------------------"
        "----------\n");
    goto done;
  }

  success = true;

done:
  if (!drop_privileges(server) || !success) {
    if (server->backend != NULL) {
      // [COMMENT] Action purpose: Destroy backend, which also destroys the
      // session internally in wlroots 0.20. Do NOT call wlr_session_destroy
      // separately -- the session is owned by the backend.
      wlr_backend_destroy(server->backend);
    }
    if (server->display != NULL) {
      wl_display_destroy(server->display);
    }

    exit(EXIT_FAILURE);
  }
}

// [COMMENT] Function purpose: Initialize headless fallback output for window
// management when no physical monitor is attached.
static void
init_noop_output(struct hikari_server *server)
{
  // [COMMENT] Action purpose: Create the headless fallback backend. wlroots
  // 0.20 wlr_headless_backend_create() takes the compositor's struct
  // wl_event_loop * (NOT the wl_display *). Passing the display is an
  // incompatible-pointer-type error that compiles to a warning and corrupts the
  // backend's event loop usage at runtime (undefined behavior).
  // server->event_loop was captured in hikari_server_prepare_privileged() via
  // wl_display_get_event_loop().
  server->noop_backend = wlr_headless_backend_create(server->event_loop);

  // [COMMENT] Action purpose: Guard against headless backend allocation
  // failure. Without a noop backend, the compositor cannot manage views when no
  // physical monitor is attached.
  if (server->noop_backend == NULL) {
    fprintf(
        stderr, "error: could not create headless backend for noop output\n");
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  struct wlr_output *wlr_output =
      wlr_headless_add_output(server->noop_backend, 800, 600);

  // [COMMENT] Action purpose: Guard against headless output allocation failure.
  if (wlr_output == NULL) {
    fprintf(stderr, "error: could not create headless output\n");
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  // [COMMENT] Action purpose: Initialize render backend for the noop output,
  // matching what new_output_handler does for real outputs. Without this, any
  // rendering path that touches the noop output will fail.
  if (!wlr_output_init_render(
          wlr_output, server->allocator, server->renderer)) {
    fprintf(stderr, "error: could not initialize render for noop output\n");
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  struct hikari_output *noop_output =
      hikari_malloc(sizeof(struct hikari_output));

  server->noop_output = noop_output;
  hikari_output_init(noop_output, wlr_output);

  hikari_server.workspace = noop_output->workspace;
  hikari_cursor_activate(&hikari_server.cursor);
  hikari_server.mode = (struct hikari_mode *)&hikari_server.normal_mode;
}

/* [COMMENT] Function purpose: One-time initialisation of the server
singleton -- configuration, wayland globals, scene graph, shells, input, and
the noop fallback output. Called by hikari_server_start before the backend
is started. */
/* Function purpose: Listener for wlr_session.events.active -- fired by
wlroots on VT switch-in (data=true) and switch-out (data=false). Sets the
global session_active flag consumed by frame_handler and
request_state_handler, and reschedules frames on all outputs when the
compositor becomes the foreground VT again so the display repaints
immediately after return. */
static void
session_active_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server =
      wl_container_of(listener, server, session_active_listener);

  /* [COMMENT] Action purpose: Read the state from the session struct, NOT from
  the listener's data argument. wlroots emits this signal as
  wl_signal_emit_mutable(&session->events.active, NULL) -- both at
  backend/session/session.c:27 and :33 -- so data is ALWAYS NULL and the
  previous `*(bool *)data` was an unconditional NULL dereference. It faulted on
  every session activate/deactivate, i.e. on every VT switch and every seat
  disable, killing the compositor with SIGSEGV before this function could log a
  single line. Confirmed from a core dump: the faulting instruction is the
  first memory access in this function. The authoritative state is the
  session's own `active` field (wlr/backend/session.h). */
  bool active = server->session != NULL ? server->session->active : true;

  /* Action purpose: Log session active state transitions to provide structured
  context if a compositor crash occurs after a VT switch. */
  wlr_log(WLR_INFO,
      "session_active_handler: VT session switched to %s",
      active ? "active" : "inactive");

  /* Action purpose: Update session-active flag unconditionally; frame_handler
  and request_state_handler read this flag to gate commit calls on an active
  CRTC. */
  server->session_active = active;

  if (active) {
    /* Action purpose: After VT switch-back all outputs need one frame-done
    cycle to re-synchronise the swapchain. Schedule frames on every enabled
    output so the compositor repaints without waiting for a client commit. */
    struct hikari_output *output;
    wl_list_for_each (output, &server->outputs, server_outputs) {
      if (output->enabled) {
        hikari_output_schedule_frame(output);
      }
    }
  }
}

static void
server_init(struct hikari_server *server, char *config_path)
{
#ifndef NDEBUG
  server->track_damage = false;
#endif
  server->shutdown_timer = NULL;
  server->config_path = config_path;

  hikari_configuration = hikari_malloc(sizeof(struct hikari_configuration));

  hikari_configuration_init(hikari_configuration);

  if (!hikari_configuration_load(hikari_configuration, config_path)) {
    /* [COMMENT] Action purpose: Emit a hikari-side diagnostic on configuration
    failure (covers non-parse failures such as keymap compilation errors that
    print nothing themselves) so startup never exits silently. */
    fprintf(
        stderr, "error: could not load configuration \"%s\"\n", config_path);
    hikari_configuration_fini(hikari_configuration);
    hikari_free(hikari_configuration);

    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  server->keyboard_state.modifiers = 0;
  server->keyboard_state.mod_released = false;
  server->keyboard_state.mod_changed = false;
  server->keyboard_state.mod_pressed = false;

  server->cycling = false;
  server->workspace = NULL;

  hikari_indicator_init(
      &server->indicator, hikari_configuration->indicator_selected);

  wl_list_init(&server->outputs);

  signal(SIGPIPE, SIG_IGN);

  server->renderer = wlr_renderer_autocreate(server->backend);
  if (server->renderer == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  wlr_shm_create_with_renderer(server->display, 1, server->renderer);

  server->allocator =
      wlr_allocator_autocreate(server->backend, server->renderer);
  if (server->allocator == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  server->socket = wl_display_add_socket_auto(server->display);
  if (server->socket == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }

  setenv("WAYLAND_DISPLAY", server->socket, true);

  server->compositor =
      wlr_compositor_create(server->display, 5, server->renderer);

  wlr_subcompositor_create(server->display);

  server->data_device_manager = wlr_data_device_manager_create(server->display);

  server->new_input.notify = new_input_handler;
  wl_signal_add(&server->backend->events.new_input, &server->new_input);

  server->output_layout = wlr_output_layout_create(server->display);
  // [COMMENT] Action purpose: Guard against output layout allocation failure.
  if (server->output_layout == NULL) {
    // [COMMENT] Action purpose: Abort server initialization on output layout
    // allocation failure.
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }
  server->output_manager =
      wlr_xdg_output_manager_v1_create(server->display, server->output_layout);

  server->output_layout_change.notify = output_layout_change_handler;
  wl_signal_add(
      &server->output_layout->events.change, &server->output_layout_change);

  server->new_output.notify = new_output_handler;
  wl_signal_add(&server->backend->events.new_output, &server->new_output);

#ifdef HAVE_GAMMACONTROL
  wlr_gamma_control_manager_v1_create(server->display);
#endif

#ifdef HAVE_SCREENCOPY
  wlr_screencopy_manager_v1_create(server->display);
#endif

#ifdef HAVE_XWAYLAND
  setup_xwayland(server);
#endif
  setup_scene_graph(server);

  /* [COMMENT] Action purpose: Create the linux-dmabuf-v1 protocol object and
  register it with the scene graph. wlr_scene_set_linux_dmabuf_v1() enables
  the scene to send per-surface DMA-BUF format/modifier feedback to clients.
  Without this, GPU-accelerated clients (GPU terminals, Electron, etc.) cannot
  negotiate optimal buffer formats and fall back to wl_shm or pick wrong
  modifiers, causing GL errors, black windows, or posix_fallocate crashes on
  ZFS. Must be called AFTER setup_scene_graph() since the scene must exist. */
  struct wlr_linux_dmabuf_v1 *linux_dmabuf =
      wlr_linux_dmabuf_v1_create_with_renderer(
          server->display, 4, server->renderer);
  if (linux_dmabuf != NULL) {
    wlr_scene_set_linux_dmabuf_v1(server->scene, linux_dmabuf);
  }

  /* [COMMENT] Action purpose: Protocols browsers and media players rely on for
  video subsurface scaling, frame timing, and HiDPI. wlr_scene picks up
  viewporter and presentation automatically once the globals exist. */
  wlr_viewporter_create(server->display);
  wlr_presentation_create(server->display, server->backend, 2);
  wlr_single_pixel_buffer_manager_v1_create(server->display);
  wlr_content_type_manager_v1_create(server->display, 1);
  wlr_tearing_control_manager_v1_create(server->display, 1);
  wlr_fractional_scale_manager_v1_create(server->display, 1);

  setup_cursor(server);
#ifdef HAVE_VIRTUAL_INPUT
  setup_virtual_keyboard(server);
  setup_virtual_pointer(server);
#endif
  setup_decorations(server);
  server->pointer_gestures =
      wlr_pointer_gestures_v1_create(server->display);
  // Action purpose: Guard against pointer-gestures manager allocation
  // failure. replay_swipe/replay_pinch/replay_hold (src/cursor.c) call
  // wlr_pointer_gestures_v1_send_* through server->pointer_gestures
  // unconditionally, so continuing with it unset would segfault the first
  // time an unmatched gesture needs replaying.
  if (server->pointer_gestures == NULL) {
    wl_display_destroy(server->display);
    exit(EXIT_FAILURE);
  }
  setup_selection(server);
  setup_xdg_shell(server);
  setup_xdg_activation(server);
  setup_idle_inhibit(server);
#ifdef HAVE_LAYERSHELL
  setup_layer_shell(server);
#endif

  wl_list_init(&server->pointers);
  wl_list_init(&server->keyboards);
  wl_list_init(&server->switches);
  wl_list_init(&server->touches);
  wl_list_init(&server->groups);
  wl_list_init(&server->visible_groups);
  wl_list_init(&server->visible_views);
  wl_list_init(&server->toplevels);

  hikari_dnd_mode_init(&server->dnd_mode);
  hikari_group_assign_mode_init(&server->group_assign_mode);
  hikari_input_grab_mode_init(&server->input_grab_mode);
  hikari_layout_select_mode_init(&server->layout_select_mode);
  hikari_lock_mode_init(&server->lock_mode);
  hikari_mark_assign_mode_init(&server->mark_assign_mode);
  hikari_mark_select_mode_init(&server->mark_select_mode);
  hikari_move_mode_init(&server->move_mode);
  hikari_normal_mode_init(&server->normal_mode);
  hikari_resize_mode_init(&server->resize_mode);
  hikari_sheet_assign_mode_init(&server->sheet_assign_mode);

  hikari_marks_init();

  init_noop_output(server);

  /* Action purpose: Subscribe to the session active signal so frame_handler
  and request_state_handler can gate commits on VT-active state. The session
  pointer is populated by wlr_backend_autocreate; it is NULL when running
  nested (Wayland-on-Wayland), in which case VT switching cannot occur and
  the guard is safely skipped. session_active is initialised true so frames
  proceed normally on startup. */
  /* [COMMENT] Action purpose: Start the native top bar's telemetry helper. It
  runs as a separate process feeding a non-blocking pipe: the sensors it reads
  (pactl, playerctl, nvidia-smi, sysctl) would otherwise have to be sampled with
  blocking popen() calls several times a second, stalling the event loop. */
  hikari_topbar_source_init(&server->topbar);

  server->session_active = true;
  if (server->session != NULL) {
    server->session_active_listener.notify = session_active_handler;
    wl_signal_add(
        &server->session->events.active, &server->session_active_listener);
  } else {
    /* Action purpose: Pre-initialise the listener link as an empty list when
    no session exists (nested compositor) so hikari_server_stop can safely
    call wl_list_remove on it unconditionally. */
    wl_list_init(&server->session_active_listener.link);
  }
}

// [COMMENT] Function purpose: wl_event_loop_add_signal callback for SIGTERM
// and SIGINT. Unlike a raw signal(3) handler, this runs as an ordinary event
// loop callback dispatched between poll cycles -- never reentering the main
// thread's own code at an arbitrary instruction, which matters because
// hikari_server_terminate() walks wl_lists and calls into per-view virtual
// dispatch, none of which is async-signal-safe. This is the same mechanism
// other wlroots compositors (Wayfire, labwc) use for graceful signal-driven
// shutdown. See DECISIONS_LOG Phase 42 Finding 2.
static int
terminate_signal_handler(int signal_number, void *data)
{
  hikari_server_terminate(NULL);

  return 0;
}

static void
run_autostart(char *autostart)
{
  hikari_command_execute(autostart);
  free(autostart);
}

/* [COMMENT] Function purpose: Compositor entry point invoked from main --
initialise the server, start the backend (failing loudly when the session
side is broken), run the autostart command, and enter the event loop. */
void
hikari_server_start(char *config_path, char *autostart)
{
  server_init(&hikari_server, config_path);

  // [COMMENT] Action purpose: Register SIGTERM and SIGINT through the
  // Wayland event loop instead of raw signal(3). SIGINT was previously
  // unhandled entirely -- Ctrl+C took the default disposition and skipped
  // every cleanup step (client quit requests, wl_display_destroy,
  // hikari_server_stop()'s teardown chain). Both now route through the same
  // existing, already-correct hikari_server_terminate() graceful-shutdown
  // sequence.
  hikari_server.sigterm_source = wl_event_loop_add_signal(
      hikari_server.event_loop, SIGTERM, terminate_signal_handler, NULL);
  hikari_server.sigint_source = wl_event_loop_add_signal(
      hikari_server.event_loop, SIGINT, terminate_signal_handler, NULL);

  // [COMMENT] Action purpose: Verify the backend actually started before
  // entering the event loop. wlr_backend_start() returns false when the
  // session/DRM/ libinput side fails (seatd down, VT or DRM-node permission
  // errors, etc.). Continuing anyway would run the compositor with zero outputs
  // and zero input devices -- a live process presenting a black screen with
  // dead keyboard and frozen mouse. Fail loudly instead so the user gets a
  // diagnostic on stderr.
  if (!wlr_backend_start(hikari_server.backend)) {
    fprintf(stderr, "error: could not start backend\n");
    fprintf(stderr,
        "verify that seatd (or another seat manager) is running and that "
        "this user can access DRM/input devices\n");
    wlr_backend_destroy(hikari_server.backend);
    wl_display_destroy(hikari_server.display);
    exit(EXIT_FAILURE);
  }

  if (autostart != NULL) {
    run_autostart(autostart);
  }

  wl_display_run(hikari_server.display);
}

static int
shutdown_handler(void *data)
{
  struct hikari_server *server = &hikari_server;

  if (server->shutdown_timer == NULL) {
    return 0;
  }

  struct hikari_output *output;
  wl_list_for_each (output, &server->outputs, server_outputs) {
    if (!wl_list_empty(&output->views)) {
      wl_event_source_timer_update(server->shutdown_timer, 1000);
      return 0;
    }
  }

  wl_display_terminate(hikari_server.display);

  return 0;
}

static void
destroy_shutdown_timer(struct hikari_server *server)
{
  wl_event_source_timer_update(server->shutdown_timer, 0);
  wl_event_source_remove(server->shutdown_timer);

  server->shutdown_timer = NULL;
}

void
hikari_server_terminate(void *arg)
{
  struct hikari_server *server = &hikari_server;

  if (server->shutdown_timer != NULL) {
    destroy_shutdown_timer(server);
    wl_display_terminate(server->display);
    return;
  }

  struct hikari_output *output;
  wl_list_for_each (output, &server->outputs, server_outputs) {
    struct hikari_view *view, *view_temp;
    wl_list_for_each_safe (view, view_temp, &output->views, output_views) {
      hikari_view_quit(view);
    }
  }

  server->shutdown_timer =
      wl_event_loop_add_timer(server->event_loop, shutdown_handler, NULL);

  wl_event_source_timer_update(server->shutdown_timer, 100);
}

void
hikari_server_stop(void)
{
  struct hikari_server *server = &hikari_server;

  if (server->sigterm_source != NULL) {
    wl_event_source_remove(server->sigterm_source);
    server->sigterm_source = NULL;
  }
  if (server->sigint_source != NULL) {
    wl_event_source_remove(server->sigint_source);
    server->sigint_source = NULL;
  }

  wl_list_remove(&server->new_output.link);
  wl_list_remove(&server->new_input.link);
  wl_list_remove(&server->new_toplevel.link);
  wl_list_remove(&server->request_set_primary_selection.link);
  wl_list_remove(&server->request_set_selection.link);
  wl_list_remove(&server->request_start_drag.link);
  wl_list_remove(&server->start_drag.link);
  wl_list_remove(&server->output_layout_change.link);
  wl_list_remove(&server->new_decoration.link);
  wl_list_remove(&server->new_toplevel_decoration.link);
  wl_list_remove(&server->request_activate.link);
  wl_list_remove(&server->new_idle_inhibitor.link);
  // [COMMENT] Action purpose: Remove the session-active listener before the
  // backend (and therefore the session) are destroyed. The link is always
  // initialised (either via wl_signal_add or wl_list_init when no session
  // exists), so removal is unconditionally safe.
  wl_list_remove(&server->session_active_listener.link);
  // [COMMENT] Action purpose: Stop the top bar helper and release its pipe and
  // event source before the event loop is destroyed.
  hikari_topbar_source_fini(&server->topbar);
#ifdef HAVE_LAYERSHELL
  wl_list_remove(&server->new_layer_shell_surface.link);
#endif
#ifdef HAVE_XWAYLAND
  wl_list_remove(&server->new_xwayland_surface.link);
  wl_list_remove(&server->xwayland_ready.link);
#endif
#ifdef HAVE_VIRTUAL_INPUT
  wl_list_remove(&server->new_virtual_keyboard.link);
  wl_list_remove(&server->new_virtual_pointer.link);
#endif

  if (server->shutdown_timer != NULL) {
    destroy_shutdown_timer(server);
  }

  /* Action purpose: Tear the clients down BEFORE the subsystems their teardown
  calls into. Destroying a client destroys its surfaces, which runs hikari's own
  view destroy handlers, and those reach hikari_server_cursor_focus() and the
  indicator. Finalising the cursor first left those handlers running against a
  destroyed wlr_xcursor_manager. XWayland is itself a client, so it follows the
  general client teardown. */
  wl_display_destroy_clients(server->display);

#ifdef HAVE_XWAYLAND
  wlr_xwayland_destroy(server->xwayland);
#endif

  hikari_cursor_fini(&server->cursor);
  hikari_indicator_fini(&server->indicator);

  hikari_lock_mode_fini(&server->lock_mode);
  hikari_mark_assign_mode_fini(&server->mark_assign_mode);

  wlr_seat_destroy(server->seat);
  if (server->noop_backend != NULL) {
    wlr_backend_destroy(server->noop_backend);
  }
  // [COMMENT] Action purpose: Destroy backend, which also destroys the session
  // internally in wlroots 0.20. Do NOT call wlr_session_destroy
  // separately -- the session is owned by the backend.
  if (server->backend != NULL) {
    wlr_backend_destroy(server->backend);
  }
  // [COMMENT] Action purpose: Destroy scene root before output layout so all
  // scene outputs are released while the layout is still valid.
  if (server->scene != NULL) {
    wlr_scene_node_destroy(&server->scene->tree.node);
  }
  wlr_output_layout_destroy(server->output_layout);
  wl_display_destroy(server->display);

  hikari_configuration_fini(hikari_configuration);
  hikari_free(hikari_configuration);
  hikari_marks_fini();

  free(server->config_path);
}

struct hikari_group *
hikari_server_find_group(const char *group_name)
{
  struct hikari_group *group;
  wl_list_for_each (group, &hikari_server.groups, server_groups) {
    if (!strcmp(group_name, group->name)) {
      return group;
    }
  }

  return NULL;
}

struct hikari_group *
hikari_server_find_or_create_group(const char *group_name)
{
  struct hikari_group *group = hikari_server_find_group(group_name);

  if (group == NULL) {
    group = hikari_malloc(sizeof(struct hikari_group));
    hikari_group_init(group, group_name);
  }

  return group;
}

void
hikari_server_lock(void *arg)
{
  hikari_lock_mode_enter();
}

void
hikari_server_reload(void *arg)
{
  hikari_configuration_reload(hikari_server.config_path);
}

#define CYCLE_VIEW(name, link)                                                 \
  static struct hikari_view *cycle_##name##_view(void)                         \
  {                                                                            \
    struct hikari_server *server = &hikari_server;                             \
                                                                               \
    if (wl_list_empty(&server->visible_views)) {                               \
      return NULL;                                                             \
    }                                                                          \
                                                                               \
    struct hikari_view *focus_view = server->workspace->focus_view;            \
                                                                               \
    struct hikari_view *view;                                                  \
    if (focus_view == NULL) {                                                  \
      view = wl_container_of(                                                  \
          server->visible_views.link, view, visible_server_views);             \
    } else {                                                                   \
      struct wl_list *link = focus_view->visible_server_views.link;            \
      if (link != &server->visible_views) {                                    \
        view = wl_container_of(link, view, visible_server_views);              \
      } else {                                                                 \
        view = wl_container_of(                                                \
            server->visible_views.link, view, visible_server_views);           \
      }                                                                        \
    }                                                                          \
                                                                               \
    return view;                                                               \
  }                                                                            \
                                                                               \
  void hikari_server_cycle_##name##_view(void *arg)                            \
  {                                                                            \
    struct hikari_view *view;                                                  \
                                                                               \
    hikari_server_set_cycling();                                               \
    view = cycle_##name##_view();                                              \
                                                                               \
    if (view != NULL && view != hikari_server.workspace->focus_view) {         \
      hikari_workspace_focus_view(view->sheet->workspace, view);               \
    }                                                                          \
  }

CYCLE_VIEW(next, prev)
CYCLE_VIEW(prev, next)
#undef CYCLE_VIEW

#define CYCLE_ACTION(n)                                                        \
  void hikari_server_cycle_##n(void *arg)                                      \
  {                                                                            \
    struct hikari_view *view;                                                  \
                                                                               \
    hikari_server_set_cycling();                                               \
    view = hikari_workspace_##n(hikari_server.workspace);                      \
                                                                               \
    if (view != NULL && view != hikari_server.workspace->focus_view) {         \
      hikari_workspace_focus_view(view->sheet->workspace, view);               \
    }                                                                          \
  }

CYCLE_ACTION(first_group_view)
CYCLE_ACTION(last_group_view)
CYCLE_ACTION(next_group_view)
CYCLE_ACTION(prev_group_view)
CYCLE_ACTION(next_layout_view)
CYCLE_ACTION(prev_layout_view)
CYCLE_ACTION(first_layout_view)
CYCLE_ACTION(last_layout_view)
CYCLE_ACTION(next_group)
CYCLE_ACTION(prev_group)
#undef CYCLE_ACTION

#define CYCLE_WORKSPACE(link)                                                  \
  void hikari_server_cycle_##link##_workspace(void *arg)                       \
  {                                                                            \
    struct hikari_workspace *workspace = hikari_server.workspace;              \
    struct hikari_workspace *link = hikari_workspace_##link(workspace);        \
                                                                               \
    if (workspace != link) {                                                   \
      hikari_server_set_cycling();                                             \
                                                                               \
      struct hikari_view *view = hikari_workspace_first_view(link);            \
      if (view != NULL) {                                                      \
        hikari_workspace_focus_view(link, view);                               \
        hikari_view_center_cursor(view);                                       \
      } else {                                                                 \
        hikari_workspace_center_cursor(link);                                  \
        hikari_server_cursor_focus();                                          \
      }                                                                        \
    }                                                                          \
  }

CYCLE_WORKSPACE(next)
CYCLE_WORKSPACE(prev)
#undef CYCLE_WORKSPACE

static void
update_indication(struct hikari_view *view)
{
  assert(view != NULL);
  assert(view->group != NULL);

  hikari_group_damage(view->group);
  hikari_indicator_damage(&hikari_server.indicator, view);
}

void
hikari_server_enter_normal_mode(void *arg)
{
  struct hikari_server *server = &hikari_server;

  hikari_cursor_reset_image(&server->cursor);

  hikari_normal_mode_enter();

  hikari_server_cursor_focus();
}

void
hikari_server_enter_sheet_assign_mode(void *arg)
{
  assert(hikari_server.workspace != NULL);

  struct hikari_workspace *workspace = hikari_server.workspace;
  struct hikari_view *focus_view = workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  update_indication(focus_view);

  hikari_sheet_assign_mode_enter(focus_view);
}

void
hikari_server_enter_move_mode(void *arg)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  update_indication(focus_view);

  hikari_move_mode_enter(focus_view);
}

void
hikari_server_enter_resize_mode(void *arg)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  update_indication(focus_view);

  hikari_resize_mode_enter(focus_view);
}

void
hikari_server_enter_group_assign_mode(void *arg)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  hikari_group_assign_mode_enter(focus_view);
}

void
hikari_server_enter_input_grab_mode(void *arg)
{
  struct hikari_workspace *workspace = hikari_server.workspace;
  struct hikari_view *focus_view = workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  update_indication(focus_view);

  hikari_input_grab_mode_enter(focus_view);
}

void
hikari_server_enter_mark_select_mode(void *arg)
{
  hikari_mark_select_mode_enter(false);
}

void
hikari_server_enter_mark_select_switch_mode(void *arg)
{
  hikari_mark_select_mode_enter(true);
}

void
hikari_server_enter_layout_select_mode(void *arg)
{
  struct hikari_workspace *workspace = hikari_server.workspace;
  struct hikari_view *focus_view = workspace->focus_view;

  if (focus_view != NULL) {
    update_indication(focus_view);
  }

  hikari_layout_select_mode_enter();
}

void
hikari_server_enter_mark_assign_mode(void *arg)
{
  assert(hikari_server.workspace != NULL);

  struct hikari_workspace *workspace = hikari_server.workspace;
  struct hikari_view *focus_view = workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  update_indication(focus_view);

  hikari_mark_assign_mode_enter(focus_view);
}

void
hikari_server_execute_command(void *arg)
{
  const char *command = arg;
  hikari_command_execute(command);
}

void
hikari_server_reset_sheet_layout(void *arg)
{
  struct hikari_layout *layout = hikari_server.workspace->sheet->layout;

  if (layout == NULL) {
    return;
  }

  hikari_layout_reset(layout);
}

void
hikari_server_layout_restack_append(void *arg)
{
  struct hikari_workspace *workspace = hikari_server.workspace;
  struct hikari_sheet *sheet = workspace->sheet;
  struct hikari_layout *layout = sheet->layout;

  if (layout == NULL) {
    struct hikari_split *split = hikari_sheet_default_split(sheet);

    if (split != NULL) {
      hikari_sheet_apply_split(sheet, split);
    }
  } else {
    hikari_workspace_display_sheet_current(workspace);
    hikari_layout_restack_append(layout);
  }
}

void
hikari_server_layout_restack_prepend(void *arg)
{
  struct hikari_workspace *workspace = hikari_server.workspace;
  struct hikari_sheet *sheet = workspace->sheet;
  struct hikari_layout *layout = sheet->layout;

  if (layout == NULL) {
    struct hikari_split *split = hikari_sheet_default_split(sheet);

    if (split != NULL) {
      hikari_sheet_apply_split(sheet, split);
    }
  } else {
    hikari_workspace_display_sheet_current(workspace);
    hikari_layout_restack_prepend(layout);
  }
}

void
hikari_server_layout_sheet(void *arg)
{
  char layout_register = (intptr_t)arg;

  struct hikari_split *split =
      hikari_configuration_lookup_layout(hikari_configuration, layout_register);

  if (split != NULL) {
    hikari_workspace_apply_split(hikari_server.workspace, split);
  }
}

void
hikari_server_session_change_vt(void *arg)
{
  const intptr_t vt = (intptr_t)arg;
  assert(vt >= 1 && vt <= 12);

  if (hikari_server.session != NULL) {
    wlr_session_change_vt(hikari_server.session, vt);
  }
}

static void
show_marked_view(struct hikari_view *view, struct hikari_mark *mark)
{
  if (view != NULL) {
    if (hikari_view_is_hidden(view)) {
      hikari_view_show(view);
    } else {
      hikari_view_raise(view);
    }

    hikari_view_center_cursor(view);
    hikari_server_cursor_focus();
  } else {
    char *command = hikari_configuration->execs[mark->nr].command;

    if (command != NULL) {
      hikari_command_execute(command);
    }
  }
}

void
hikari_server_show_mark(void *arg)
{
  assert(arg != NULL);

  struct hikari_mark *mark = (struct hikari_mark *)arg;
  struct hikari_view *view = mark->view;

  show_marked_view(view, mark);
}

void
hikari_server_switch_to_mark(void *arg)
{
  assert(arg != NULL);

  struct hikari_mark *mark = (struct hikari_mark *)arg;
  struct hikari_view *view = mark->view;

  if (view != NULL && view->sheet->workspace->sheet != view->sheet) {
    hikari_workspace_switch_sheet(view->sheet->workspace, view->sheet);
  }

  show_marked_view(view, mark);
}

void
hikari_server_migrate_focus_view(
    struct hikari_output *output, double lx, double ly, bool center)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  assert(focus_view != NULL);

  struct hikari_sheet *sheet = output->workspace->sheet;

  hikari_view_migrate(focus_view,
      sheet,
      lx - output->geometry.x,
      ly - output->geometry.y,
      center);

  hikari_indicator_update_sheet(
      &hikari_server.indicator, output, sheet, focus_view->flags);

  hikari_server.workspace->focus_view = NULL;
  hikari_server.workspace = output->workspace;
  hikari_server.workspace->focus_view = focus_view;
}

static void
move_view(int dx, int dy)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  struct hikari_output *view_output = focus_view->output;
  struct wlr_box *geometry = hikari_view_geometry(focus_view);

  double lx = view_output->geometry.x + geometry->x + dx;
  double ly = view_output->geometry.y + geometry->y + dy;

  struct wlr_output *wlr_output =
      wlr_output_layout_output_at(hikari_server.output_layout, lx, ly);

  hikari_server_set_cycling();

  if (wlr_output == NULL || wlr_output->data == view_output) {
    hikari_view_move(focus_view, dx, dy);
  } else {
    hikari_server_migrate_focus_view(wlr_output->data, lx, ly, false);
  }
}

void
hikari_server_move_view_up(void *arg)
{
  const int step = hikari_configuration->step;
  move_view(0, -step);
}

void
hikari_server_move_view_down(void *arg)
{
  const int step = hikari_configuration->step;
  move_view(0, step);
}

void
hikari_server_move_view_left(void *arg)
{
  const int step = hikari_configuration->step;
  move_view(-step, 0);
}

void
hikari_server_move_view_right(void *arg)
{
  const int step = hikari_configuration->step;
  move_view(step, 0);
}

static void
move_resize_view(int dx, int dy, int dwidth, int dheight)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  struct hikari_output *view_output = focus_view->output;
  struct wlr_box *geometry = hikari_view_geometry(focus_view);

  double lx = view_output->geometry.x + geometry->x + dx;
  double ly = view_output->geometry.y + geometry->y + dy;

  struct wlr_output *wlr_output =
      wlr_output_layout_output_at(hikari_server.output_layout, lx, ly);

  hikari_server_set_cycling();

  if (wlr_output == NULL || wlr_output->data == view_output) {
    hikari_view_move_resize(focus_view, dx, dy, dwidth, dheight);
  } else {
    hikari_server_migrate_focus_view(wlr_output->data, lx, ly, false);
    hikari_view_resize(focus_view, dwidth, dheight);
  }
}

/* [COMMENT] Class purpose: CPU-backed ARGB8888 buffer holding a copy of
caller-rendered cairo pixels. This exists because allocating through
wlr_allocator does not work on the target platform: GBM buffer mapping fails on
FreeBSD/ZFS, so wlr_allocator_create_buffer() (or the subsequent
begin_data_ptr_access for write) returns failure and every UI element built this
way silently disappears. output.c already worked around this for the background
with its own wlr_buffer_impl (DECISIONS_LOG Phase 33); this brings the shared
helper -- and therefore the indicator bars, lock indicator, and top bar -- onto
the same footing. */
struct hikari_argb8888_buffer {
  struct wlr_buffer base;
  unsigned char *data;
  uint32_t format;
  size_t stride;
};

static void
argb8888_buffer_destroy(struct wlr_buffer *wlr_buffer)
{
  struct hikari_argb8888_buffer *buffer =
      wl_container_of(wlr_buffer, buffer, base);
  hikari_free(buffer->data);
  hikari_free(buffer);
}

static bool
argb8888_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
    uint32_t flags,
    void **data,
    uint32_t *format,
    size_t *stride)
{
  struct hikari_argb8888_buffer *buffer =
      wl_container_of(wlr_buffer, buffer, base);

  // [COMMENT] Action purpose: The pixels are a snapshot owned by the buffer;
  // callers re-render and create a new buffer rather than mutating this one.
  if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) {
    return false;
  }

  *data = buffer->data;
  *format = buffer->format;
  *stride = buffer->stride;

  return true;
}

static void
argb8888_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer)
{}

static const struct wlr_buffer_impl argb8888_buffer_impl = {
  .destroy = argb8888_buffer_destroy,
  .begin_data_ptr_access = argb8888_buffer_begin_data_ptr_access,
  .end_data_ptr_access = argb8888_buffer_end_data_ptr_access,
};

struct wlr_buffer *
hikari_server_create_argb8888_buffer(int width, int height, unsigned char *data, int stride)
{
  // [COMMENT] Action purpose: Reject degenerate geometry and guard the size
  // computation against overflow before allocating.
  if (width <= 0 || height <= 0 || stride <= 0 || data == NULL) {
    return NULL;
  }

  // [COMMENT] Action purpose: ARGB8888 is 4 bytes/pixel; a stride shorter than
  // that would make the flat memcpy below (stride * height bytes) read past
  // the end of the source buffer. Guard the width*4 multiplication against
  // overflow before comparing.
  size_t min_stride = (size_t)width * 4;
  if (min_stride / 4 != (size_t)width || (size_t)stride < min_stride) {
    return NULL;
  }

  size_t byte_count = (size_t)stride * (size_t)height;
  if (byte_count / (size_t)stride != (size_t)height) {
    return NULL;
  }

  // [COMMENT] Action purpose: Graceful-degradation allocation. This helper's
  // contract is already "return NULL on failure" (see the geometry/overflow
  // guards above), and every caller (hikari_bar_refresh, hikari_indicator_bar_
  // update) already handles a NULL return by skipping that one UI element's
  // repaint rather than crashing -- aborting here would have defeated that
  // contract for the one failure mode that actually matters. See
  // DECISIONS_LOG Finding 4.
  struct hikari_argb8888_buffer *buffer =
      hikari_try_malloc(sizeof(struct hikari_argb8888_buffer));
  if (buffer == NULL) {
    return NULL;
  }

  unsigned char *buffer_data = hikari_try_malloc(byte_count);
  if (buffer_data == NULL) {
    hikari_free(buffer);
    return NULL;
  }

  wlr_buffer_init(&buffer->base, &argb8888_buffer_impl, width, height);
  buffer->format = DRM_FORMAT_ARGB8888;
  buffer->stride = (size_t)stride;
  buffer->data = buffer_data;
  memcpy(buffer->data, data, byte_count);

  return &buffer->base;
}

void
hikari_server_decrease_view_size_down(void *arg)
{
  const int step = hikari_configuration->step;
  move_resize_view(0, step, 0, -step);
}

void
hikari_server_decrease_view_size_right(void *arg)
{
  const int step = hikari_configuration->step;
  move_resize_view(step, 0, -step, 0);
}

void
hikari_server_increase_view_size_up(void *arg)
{
  const int step = hikari_configuration->step;
  move_resize_view(0, -step, 0, step);
}

void
hikari_server_increase_view_size_left(void *arg)
{
  const int step = hikari_configuration->step;
  move_resize_view(-step, 0, step, 0);
}

void
hikari_server_lower_group(void *arg)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  struct hikari_group *group = focus_view->group;

  hikari_group_lower(group, focus_view);
  hikari_server_cursor_focus();
}

void
hikari_server_raise_group(void *arg)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  struct hikari_group *group = focus_view->group;

  hikari_group_raise(group, focus_view);
}

void
hikari_server_only_group(void *arg)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  struct hikari_group *group = focus_view->group;

  struct hikari_output *output;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    hikari_workspace_clear(output->workspace);
  }

  hikari_group_show(group);
  hikari_server_cursor_focus();
}

void
hikari_server_hide_group(void *arg)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view == NULL) {
    return;
  }

  struct hikari_group *group = focus_view->group;
  assert(group != NULL);

  hikari_group_hide(group);
  hikari_server_cursor_focus();
}

#ifndef NDEBUG
void
hikari_server_toggle_damage_tracking(void *arg)
{
  hikari_server.track_damage = !hikari_server.track_damage;

  struct hikari_output *output = NULL;
  wl_list_for_each (output, &hikari_server.outputs, server_outputs) {
    hikari_output_damage_whole(output);
  }
}
#endif
