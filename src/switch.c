// [COMMENT] Script function and purpose: Handling hardware switch input events (e.g., laptop lid toggle).

#include <hikari/switch.h>

#include <hikari/action.h>
#include <hikari/memory.h>
#include <hikari/server.h>
#include <hikari/switch_config.h>

static void
execute_action(void (*action)(void *arg), void *arg)
{
  if (hikari_server_in_lock_mode()) {
    return;
  }

  if (!hikari_server_in_normal_mode()) {
    hikari_server_enter_normal_mode(NULL);
  }

  action(arg);
}

static void
destroy_handler(struct wl_listener *listener, void *data)
{
  struct hikari_switch *swtch = wl_container_of(listener, swtch, destroy);

  hikari_switch_fini(swtch);
}

// [COMMENT] Function purpose: Handle hardware switch toggle events (e.g. laptop
// lid open/close). Uses the switch_state from the wlr_switch_toggle_event as
// the authoritative hardware state and dispatches begin/end actions accordingly.
static void
toggle_handler(struct wl_listener *listener, void *data)
{
  struct hikari_switch *swtch = wl_container_of(listener, swtch, toggle);
  struct wlr_switch_toggle_event *event = data;

  // [COMMENT] Action purpose: Use the hardware-reported state from the event
  // as authoritative, rather than flipping the internally tracked state.
  swtch->state = event->switch_state;

  // [COMMENT] Action purpose: Guard against NULL action pointer — action is
  // NULL between hikari_switch_init and hikari_switch_configure, or after
  // hikari_switch_reset.
  if (swtch->action == NULL) {
    return;
  }

  if (swtch->state == WLR_SWITCH_STATE_ON) {
    struct hikari_event_action *begin = &swtch->action->begin;
    // [COMMENT] Action purpose: Only dispatch begin action if its callback
    // function pointer is set (action may be configured without a begin handler).
    if (begin->action != NULL) {
      execute_action(begin->action, begin->arg);
    }
  } else if (swtch->state == WLR_SWITCH_STATE_OFF) {
    struct hikari_event_action *end = &swtch->action->end;
    // [COMMENT] Action purpose: Only dispatch end action if its callback
    // function pointer is set (action may be configured without an end handler).
    if (end->action != NULL) {
      execute_action(end->action, end->arg);
    }
  }
}

// [COMMENT] Function purpose: Initialize a switch input device, set default
// state, attach destroy signal listener to the base wlr_input_device, and
// register in the server's switch list.
void
hikari_switch_init(struct hikari_switch *swtch, struct wlr_input_device *device)
{
  // [COMMENT] Action purpose: Obtain the wlr_switch from the generic input
  // device and initialize switch state and action pointer.
  struct wlr_switch *wlr_switch = wlr_switch_from_input_device(device);
  swtch->wlr_switch = wlr_switch;
  swtch->state = WLR_SWITCH_STATE_OFF;
  swtch->action = NULL;

  // [COMMENT] Action purpose: Register the destroy listener on the base input
  // device events so the switch is cleaned up when hardware is removed.
  swtch->destroy.notify = destroy_handler;
  wl_signal_add(&wlr_switch->base.events.destroy, &swtch->destroy);

  // [COMMENT] Action purpose: Initialize the toggle listener link so it can
  // be safely removed later even if no configuration has been applied yet.
  wl_list_init(&swtch->toggle.link);

  wl_list_insert(&hikari_server.switches, &swtch->server_switches);
}

void
hikari_switch_fini(struct hikari_switch *swtch)
{
  wl_list_remove(&swtch->destroy.link);
  wl_list_remove(&swtch->toggle.link);
  wl_list_remove(&swtch->server_switches);
}

// [COMMENT] Function purpose: Apply a switch configuration, binding the action
// set and registering the toggle event listener on the wlr_switch.
void
hikari_switch_configure(
    struct hikari_switch *swtch, struct hikari_switch_config *switch_config)
{
  swtch->action = &switch_config->action;

  // [COMMENT] Action purpose: Remove any previous toggle listener before
  // re-registering, preventing duplicate signal subscriptions.
  wl_list_remove(&swtch->toggle.link);
  swtch->toggle.notify = toggle_handler;
  wl_signal_add(&swtch->wlr_switch->events.toggle, &swtch->toggle);
}


void
hikari_switch_reset(struct hikari_switch *swtch)
{
  swtch->action = NULL;
}
