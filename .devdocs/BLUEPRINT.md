# Hikari Project Blueprint

*Last Updated:* 2026-07-31 01:15

## 1. Subsystem Architecture

```text
                                  +-----------------------+
                                  |     main() Entry      |
                                  +-----------+-----------+
                                              |
                                              v
                                  +-----------------------+
                                  |   src/server.c        |
                                  |   (Server Lifecycle)  |
                                  +-----------+-----------+
                                              |
           +----------------------------------+----------------------------------+
           |                                  |                                  |
           v                                  v                                  v
+---------------------+            +---------------------+            +---------------------+
|  src/output.c       |            |   src/view.c        |            |  src/keyboard.c     |
|    (wlr_scene)      |            |   src/workspace.c   |            |  src/pointer.c      |
|                     |            | (Views/Sheets/Tiles)|            |  (Input Events)     |
+---------------------+            +---------------------+            +---------------------+
```

## 2. Modal State Machine Index

| Mode | Handler File | Description |
|---|---|---|
| **Normal Mode** | `src/normal_mode.c` | Default state. Keybindings and mouse gestures dispatch window actions. |
| **Move Mode** | `src/move_mode.c` | Interactive view repositioning (via cursor drag or keybinding). |
| **Resize Mode** | `src/resize_mode.c` | Interactive view resizing (via cursor drag or keybinding). |
| **Lock Mode** | `src/lock_mode.c` | Screen locker overlay active; dispatches input to lock screen indicator. |
| **Sheet Assign Mode** | `src/sheet_assign_mode.c` | Modal prompt for reassigning focused view to target sheet (0-9). |
| **Group Assign Mode** | `src/group_assign_mode.c` | Modal prompt for assigning focused view to named group. |
| **Mark Assign Mode** | `src/mark_assign_mode.c` | Modal prompt for tagging focused view with quick-jump mark (a-z). |
| **Mark Select Mode** | `src/mark_select_mode.c` | Modal prompt for jumping focus to tagged view mark. |
| **Layout Select Mode** | `src/layout_select_mode.c` | Modal prompt for applying tile layout algorithms. |
| **Input Grab Mode** | `src/input_grab_mode.c` | Redirects input to explicit client surface request. |
| **Grab Keyboard Mode**| `src/grab_keyboard_mode.c` | Exclusively forwards raw keyboard events to target application. |

## 3. Implementation Registry

* **Standard Memory Allocation:** Migrated compositor allocation paths to `hikari_malloc`.
* **wlr_scene Rendering:** Migrated borders, lock indicator, backgrounds, and indicator bars to wlroots `wlr_scene` graph.
* **wlroots 0.20 Migration:** Resolved 7 API-breaking changes across `cursor.c`, `output.c`, `server.c`, `switch.c`, and `xdg_view.c`.
* **FreeBSD Adaptations:** Native evdev headers adopted, Makefile points to wlroots 0.20 via pkg-config with epoll-shim conditional.
* **DOD Reverted:** Struct-of-Arrays (SoA) and Object Pool implementations were removed as they added unnecessary complexity and are incompatible with `wlr_scene` workflows.
