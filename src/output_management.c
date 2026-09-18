/* Script function and purpose: zwlr_output_manager_v1 -- runtime display
configuration. Advertises every output with its modes to clients such as
wlr-randr, kanshi and wdisplays, and applies the position, mode, refresh rate,
scale and transform they ask for.

Whether a client may change anything at all is the user's decision, taken with
the global `output_management_overrides_config` key. When it is false this
compositor's own `outputs { }` block leads: clients may still READ the
configuration, which is what keeps `wlr-randr` useful as a way to find out what
modes a monitor has, but every apply and test is refused. */

#include <hikari/output_management.h>

#include <stdlib.h>

#include <wlr/backend.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_swapchain_manager.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include <hikari/configuration.h>
#include <hikari/output.h>
#include <hikari/server.h>

/* Function purpose: Release the array wlr_output_configuration_v1_build_state()
hands back. Every element was wlr_output_state_init()'d on the compositor's
behalf, so each one has to be finished individually before the array itself is
freed -- freeing the array alone leaks whatever the states hold. */
static void
destroy_states(struct wlr_backend_output_state *states, size_t states_len)
{
  if (states == NULL) {
    return;
  }

  for (size_t i = 0; i < states_len; i++) {
    wlr_output_state_finish(&states[i].base);
  }

  free(states);
}

/* Function purpose: Decide whether a client may change anything, and say why in
the log when it may not.

Both refusals reject the WHOLE configuration, because the protocol offers no way
to refuse part of one: zwlr_output_configuration_v1 carries a single
succeeded/failed reply covering every head it was given.

It rejects the WHOLE configuration, because the protocol offers no way to refuse
part of one: zwlr_output_configuration_v1 carries a single succeeded/failed
reply covering every head it was given. */
static bool
configuration_is_permitted(struct wlr_output_configuration_v1 *config)
{
  if (!hikari_configuration->output_management_overrides_config) {
    wlr_log(WLR_INFO,
        "output management: refusing a client configuration -- "
        "\"output_management_overrides_config\" is false, so this "
        "compositor's own configuration leads");
    return false;
  }

  return true;
}

/* Function purpose: Test, and unless this is a dry run also commit, a client's
configuration.

Only the outputs named by `config` are touched. A configuration is allowed to
name a subset of the heads -- wlroots refuses to let a client configure the same
head twice but never requires that every head be mentioned -- so walking
hikari_server.outputs here instead would silently reconfigure screens the client
never asked about. */
static bool
configuration_apply(struct wlr_output_configuration_v1 *config, bool test_only)
{
  size_t states_len = 0;
  struct wlr_backend_output_state *states =
      wlr_output_configuration_v1_build_state(config, &states_len);

  if (states == NULL && states_len > 0) {
    wlr_log(WLR_ERROR,
        "output management: could not build the output state for a client "
        "configuration");
    return false;
  }

  /* Action purpose: Allocating swapchains IS the test, and a plain
  wlr_backend_test() is not a substitute.

  Turning an output on is a mode-setting commit, and one of those scans out of
  the primary plane. wlroots borrows the plane's existing framebuffer when the
  state carries none -- but an output that has been switched off no longer has
  one, so the commit is refused however valid the mode is. Testing and
  committing without allocating first therefore made "off" a one-way door: every
  configuration that turned an output on failed, while every configuration that
  kept it off succeeded.

  This helper allocates a swapchain per output for the configuration being
  tried, which is both the feasibility question and the thing that produces the
  framebuffer the commit then needs. */
  struct wlr_output_swapchain_manager swapchains;
  wlr_output_swapchain_manager_init(&swapchains, hikari_server.backend);

  bool success =
      wlr_output_swapchain_manager_prepare(&swapchains, states, states_len);

  if (!success || test_only) {
    goto out;
  }

  /* Action purpose: Outputs coming on need their scene output before the loop
  below, because that is what the frame is rendered from. Only the layout and
  scene halves happen here -- the CRTC is left alone until the backend commit,
  so a refused configuration leaves nothing lit. */
  size_t attached = 0;
  for (; attached < states_len; attached++) {
    struct hikari_output *output = states[attached].output->data;

    if (output == NULL || !states[attached].base.enabled ||
        output->wants_enabled) {
      continue;
    }

    if (!hikari_output_attach(output)) {
      success = false;
      break;
    }
  }

  if (!success) {
    for (size_t i = 0; i < attached; i++) {
      struct hikari_output *output = states[i].output->data;

      if (output != NULL && states[i].base.enabled && !output->wants_enabled) {
        hikari_output_detach(output);
      }
    }
    goto out;
  }

  /* Action purpose: Render one frame of the new configuration into the
  swapchain allocated for it, so the commit below has something to scan out. */
  for (size_t i = 0; i < states_len; i++) {
    if (!states[i].base.enabled) {
      continue;
    }

    struct hikari_output *output = states[i].output->data;

    if (output == NULL || output->scene_output == NULL) {
      continue;
    }

    struct wlr_swapchain *swapchain =
        wlr_output_swapchain_manager_get_swapchain(
            &swapchains, states[i].output);

    if (swapchain == NULL) {
      continue;
    }

    struct wlr_scene_output_state_options options = { .swapchain = swapchain };
    wlr_scene_output_build_state(
        output->scene_output, &states[i].base, &options);
  }

  success = wlr_backend_commit(hikari_server.backend, states, states_len);

  if (success) {
    wlr_output_swapchain_manager_apply(&swapchains);
  } else {
    /* Action purpose: A refused commit means nothing that was asked for
    happened, so the layout and scene halves taken above have to come back off.
    Leaving them puts an output in the layout that is not part of the desktop --
    which skews the extents the next output is placed against, and leaves the
    next attach of the same output returning early, so a later rollback would
    destroy a scene output it did not create. */
    for (size_t i = 0; i < attached; i++) {
      struct hikari_output *output = states[i].output->data;

      if (output != NULL && states[i].base.enabled && !output->wants_enabled) {
        hikari_output_detach(output);
      }
    }
  }

out:
  wlr_output_swapchain_manager_finish(&swapchains);
  destroy_states(states, states_len);

  if (test_only || !success) {
    return success;
  }

  /* Action purpose: Position is the one thing the backend commit does not
  carry. wlr_output_head_v1_state_apply() leaves it to the caller by design,
  because a position belongs to the output layout rather than to the hardware.

  Re-adding an output that is already in the layout simply moves it, and that is
  what raises the layout's change event -- which is in turn what re-derives
  every output's geometry and usable area, the top bar's reservation, the
  wallpaper, the layer-shell arrangement and the tiling. Outputs that did not
  actually move are skipped so a one-monitor change does not drag every other
  screen through that pass. Their geometry is already current here: a mode
  change raises the same event from inside the commit above, before this
  runs.

  A head that is not enabled carries no position to apply. The protocol has no
  request that sets one on a disabled head, so wlroots allocates the head
  zeroed and fills in only the output and the flag -- meaning its x and y read
  as 0 rather than as "unchanged". Moving an output to the layout origin
  because a client mentioned it while it was off is not what was asked for, and
  on a multi-monitor layout it would drag the others with it. */
  struct wlr_output_configuration_head_v1 *config_head;
  wl_list_for_each (config_head, &config->heads, link) {
    struct hikari_output *output = config_head->state.output->data;

    if (output != NULL) {
      hikari_output_set_wants_enabled(output, config_head->state.enabled);
    }

    if (!config_head->state.enabled) {
      continue;
    }

    if (output != NULL && output->geometry.x == config_head->state.x &&
        output->geometry.y == config_head->state.y) {
      continue;
    }

    if (wlr_output_layout_add(hikari_server.output_layout,
            config_head->state.output,
            config_head->state.x,
            config_head->state.y) == NULL) {
      fprintf(stderr,
          "error: failed to position output \"%s\" at %d,%d; its enabled/"
          "mode changes from this configuration were still applied\n",
          config_head->state.output->name,
          config_head->state.x,
          config_head->state.y);

      /* Action purpose: The client's request asked for this position, and
      it did not happen -- reporting success here would tell a well-behaved
      client (kanshi, wdisplays) that the output is where it asked for it
      to be when it is not. Reporting failure does not undo the mode/
      enablement changes already committed above: this protocol's "failed"
      means "not everything you asked for took effect", not "roll back
      what did". */
      success = false;
    }
  }

  return success;
}

/* Function purpose: Shared body of both requests.

wlroots hands the compositor ownership of the configuration, and exactly one of
succeeded/failed must be sent before it is destroyed -- both send functions
assert that no feedback has gone out yet, so a second reply aborts the
compositor rather than confusing the client. */
static void
handle_request(struct wlr_output_configuration_v1 *config, bool test_only)
{
  bool success = configuration_is_permitted(config) &&
                 configuration_apply(config, test_only);

  if (success) {
    wlr_output_configuration_v1_send_succeeded(config);
  } else {
    wlr_output_configuration_v1_send_failed(config);
  }

  wlr_output_configuration_v1_destroy(config);
}

/* Function purpose: A dry run. Refused on exactly the same terms as a real
apply, deliberately: letting `wlr-randr --dryrun` report success against a
configuration that the compositor would then reject would make the dry run worse
than useless. */
static void
test_handler(struct wl_listener *listener, void *data)
{
  handle_request(data, true);
}

static void
apply_handler(struct wl_listener *listener, void *data)
{
  struct hikari_server *server = &hikari_server;

  /* Action purpose: Hold off the broadcast that each layout change would
  otherwise trigger. One apply can move several outputs, every move raises the
  layout's change event, and every broadcast reporting a real difference takes a
  fresh serial -- which cancels other clients' in-flight configurations, and can
  leave a profile daemon such as kanshi reacting to its own work. One apply, one
  broadcast, once it has all landed. */
  server->output_management_applying = true;
  handle_request(data, false);
  server->output_management_applying = false;

  hikari_output_management_broadcast();
}

void
hikari_output_management_init(struct hikari_server *server)
{
  server->output_management_applying = false;
  server->output_management = wlr_output_manager_v1_create(server->display);

  if (server->output_management == NULL) {
    /* Action purpose: Not fatal, unlike most global creation failures in this
    file. What is lost is the ability to reconfigure displays at runtime, and
    every other part of the session still works -- refusing to start would be a
    worse answer to that than a session that runs with one feature missing.
    Initialise the links anyway so hikari_output_management_fini() can remove
    them unconditionally. */
    wlr_log(WLR_ERROR,
        "output management: could not create zwlr_output_manager_v1; "
        "wlr-randr and kanshi will not work this session");

    wl_list_init(&server->output_management_test.link);
    wl_list_init(&server->output_management_apply.link);
    return;
  }

  server->output_management_test.notify = test_handler;
  wl_signal_add(&server->output_management->events.test,
      &server->output_management_test);

  server->output_management_apply.notify = apply_handler;
  wl_signal_add(&server->output_management->events.apply,
      &server->output_management_apply);
}

void
hikari_output_management_fini(struct hikari_server *server)
{
  wl_list_remove(&server->output_management_test.link);
  wl_list_remove(&server->output_management_apply.link);
  wl_list_init(&server->output_management_test.link);
  wl_list_init(&server->output_management_apply.link);

  /* Action purpose: The manager itself belongs to the display and is destroyed
  with it. Dropping the pointer here is what makes a broadcast during the rest
  of teardown a no-op rather than a use-after-free. */
  server->output_management = NULL;
}

void
hikari_output_management_broadcast(void)
{
  struct hikari_server *server = &hikari_server;

  if (server->output_management == NULL || server->output_management_applying) {
    return;
  }

  struct wlr_output_configuration_v1 *config =
      wlr_output_configuration_v1_create();

  if (config == NULL) {
    wlr_log(WLR_ERROR,
        "output management: could not allocate the current configuration; "
        "clients keep the last one they were told about");
    return;
  }

  /* Action purpose: hikari_server.outputs holds the real outputs only -- the
  headless noop output is never inserted into it -- so the fallback screen the
  compositor keeps for when no monitor is attached stays invisible to clients
  without needing a case of its own here. */
  struct hikari_output *output;
  wl_list_for_each (output, &server->outputs, server_outputs) {
    struct wlr_output_configuration_head_v1 *config_head =
        wlr_output_configuration_head_v1_create(config, output->wlr_output);

    if (config_head == NULL) {
      wlr_log(WLR_ERROR,
          "output management: could not add output \"%s\" to the current "
          "configuration; not publishing a partial one",
          output->wlr_output->name);

      wlr_output_configuration_v1_destroy(config);
      return;
    }

    /* Action purpose: Every other field is pre-filled from the wlr_output, but
    the position is not, because wlroots does not know it -- it lives in the
    output layout, which is where hikari keeps it. */
    config_head->state.x = output->geometry.x;
    config_head->state.y = output->geometry.y;
  }

  wlr_output_manager_v1_set_configuration(server->output_management, config);
}
