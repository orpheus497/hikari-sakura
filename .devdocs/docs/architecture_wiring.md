# Hikari Architecture & Wiring Reference

## Overview
`hikari` is a modular Wayland compositor structured around a core server object (`struct hikari_server`), modal state machines (`struct hikari_mode`), spatial hierarchy elements (`hikari_workspace`, `hikari_sheet`, `hikari_group`, `hikari_view`), and event listeners linked to `wlroots` signals.

---

## 1. High-Level Component Architecture

```text
                                  +-----------------------+
                                  |     main() Entry      |
                                  +-----------+-----------+
                                              |
                                              v
                                  +-----------------------+
                                  |   hikari_server.c     |
                                  |   (Server Lifecycle)  |
                                  +-----------+-----------+
                                              |
           +----------------------------------+----------------------------------+
           |                                  |                                  |
           v                                  v                                  v
+---------------------+            +---------------------+            +---------------------+
<<<<<<< HEAD
|  src/output.c       |            |   hikari_view.c     |            |  hikari_keyboard.c  |
|  (wlr_scene Output) |            | hikari_workspace.c  |            |  hikari_pointer.c   |
|                     |            | (Views/Sheets/Tiles)|            |  (Input Events)     |
=======
|  hikari_output.c    |            |   hikari_view.c     |            |  hikari_keyboard.c  |
|    (wlr_scene)      |            | hikari_workspace.c  |            |  hikari_pointer.c   |
| (Output & Render)  |            | (Views/Sheets/Tiles)|            |  (Input Events)     |
>>>>>>> 930f3bf (chore: complete wlroots 0.20 API migration and update documentation compliance)
+---------------------+            +---------------------+            +---------------------+
```

---

## 2. Server Lifecycle & Execution Flow

1. **Initialization (`main.c`):**
   * Parses CLI flags (`-c <config>`, `-a <autostart>`, `-v`, `-h`).
   * Evaluates `$XDG_CONFIG_HOME/hikari/hikari.conf` or `/usr/local/etc/hikari/hikari.conf`.
   * Invokes `hikari_server_prepare_privileged()` to initialize backend security and drops root permissions.
   * Asserts `geteuid() != 0` as a diagnostic assertion. If strict enforcement is required, an explicit runtime check with a safe failure path should be used.
   * Calls `hikari_server_start(config_path, autostart)`.

2. **Server Setup (`src/server.c`):**
   * Initializes `wl_display`, `wlr_backend`, `wlr_renderer`, and `wlr_allocator`.
   * Initializes input subsystems: `wlr_compositor`, `wlr_subcompositor`, `wlr_data_device_manager`, `wlr_seat`, `wlr_output_layout`.
   * Configures shell protocols: XDG Shell (`wlr_xdg_shell`), Layer Shell (`wlr_layer_shell_v1`), Gamma Control, Screencopy, Virtual Input.
   * Loads user configuration via LibUCL parser ([src/configuration.c](../src/configuration.c)).
   * Executes autostart binary/script if specified.
   * Starts Wayland event loop (`wl_display_run`).

3. **Teardown (`hikari_server_stop`):**
   * Destroys views, outputs, sheets, groups, layouts, and input devices.
   * Closes Wayland display socket and frees server allocation memory.

---

## 3. Core Objects & Data Structures

### `hikari_server`
Central singleton managing all outputs, views, active modes, keybindings, input devices, dynamic configuration, and workspace sheets.

### `hikari_workspace`
Active view visibility container per output. Contains tile trees (`hikari_tile`), focus stacks, sheet mappings, and output layout geometries.

### `hikari_sheet`
General-purpose virtual desktop sheets (Sheets **1** through **9**, plus sticky Sheet **0**).
* **Sheet 0:** Sticky background/foreground layer (visible across all active sheets).
* **Sheets 1-9:** Discrete view collections. Switching active sheet swaps visible workspace contents.

### `hikari_group`
Named logical aggregations of views spanning multiple sheets and outputs. Supports group-wide move, raise, lower, hide, and layout operations.

### `hikari_view` (Abstract Base)
Base view structure for window surfaces. Extended by:
* `hikari_xdg_view`: Native Wayland XDG toplevel surface.
* `hikari_xwayland_view`: Legacy X11 view managed via XWayland.
* `hikari_xwayland_unmanaged_view`: X11 popups, override-redirect windows, tooltips.

---

## 4. Modal Input State Machine

`hikari` handles user interaction through a strict modal state machine (`struct hikari_mode`):

| Mode | Handler File | Trigger / Function |
|------|--------------|--------------------|
| **Normal Mode** | `src/normal_mode.c` | Default operational state. Keybindings and mouse gestures dispatch window actions. |
| **Move Mode** | `src/move_mode.c` | Interactive view repositioning (via cursor drag or keybinding). |
| **Resize Mode** | `src/resize_mode.c` | Interactive view resizing (via cursor drag or keybinding). |
| **Lock Mode** | `src/lock_mode.c` | Screen locker overlay active; dispatches input to lock screen indicator. |
| **Sheet Assign Mode** | `src/sheet_assign_mode.c` | Modal prompt for reassigning focused view to target sheet (0-9). |
| **Group Assign Mode** | `src/group_assign_mode.c` | Modal prompt for assigning focused view to named group. |
| **Mark Assign Mode** | `src/mark_assign_mode.c` | Modal prompt for tagging focused view with quick-jump mark (a-z). |
| **Mark Select Mode** | `src/mark_select_mode.c` | Modal prompt for jumping focus to tagged view mark. |
| **Layout Select Mode** | `src/layout_select_mode.c` | Modal prompt for applying tile layout algorithms (grid, stack, queue). |
| **Input Grab Mode** | `src/input_grab_mode.c` | Redirects input to explicit client surface request. |
| **Grab Keyboard Mode** | `src/grab_keyboard_mode.c` | Exclusively forwards raw keyboard events to target application. |

---

## 5. Rendering Pipeline

Rendering uses the `wlr_scene` graph from wlroots. The compositor owns scene node creation, ordering, damage submission, and frame scheduling:

1. **Damage Tracking:** The compositor submits damage via `wlr_damage_ring_add_whole` and `wlr_damage_ring_add_box` on `scene_output->damage_ring`, and schedules frames with `wlr_output_schedule_frame`.
2. **Scene Composition:** The compositor creates and manages scene graph nodes:
   * **Borders:** `wlr_scene_rect` nodes attached to view scene trees.
   * **Indicator Frames:** `wlr_scene_rect` nodes for modal view highlighting.
   * **Lock Indicator:** `wlr_scene_buffer` node per output.
   * **Backgrounds / Wallpapers:** `wlr_scene_buffer` nodes per output.
   * **Indicator Bars:** `wlr_scene_buffer` nodes for title/sheet/group/mark text.
3. **Layer Shell:** Docks, status bars, notification popups, and launcher menus are integrated via `wlr_scene_layer_tree`.
4. **Render Execution:** `wlr_scene_output_commit` handles damage region intersection, z-ordering, and buffer swapping. The compositor calls this from its frame handler and manages `send_frame_done` timing.
