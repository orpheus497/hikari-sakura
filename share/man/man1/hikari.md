NAME
====
**hikari** - the Hikari Sakura Wayland compositor for FreeBSD and Linux

SYNTAX
======
**hikari** [-vh] [-a \<executable\>] [-c \<config\>]

DESCRIPTION
===========

**Hikari Sakura** is a FreeBSD-focused revamp and modernization of the original
Hikari -- created by *raichoo* and later carried at
<https://github.com/antaz/hikari>, which has since been abandoned upstream. It
is explicitly designed as a comprehensive Wayland desktop environment rather
than as a compositor alone.

FreeBSD is the first-class platform and Linux the second-class one. Both are
supported and both are built from this one tree. Where the two differ the
compositor takes the native interface on each rather than a lowest common
denominator, and no FreeBSD behaviour is given up for the sake of the Linux
build; the places where a dependency differs between them are named where they
arise below.

**Hikari Sakura** is the name of the desktop environment; **hikari** is the
compositor binary, the configuration file and this manual page, and is what
this page refers to throughout.

It is a stacking Wayland compositor with additional tiling capabilities,
it is heavily inspired by the Calm Window manager (cwm(1)). Its core concepts
are views, workspaces, sheets and groups.

Besides managing windows, **hikari** draws its own top bar and its own screen
locker in-process; both are themed from the same *palette* as everything else.
See **TOP BAR** and the *Lock screen* section below.

The desktop environment is completed by three sibling projects, each installed
and configured independently:

* **sakura** -- the display manager, and the source of the second half of the
  desktop's name. A FreeBSD-only TUI login manager that runs on a virtual
  terminal and talks to OpenPAM directly, needing no graphical toolkit, session
  bus or login-manager framework. It launches this compositor through either of
  the two installed session entries; see **FILES**.
  <https://github.com/orpheus497/sakura>

* **sofi** -- the shell, and everything summoned rather than always present. One
  binary providing the control panel, application menu, sheet switcher, volume
  and network panes, notification daemon and history, and message toasts. Under
  this compositor each is a *zwlr\_layer\_shell\_v1* surface, bound at version 4;
  sofi also has *xcb* and *xdg-shell* fallbacks for other environments. The
  default configuration binds four keys to it -- the control panel on *L+w* is
  the entry point to the surfaces that have no key of their own -- and its sheet
  switcher is a client of the control socket described under **CONTROL SOCKET**.
  <https://github.com/orpheus497/sofi>

* **saber** -- the panel, and the one surface that is always there. A persistent
  vertical launcher in the tradition of the Unity 7 launcher, carrying running
  indicators, quicklists, an application dash, a window spread, the system tray
  and session controls in one always-present column. It reserves an exclusive
  zone, so views tile beside it rather than under it, and it reads the
  compositor's sheets over the same control socket. FreeBSD only, and written
  for this compositor alone. The default configuration binds three keys to it.
  <https://github.com/orpheus497/saber>

**sofi** and **saber** divide by persistence: every sofi surface is summoned,
does one job and dismisses, reserving no space; saber stays for the whole
session and reserves its column. The persistent system tray is saber's --
**sofi -tray-daemon** is retained for sessions without saber and draws no
surface of its own -- and since exactly one process on a session bus may own
*org.kde.StatusNotifierWatcher*, run one tray host, not both. The system status
readout stays in this compositor's own top bar and is duplicated by neither.

Neither is required: **hikari** runs on its own, and any layer-shell client or
display manager works in their place.

The following options are available:

-a *\<executable\>* Specify autostart executable.

-c *\<config\>* Specify a configuration file.

-h Show this message and quit.

-v Show version and quit.

CONCEPTS
========

View
----
_Views_ are basically the windows of a Wayland client. Each _view_ belongs to at
most one sheet and can also belong to at most one group. A _view_ can be in
several states.

* **hidden**

  *Hidden* _views_ are not displayed on the workspace. Hiding a _view_ removes
  this _view_ from the workspace.

* **tiled**

  A *tiled* _view_ is part of a layout. They can never be *floating* or
  *invisible*.

* **floating**

  *Floating* _views_ can never become part of a layout. The floating state is
  indicated using a tilde in the sheet indicator.

* **invisible**

  When a _view_ is set into *invisible* state it will not be displayed when
  switching to the containing sheet and stay hidden until it is explicitly
  requested to be shown. This can be used to keep long running _views_ from
  cluttering the workspace. An *invisible* _view_ can never be *tiled* and are
  indicated using square brackets in the sheet indicator.

* **maximized** (horizontal, vertical and full)

  Views with such a state can take up the entire horizontal and or vertical
  space of a workspace. *Tiled* _views_ can also be maximized.

* **borrowed**

  Borrowing happens when a workspace contains a _view_ that view that is not
  part of the **current sheet**. These views are called *borrowed* views and
  are indicated by the sheet indicator using the string "**x** @ **y**", where
  **x** is the sheet the _view_ is a member of and **y** is the sheet that is
  currently borrowing the _view_.

* **public**

  *Public* views are also displayed on the lock screen, in this case they will
  never accept input. Views that display sensible information should never be
  marked as *public*. The public state is indicated using an exclamation mark in
  the sheet indicator.


Workspace
---------
A _workspace_ is the set of views that are currently visible. Unlike in most
other Wayland compositors, **hikari** only has a single _workspace_ for each
output and we only manipulate its content using actions. While this seems like a
superficial distinction it is important to keep in mind for some actions to make
sense. When switching to a sheet this sheet becomes the **workspace sheet**. On
startup a _workspace_ sheet is **1**. Views on a _workspace_ are stacked from
bottom to top, where the next view is higher up the stack and the previous view
is one below. This order can be changed by raising or lowering views
individually via actions. Selecting a view via cycling actions automatically
raises this view to the top of the stacking order.

**hikari** provides multiple ways to *cycle* the views on a _workspace_. Cycling
is a way to navigate to a view using key bindings.

Sheet
-----
A _sheet_ is a collection of views, each view can only be a member of a single
_sheet_. Switching between sheets will replace the current content of the
workspace with all the views that are a member of the selected _sheet_, this
_sheet_ will also become the **current sheet**. **hikari** has 9 general
purpose sheets that correspond to the numbers **1** to **9** and a special
purpose _sheet_ **0**. Views that are a member of _sheet_ **0** will always be
visible but stacked below the views of the selected _sheet_. A _sheet_ that
contains views is called **inhabited**.

When switching to a different sheet the current **current sheet** becomes the
**alternate sheet**.

Group
-----
_Groups_ are a bit more fine grained than sheets. Like sheets, _groups_ are a
collection of views. Unlike sheets you can have a arbitrary number of _groups_
and each _group_ can have an arbitrary name. Views from one _group_ can be
spread among all available sheets. Some actions act on entire _groups_ rather
than individual views.

Layout
------
Each sheet can have at most one _layout_ for tiling views. Applying a _layout_
to a sheet introduces an additional ordering for views which is not altered by
raising or lowering, which can be used to traverse the _layout_ in the expected
order. Each _layout_ can be stored in one of the _layout_ register **a** to
**z**.

View indicators
---------------
_View indicators_ show information about the current view as well as views
belonging to the same group. They outline the border of the current view as
well as all view borders belonging to the same group (obscured view borders
will shine through the obscuring view). The focused view will also display
so called **indicator bars**. Each bar holds some information, like title, sheet
information, group and its mark (if one has been set for the view).

Marks
-----
_Marks_ can be used to "speed dial" views, even if they are on a sheet other
than the **current sheet** (note: such views will become **borrowed** in the
process). _Marks_ are represented by characters from **a** to **z**. When jumping
to a _mark_ the view will be brought forward and focused. If no view is bound to
a _mark_ the user can specify a command that is executed instead. This can be
used to start an application that binds itself to this _mark_.

Mode
----
**hikari** is a modal Wayland compositor and therefore offers several _modes_ for
actions like changing a views group, mark or sheet as well a jumping to marks or
grabbing input events and layout selection.

CONFIGURATION
=============

**hikari** is configured by a Lua 5.4 chunk. The configuration is located
under _$XDG_CONFIG_HOME/hikari/hikari.lua_; when **XDG_CONFIG_HOME** is unset
this is _~/.config/hikari/hikari.lua_. If no user configuration is readable
**hikari** is going to try _hikari.lua_ from the install _etc_ directory.

The default configuration is going to use the **TERMINAL** environment
variable as your standard terminal application, falling back to **foot** when
it is unset.

Structural changes like UI themes, custom actions, and new bindings can be
hot-reloaded using the **reload** action. `xkb` settings configured natively in
the *keyboards* section of the config file are reapplied to already-connected
keyboards on every reload, same as any other config value. Settings that come
from the session environment instead of the config file -- `xkb` environment
variables used as a fallback for keyboards with no native configuration, and
XWayland initialization -- are only read once at startup and require a full
restart of the compositor to apply.

On startup **hikari** attempts to execute _~/.config/hikari/autostart_ to
autostart applications.

The configuration language
--------------------------
The file is a Lua chunk that must `return` exactly one table, and every section
documented below is an entry in that table. A chunk that returns nothing, or
returns anything that is not a table, fails the load and says so. The file is
read as source text only and never as precompiled bytecode: Lua's bytecode
verifier is not a security boundary, since malformed bytecode corrupts memory
rather than raising an error.

Because the configuration is a program rather than a data file, a repetitive
block -- the per-sheet bindings, say -- can be generated instead of typed.
`for` loops, local functions, `string.format`, `table.insert`, `math.max`,
`pairs` and `ipairs` are all available. The language itself is documented at
<https://www.lua.org/manual/5.4/>, and the annotated default configuration
installed in the _etc_ directory is the worked example for everything on this
page.

Five points catch most of a configuration translated by hand from the old
format:

* Entries in a table are separated by commas -- `key = value,`. A missing comma
  is a syntax error naming the line, so it is a loud mistake rather than a
  quiet one.

* Strings are quoted, and there are no barewords: `spill = "drag"`, not
  `spill = drag`. An unquoted word is a variable read, and rather than let it
  be `nil` the sandbox refuses an undefined one by name and line.

* A key that is not a plain Lua identifier goes in brackets and quotes:
  `["blank-timeout-ac"]` because of the hyphen, `["end"]` because *end* is a
  Lua keyword, `["L+Return"]` because of the punctuation. A bare
  `blank-timeout-ac = 180` is read as a subtraction.

* `["0"]` and `[0]` are different keys -- the first is the string, the second
  the number. Layout registers and sheet-like names are strings, so write
  `["0"]`.

* Booleans are `true` and `false` only. `yes`, `no`, `on` and `off` are not
  booleans in Lua, and a key given one of them is reported as a type error
  naming the key.

Comments start with `--` and run to the end of the line.

Sandbox
-------
The configuration is executed with a restricted set of globals, built as a
whitelist rather than a blacklist so that a future Lua release cannot widen it
by accident. Available are `assert`, `error`, `ipairs`, `next`, `pairs`,
`print`, `rawequal`, `rawget`, `rawlen`, `rawset`, `select`, `tonumber`,
`tostring`, `type` and `_VERSION`, together with the `string`, `table` and
`math` libraries.

Absent, deliberately: `io`, `os` and `package`/`require`, so that a
configuration may compute values but may not run commands; `load`, `loadfile`
and `dofile`, which live in the base library rather than in `io` and are what
would actually reach the filesystem; `pcall` and `xpcall`, so that a
configuration cannot catch the resource errors described below and spin on them
forever; `setmetatable` and `getmetatable`, which would let a configuration
attach a `__gc` finalizer and run it outside those same limits; and `_G`,
`collectgarbage` and `warn`, which have no use here.

Four names are added to that set:

* `env` -- the environment. See *Environment Variables* below.

* `include(path [, optional])` -- the successor to the old format's include
  macro. It loads another configuration file and *returns* its value for the
  caller to splice in, rather than merging it in place. A relative *path*
  resolves against the directory of the file that calls it, so an include is
  independent of the compositor's working directory. A second argument of
  `true` turns an unreadable file into `nil` instead of an error; a file that
  is present but malformed is still fatal, because the author plainly meant to
  load it. Includes may nest 8 deep, and a file that includes itself is
  refused by name.

* `CONFIG_FILE` -- the path of the configuration file being read.

* `CONFIG_DIR` -- the directory holding it, which is the base an `include`
  resolves a relative path against.

Resource limits stop a mistake in the configuration from hanging the
compositor at startup, where there is no desktop yet to report it on. A load
may allocate at most 64 MiB and may execute at most 10,000,000 virtual-machine
instructions. Three operations do their work inside a single C call, where the
instruction count cannot reach, and so carry ceilings of their own: `string.rep`
is capped at 1048576 repeats, the subject string handed to `string.find`,
`string.match`, `string.gmatch` and `string.gsub` at 65536 bytes, and
`table.move` at 1048576 elements. Each of these is orders of magnitude above
anything written by hand -- a rule built with `string.rep("-", 80)` uses 80 of
the million it is allowed -- and exceeding one fails the load with a message
naming the limit that was hit.

Duplicate keys
--------------
A key written twice in the same table is neither merged nor rejected. This is
Lua: a table constructor evaluates its entries in order and the **last**
assignment wins, so `a = "first", a = "second"` yields *second* and the earlier
value is gone before **hikari** is handed the table at all. Two *palette*
tables written inside the same *ui* table are not merged into one either -- the
second simply replaces the first.

Both halves of the old rule have inverted. The old format kept the **first**
value and **hikari** printed a *configuration warning:* line naming the value
it was ignoring. Under Lua the surviving value is the last one, and -- this is
the part that matters -- **there is no warning, and there cannot be one**. Lua
collapses the pair while it builds the table, so the duplicate never reaches
any code of the compositor's that could notice it. Nothing is printed on
standard error, nothing appears in the startup output, and **reload** is just
as silent.

That makes a duplicate key strictly more dangerous than it was. Before, a
configuration that quietly carried one said so on every start; now it looks
completely healthy and simply does the wrong thing. This applies to every
section, not only bindings. If something you have configured appears to have no
effect, check first that you have not written the same key twice in the same
table -- nothing else is going to tell you.

A key written *wrong*, as opposed to twice, is a different matter and is loud.
An unknown section, and an unknown key inside *ui*, *layout*, *outputs*,
*inputs*, *views*, *colorscheme*, *bar*, *lock*, *animation* or *blur*, fails
the load outright and names the offender on standard error. A failed load at
startup stops the compositor; a failed load on **reload** leaves the running
configuration in place. Those messages go to standard error too, which a
session started from a display manager may discard -- see **HIKARI\_LOG** under
**ENVIRONMENT** for how to capture it.

Environment Variables
---------------------

A Lua string is literal, and **hikari** performs no substitution inside one.
`"$HOME"`, `"${HOME}"` and `"$$HOME"` all reach the compositor as exactly those
characters. The old format's `$VARIABLE` and `${VARIABLE}` expansion is gone,
and with it the `$$` escape, which now has nothing left to escape -- a `$` in a
string is simply a `$`. This is what makes a string destined for a shell easy
to write: an action's command is run through `/bin/sh -c`, so `$`, `$(` and
`${}` in it reach the shell exactly as typed, with no doubling.

The environment is read through a global table named `env` instead. `env.HOME`
is the value of **HOME**, and a variable that is not set reads as `nil` rather
than as the literal text of the reference -- so a default can be written with
Lua's `or`, which the old spelling had no way to express:

```
terminal = env.TERMINAL or "foot",
```

The table is read-only, and reaches **getenv**(3) on each access rather than
copying the environment, so a name is looked up when it is used and a
configuration cannot set a variable through it.

One edge the `or` idiom does not cover: a variable that is set but **empty**.
An empty string is true in Lua, so `or` does not fire and the value becomes
`""` -- for the line above, the keypress would then run nothing at all. Unset
the variable rather than blanking it, or write

```
terminal = (env.TERMINAL ~= "" and env.TERMINAL) or "foot",
```

which covers both cases.

`env` reads the **compositor's own** environment, which was fixed at the moment
the compositor was executed. The **reload** action re-reads the configuration
file inside that same process, so it sees the same environment it saw at
startup. A variable exported in a shell after **hikari** has started can
therefore never be picked up, and no reload will rescue it. Set the variable
before starting **hikari**, or write the value into the configuration file and
reload that.

ACTIONS
=======

General actions
---------------
* **lock**

  Lock **hikari**. To unlock you need to enter your password and press enter.
  Being able to unlock requires _hikari-unlocker_ to be installed at the
  compile-time prefix and running with setuid(2) root privileges (those are
  required to check if the entered password is correct). _hikari-unlocker_ also
  needs pam.conf(5) to be aware of its existence, therefore there must be a
  _hikari-unlocker_ service file in _pam.d_.

  The lock screen shows a blurred still of the workspace as it was at the moment
  of locking, with a clock and date drawn over it. Both are drawn by the
  compositor and need no client running. The outputs are powered down after a
  period of inactivity rather than immediately; see *blank-timeout-ac* and
  *blank-timeout-battery* in the *ui* section's *lock* subsection, which also
  controls the blur and the clock.

  Views marked as **public** are displayed above the backdrop, which allows
  applications to provide extra information while the computer is locked. Every
  other view is hidden — the whole desktop layer is switched off, so no window,
  bar or layer-shell surface can appear on the lock screen.

* **quit**

  Issues a quit operation to all views, allowing them to prompt their shutdown
  dialog if they have any. Issuing this operation again during shutdown will
  terminate **hikari** right away.

* **reload**

  Reload and apply the configuration.

Group actions
-------------
* **group-cycle-[next|prev]**

  Cycle to the next or previous group according to the stacking order by cycling
  through the top most view of each group. The *next* view is further up the
  stack and the *previous* view is further down the stack. Reaching each end of
  the stack just wraps around. Once a view is selected it will be raised to the
  top of the stacking order. Selecting happens by releasing the modifier key.

* **group-cycle-view-[next|prev|first|last]**

  Cycle through all visible views inside of a group. Once a view is selected it
  will be raised to the top of the stacking order. Selecting happens by
  releasing the modifier key.

* **group-hide**

  Hides all visible views of the group of the focused view.

* **group-lower**

  Lowers all visible views of the group of the focused view.

* **group-only**

  Hides all visible views not belonging to the group of the focused view.

* **group-raise**

  Raises all visible views of the group of the focused view.

Layout actions
--------------
* **layout-apply-[a-z]**

  Applies the layout in the according register to the current **workspace
  sheet**. If the register happens to be empty this is a no-op. If the view that
  currently has focus can be tiled and is not borrowed it will get raised to the
  top of the stack.

* **layout-cycle-view-[next|prev|first|last]**

  Cycle to the next or previous group according to the layout order. Once a view
  is selected it will be raised to the top of the stacking order, the layout
  order will remain unchanged.

* **layout-exchange-view-[next|prev|main]**

  Swaps the focused view with the next, previous or main view in the layout
  order.

* **layout-reset**

  Resets the geometry of all views in the current layout.

* **layout-restack-[append|prepend]**

  Adds non-floating sheet views to an existing layout without changing layout
  order of already tiled views. If no layout is present the default layout for
  the current sheet is applied.

Mark actions
------------
* **mark-show-[a-z]**

  Shows the view bound to the according mark. If no view is bound to the mark an
  optional command for this mark can be executed, if none is specified this
  action is a no-op.

* **mark-switch-to-[a-z]**

  Switches to the workspace containing the view bound to the according mark. If
  no view is bound to the mark an optional command for this mark can be
  executed, if none is specified this action is a no-op.

Mode actions
------------
* **mode-enter-group-assign**

  Entering _group-assign-mode_ allows the user to change the group of the
  currently focused view. Groups that do no exist yet get created. Groups that
  become empty get destroyed.

* **mode-enter-input-grab**

  Redirect all input events directly to the focused view without the compositor
  interfering. Focus will not leave this view anymore until the mode exits or
  the view closes. To exit this mode, reissue the same key binding that started
  this mode.

* **mode-enter-layout**

  Layout selection awaits one of the layout registers to be selected. Valid
  registers range from **a** to **z** and **0** to **9**. *ESC* cancels this
  mode without selecting a layout. If the layout register happens to be empty
  this action is a no-op. If the view that currently has focus can be tiled and
  is not borrowed it will get raised to the top of the stack.

* **mode-enter-mark-assign**

  To change the mark of a view enter mark assign mode and select a mark between
  **a** and **z**. This mode turns the mark indicator bar into an input field.
  The selection is finalized by pressing *Enter* or canceled by pressing *ESC*.
  If a mark has already been taken the conflicting window will be indicated.

* **mode-enter-mark-select**

  Mark selection allows to bring forward a view bound to a mark by selecting
  that mark. When entering this mode **hikari** awaits the name of the mark to
  be issued. If that mark is bound to a view that view is shown, in the case
  that this view is not a member of the **current sheet** it is considered
  **borrowed**. If no view is bound to this mark and the mark has been
  configured to execute a command when empty, this command gets executed.

* **mode-enter-mark-switch-select**

  This action works just like **mode-enter-mark-select** with the exception that
  is switches to the workspace of the bound view. If the mark is not bound it
  stays on the same workspace.

* **mode-enter-move**

  Moving around views with a pointer device is what this mode is for. Once
  entered the pointer will jump to the top left corner of the focused view and
  start moving the view around with the pointer. When releasing any key this
  mode is canceled automatically.

* **mode-enter-resize**

  Resizing around views with a pointer device is what this mode is for. Once
  entered the pointer will join to the bottom right corner of the focused view
  and start resizing the view with the pointer. When releasing any key this
  mode is canceled automatically.

* **mode-enter-sheet-assign**

  Entering this mode lets the user change the sheet of a view by pressing the
  number of the target sheet. If multiple outputs are available they can be
  cycled using *TAB*.

Sheet actions
-------------
* **sheet-show-all**

  Clears the current workspace and populates it with all views that are a member
  of its current sheet. This includes **invisible** views as well.

* **sheet-show-group**

  Clears the current workspace and populates it with all views that are a member
  of its current sheet and the group of the focused view. This includes
  **invisible** views as well.

* **sheet-show-invisible**

  Clears the current workspace and populates it with all **invisible** views
  that are a member of its current sheet.

View actions
------------
* **view-cycle-[next|prev]**

  Cycle through all visible views. The *next* view is further up the stack and
  the *previous* view is further down the stack. Reaching each end of the stack
  just wraps around. Once a view is selected it will be raised to the top of the
  stacking order. Selecting happens by releasing the modifier key.

* **view-decrease-size-[up|down|left|right]**

  Decreases the size of the focused view by the amount of pixels set as **step**
  value into the given direction

* **view-hide**

  Hides the focused view.

* **view-increase-size-[up|down|left|right]**

  Increases the size of the focused view by the amount of pixels set as **step**
  value into the given direction

* **view-lower**

  Lowers the focused view to the bottom of the stacking order.

* **view-move-[up|down|left|right]**

  Moves the focused view **step** pixels into the given direction. A move that
  carries the view's top left corner onto another output moves the view to that
  output -- see **OUTPUTS**.

* **view-move-[center[|-left|-right]|[bottom|top]-[left|middle|right]]**

  Moves the focused view to the given position on the output.

* **view-only**

  Hides every other view except the focused one.

* **view-pin-to-sheet-[0-9|alternate|current]**

  Pins the focused view to a given sheet. If the sheet is not currently a
  **current sheet** or sheet **0** the view becomes hidden. Pinning a view to
  the **current sheet** makes sense for **borrowed views** which takes this view
  from its original view and pin it to the current one.

* **view-quit**

  Closes the focused view.

* **view-raise**

  Raises the view to the top of the stacking order.

* **view-reset-geometry**

  Resetting view geometry brings a view back to its original size and position.
  This means that maximization will be undone and the view will also be taken
  out of a layout if it has been a part of one before.

* **view-snap-[up|down|left|right]**

  Snap the focused view into the specified direction. Views can snap to the edge
  of the screen as well as to the borders of neighboring views (in this case the
  **gap** setting is respected).

* **view-toggle-floating**

  Toggles the floating state of the focused view. Floating views can not be
  part of a layout. If a view that is already tiled is set to floating state it
  will be taken out of the layout and reset its geometry.

* **view-toggle-invisible**

  Toggles the invisible state of the focused view. A view in invisible state is
  not displayed if a user switches to the sheet containing this view. They need
  to be shown explicitly, either by using marks or by issuing actions showing
  views in this state. Iconified views can not be part of a layout. If a view
  that is already tiled is set to invisible state it will be taken out of the
  layout and reset its geometry.

* **view-toggle-maximize-[full|horizontal|vertical]**

  Maximizes the focused view in the given direction. Maximization state
  complement each other so maximizing a view horizontally and then vertically
  adds up to a full maximization state and so on.

* **view-toggle-public**

  Toggles the public state of the focused view. Public views are also displayed
  on the lock screen (note: they do not accept any input when the screen is
  locked though). These views should only contain information that should be
  displayed when the screen is locked, such as the progress of a long running
  process, they should never contain sensitive information. The public
  state is indicated in the sheet indicator bar via **!**.

  A clock is no longer a reason to mark a view public — **hikari** draws its own
  on the lock screen; see the *lock* subsection of the *ui* section.

VT actions
----------
* **vt-switch-to-[1-9]**

  Switches to another virtual terminal.

Workspace actions
-----------------
* **workspace-clear**

  Clears the current workspace.

* **workspace-cycle-[next|prev]**

  Cycle through available workspaces selecting the view that had focus last. If
  that view is no longer visible the first view of the **current sheet** of that
  workspace is selected . In both cases the cursor gets centered on that view.
  If the **current sheet** is empty this moves the cursor into the center of the
  target workspace.

* **workspace-show-all**

  Clears the current workspace and populates it with all views. This includes
  **invisible** views.

* **workspace-show-group**

  Raises the focused view, clears the current workspace and populates it with
  all views that are a member of the group of the focused view. This includes
  **invisible** views.

* **workspace-show-invisible**

  Clears the current workspace and populates it with all **invisible** views
  that belong to that workspace.

* **workspace-switch-to-sheet-[0-9|alternate|current]**

  Clears the current workspace and populates it with all views that are a member
  of the specified sheet. This action also sets the **current sheet** of the
  workspace to this very sheet. Views that are a member of sheet **0** will also
  be displayed on the bottom of the stacking order. Switching to the current
  sheet will reset the state of the sheet e.g. hiding borrowed views, showing
  views that have previously been hidden and hiding views that are in invisible
  state.

* **workspace-switch-to-sheet-[next|prev]-inhabited**

  Switch to the next or previous sheet (excluding **00**) that contains at least
  one member. If none exists is a no-op. This action also sets the **current
  sheet** of the workspace to this sheet.

USER DEFINED ACTIONS
====================

Actions can also be user defined, this is done in the *actions* section of the
configuration file. A user defined action consists of a name and a command that
should be run when the action has been issued.

To define an action *action-terminal* that launches sakura(1) one needs to
defined the following.

```
terminal = "sakura",
```

You can also define actions for laptop media controls, such as volume and
brightness:

```
["vol-up"]      = "mixer vol +5",
["vol-down"]    = "mixer vol -5",
["bright-up"]   = "backlight incr 5",
["bright-down"] = "backlight decr 5",
```

Note the spelling of the brightness commands. backlight(8) takes the relative
keyword as its OWN argument -- its usage gives `incr|+ value` and `decr|-
value`, and it compares the whole argument, so `backlight + 5` works and
`backlight +5` does not. The latter does not fail, which is the trap: it falls
through to the absolute form and sets the brightness TO 5 percent, leaving the
screen almost dark. *incr* and *decr* are used here because they cannot be
misread that way. These are FreeBSD commands; on Linux the usual equivalent is
brightnessctl, and the shipped configuration notes it.

Now we can bind the newly defined *action-terminal* to a key combination in the
*bindings* section.

BINDINGS
========

Actions can be bound to keys and mouse buttons. The *bindings* section in the
configuration file is used for this matter. Keys can be specified by using
either key symbols or codes. A key combination starts with a string identifying
the modifiers for the bindings. There are 5 valid modifiers. A valid modifier
string is a combination of the following modifiers.

* **L** (Logo)
* **S** (Shift)
* **C** (Control)
* **A** (Alt)
* **5** (AltGR)

If we want to omit the modifier for a key binding we signal this by using "0"
instead of a modifier string.

Following the modifier string a key symbol or code can be stated. If we are
using a key symbol to identify a key combination we are using "+" followed by
the symbol in the case of a key code we are using "-" followed by the numerical
key code. Key symbols and codes can be determined using wev(1).

Once a key combination has been identified it can be bound to an action.

```
["LS+a"]  = "action1",   -- symbol binding
["LS-38"] = "action2",   -- code binding
```

For laptop media keys, you can bind `XF86` keysyms directly without a modifier
by using `0`:

```
["0+XF86AudioRaiseVolume"] = "action-vol-up",
["0+XF86AudioLowerVolume"] = "action-vol-down",
["0+XF86MonBrightnessUp"]  = "action-bright-up",
```

The *bindings* section can contain 2 subsections *keyboard* and *mouse* for
keyboard and mouse bindings.

Valid values for mouse button names are *right*, *middle*, *left*, *side*,
*extra*, *forward*, *back* and *task*.

Bindings can have a dedicated *end* action that gets triggered whenever a key is
released or additional keys are pressed. It ensures that a *begin* action
definitely is followed by the *end* action.

```
["L+t"] = {
  begin   = "action-push-to-talk-start",
  ["end"] = "action-push-to-talk-stop",
},
```

MARK CONFIGURATION
==================

Marks can be used to quickly navigate to views. They can also execute commands
when they are not currently bound to a view. This functionality can be used to
start an application that can then take over that mark using auto configuration.
Note that the started application does not automatically take over the mark.

To specify commands that are issued on unassigned marks one can specify the
commands associated with the mark in the *marks* section in the configuration
file.

```
marks = {
  s = "sakura",
},
```

VIEW CONFIGURATION
==================

When an application start its views can be automatically configured by
**hikari**. Each view has a property called *id*, in the *views* section this
can be used to specify certain properties you want for that view to apply.

* **floating**

  Takes a boolean to specify the view's **floating** state on startup. The
  default value is *false*.

* **focus**

  Takes a boolean to specify if the view should automatically grab focus when it
  appears for the first time. This is useful for views that appear at a
  specified position. The default value is *false*.

* **group**

  Automatically assign this view to a group (if the group does not exist it is
  created automatically). If no group is specified a group name equal to the
  view *id* is chosen.

* **inherit**

  Lets the user specify a list of properties which should be inherited to child
  views (e.g. dialogs). To inherit a property just state the name of the
  property as a string. Additionally use an object to overwrite specific values
  if they should differ from the parent's configuration. Values that are not
  explicitly inherited resort to their default. If **inherit** is not specified
  the child view is going to use the parent's configuration.

  Note the spelling of one entry: the inheritable name for **public** is
  *publicview*. A string in this list that names no property is ignored without
  a diagnostic, unlike an unknown key elsewhere in a *views* entry, so the list
  is worth proof-reading.

* **invisible**

  Takes a boolean to specify the view's **invisible** state on startup. The
  default value is *false*.

* **mark**

  Assign a mark to the view. This only takes effect if that mark is not already
  bound to another view already.

* **position**

  Positions a newly created view to the given coordinates. **hikari** allows two
  ways to define a view position. One way is to specify absolute position
  stating the **x** and **y** coordinates as a object, the other one is by
  stating them as one of the following options:

  * *bottom-left*
  * *bottom-middle*
  * *bottom-right*
  * *center*
  * *center-left*
  * *center-right*
  * *top-left*
  * *top-middle*
  * *top-right*

  This allows positioning a view relative to the output.

* **public**

  Takes a boolean to specify the view's **public** state on startup. The default
  value is *false*.

* **sheet**

  Takes an integer that references the sheet this view should be assigned to. If
  the **current sheet** is unequal to this sheet or **0** this view
  automatically is considered to be **borrowed**.

To configure views of the **systat** *id* to become a member of the group
*monitor* and automatically assign them to sheet **0** with a given position and
focus grabbing we would do something like this. Child views inherit the
**group** and **sheet** property while overwriting **floating** to *true*, all
the other properties are set to their respective default values.

```
["systat"] = {
  group    = "monitor",
  sheet    = 0,
  position = {
    x = 1429,
    y = 1077,
  },
  focus    = true,

  inherit  = { "group", "sheet", { floating = true } },
},
```

LAYOUTS
=======

**hikari** is not a tiling compositor so naturally some things will behave a bit
differently compared to traditional tiling approaches. First and foremost,
**hikari** tries to minimize operations that will affect a lot of views at the
same time e.g. opening a new view will not automatically insert a view into an
existing layout and resize all of the already tiled views. To insert a view into
an existing layout the user has to issue a tiling action. This way opening a new
view does not scramble an existing layout and the user can actively decide when
to incorporate a view into a layout.

That is the default, and it is what the *layout* section described under
**LAYOUT POLICY** below can turn off. Setting *auto* makes a sheet incorporate
new views and close the gap left by a closed one without being asked, which is
how a conventional tiling window manager behaves. The manual tiling actions are
unchanged and remain available either way.

A layout is bound to a sheet, each sheet can have at most one layout and laying
out a sheet will incorporate all of its views unless they are **invisible** or
**floating**. Resetting a layout will reset the geometry of all of the laid out
views to its original geometry (also resetting maximization).

Configuring layouts happens in the _layouts_ section in the configuration file.
Layouts are assigned to layout registers from **a** to **z** and special layout
registers **0** to **9** which correspond to default layouts for a respective
sheet. A layout itself is a combination of splits and containers with tiling
algorithms.

Splits are used to subdivide regions of space and containers are used to consume
views and layout them according to a specific tiling algorithm.

Splits
------
A layout subdivides the screen space using splits. Dividing up the screen space
uses a binary space partitioning approach. One can split a region of space
horizontally or vertically into to new regions which can contain either another
split or a container with a tiling algorithm.

To split up the screen vertically into two equally sized section one has to
specify when the *left* and *right* hand side of the split should contain.

```
{
  left  = ...,
  right = ...,
}
```

Respectively to split horizontally you have to specify *top* and *bottom*.

The side of the split that is laid out FIRST is the part that gets filled first
when tiling a sheet, and it becomes the dominant part of the split.

Which side that is used to be decided by the order in which the two keys were
written. That no longer works, and this is the third rule the move to Lua
inverted: a Lua table constructor has no order a reader can recover, so there
is nothing for **hikari** to inspect. The orientation is written down instead,
with an *orientation* key whose value is one of *left*, *right*, *top* or
*bottom*:

```
  m = {
    orientation = "right",
    scale = 0.6,
    left  = "full",
    right = "stack",
  },
```

Omitting it gives *left* for a *left*/*right* split and *top* for a
*top*/*bottom* one -- which is exactly what writing *left* before *right*, or
*top* before *bottom*, used to mean. Every split in the shipped configuration
is written that way, so none of them needs the key. Only a configuration that
relied on writing *right* before *left*, or *bottom* before *top*, has to say
so now; one that does not, and is simply translated key for key, keeps the
layout it had.

Sometimes splitting a region of space should not result in equally sized
subdivisions and the dominant part of the split should be scaled up or down.
This can be done by specifying the *scale* attribute which can vary between
**0.1** to **0.9**, if no *scale* is specified this value defaults to **0.5**.

To horizontally split a region on space where the top portion of the split
should take up 75% would be specified like so:

```
{
  scale  = 0.75,
  top    = ...,
  bottom = ...,
}
```

Additionally to setting a fixed *scale* value, **hikari** allows to specify a
*scale* object with *min* and *max* values. This is called dynamic scaling, and
it uses the size of the first view inside the container to determine the size of
the entire container. The *min* and *max* values are used to specify possible
minimum and maximum scale values for the container. Omitting the values for
*min* or *max* sets the former to **0.1** and the latter to **0.9**.

```
{
  scale = {
    min = 0.5,
    max = 0.75,
  },
  left  = "single",
  right = "stack",
}
```

Containers
----------
Containers consume a number of views and arrange them according to a tiling
algorithm. There are 6 different tiling algorithms that you can assign to a
container.

* **empty**

  This is one of the simplest algorithms, it does not consume any views. This is
  used if a user desired to have a container of a layout to remain empty e.g.
  preventing the layout to cover up a portion of the workspace.

* **single**

  Containers using the **single** layout only consume one view which takes up
  the entire container.

* **full**

  Each view inside of a container using this algorithm will take up the entire
  size of the container. All of the views are stacked up on top of each other.

* **stack**

  The **stack** algorithm tries to give every view the container consumes an
  equal amount of vertical space (excess space is given to the first view). The
  order in which stacking works is from top to bottom.

* **queue**

  The **queue** algorithm tries to give every view the container consumes an
  equal amount of horizontal space (excess space is given to the first view).
  The order in which stacking works is from left to right.

* **grid**

  A grid tries to give each view the containers consumes an equal amount of
  horizontal and vertical space (excess space is given to the first view, and
  therefore first row of the grid). If the amount of views can not be split up
  into equal rows and column the last part of the grid will not be filled.

The easiest way to define a layout is by simply stating the tiling algorithm.
Binding a fullscreen layout to the layout register **f** can be trivially
achieved.

```
f = "full",
```

This layout does not subdivide the screen using splits in any way. The container
takes up the entire screen space (respecting gap settings) and uses the **full**
algorithm to arrange the views.

More complex layouts might demand that the user specifies the number of views
that the container may contain up to a maximum. This can be achieved by
specifying a container object.

To define a **queue** container that contains up to 4 views one would define it
like that:

```
{
  views  = 4,
  layout = "queue",
}
```

Just stating the tiling algorithm is a short-hand for a layout object with where
*views* is set to 256.


LAYOUT POLICY
=============
The *layout* section -- singular, and distinct from *layouts*, which defines the
registers themselves -- decides **when** a layout is applied without the user
asking for it.

* **auto**

  When *true*, mapping a view offers it to the current sheet's layout and
  closing one re-tiles the survivors. When *false*, which is the built-in
  default and the historical behaviour, a layout only ever changes in response
  to a tiling action.

  Note that the two defaults differ on purpose. **hikari** itself defaults to
  *false*, so a configuration that says nothing about layout policy behaves as
  it always has; the *hikari.lua* shipped with the distribution sets *true*,
  because that is the configuration the compositor is developed and tested
  against. If you started from the shipped file, automatic tiling is on and this
  is the line that turns it off.

  Re-tiling is deferred rather than immediate. A view that has a resize in
  flight cannot be laid out -- it is not *tileable* until the client answers the
  configure -- so a re-tile performed at map time would arrange every view
  except the one that triggered it. The request is instead held until the sheet
  is quiet. Requests for a sheet that is not currently displayed are dropped and
  re-offered when it is displayed again, and requests made while the screen is
  locked are dropped outright.

  Requests are also held for as long as a view is being dragged with the
  pointer, and released when the button is let go. Dragging a view onto another
  output changes which sheet it belongs to, and acting on that immediately would
  re-tile the destination -- and the view still under the pointer with it --
  while the drag was still in progress. Holding the request means a view dragged
  between outputs is folded into the destination layout once, where it was
  dropped. Nothing is lost by the delay: the destination still incorporates the
  view according to **insert**, and the sheet it left still closes the gap
  according to **reflow-on-close**.

* **insert**

  Either *append* or *prepend*, defaulting to *append*. Decides whether an
  automatically incorporated view is placed at the end of the layout order or
  becomes the main view. Views already in the layout keep their relative order
  in both cases; these are the same semantics as **layout-restack-append** and
  **layout-restack-prepend**, which the automatic path dispatches to.

* **reflow-on-close**

  Defaults to *true*. Kept separate from *auto* because incorporating a new view
  only adds to a layout, whereas closing one moves every remaining view.

Layouts are applied to an output's **usable area** rather than to the output
itself, and that area is what is left after the top bar and after every panel
that reserves space -- see **PANELS AND DOCKS**. A change to the reserved space
is treated exactly like an output being moved or changing mode: with **auto**
set the sheet re-tiles itself into the new box, and without it the views keep
the geometry they had until a tiling action is issued.

* **default-register**

  A single layout register, *a* to *z* or *0* to *9*, adopted by a sheet that
  has no layout yet. Unset by default, in which case the per-sheet default
  registers *1* to *9* apply as usual -- and where those are undefined too the
  sheet simply stays stacking. A register that names no layout falls back the
  same way rather than failing the configuration.

```
layout = {
  auto                = false,
  insert              = "append",
  ["reflow-on-close"] = true,
  -- ["default-register"] = "g",
},
```

UI CONFIGURATION
================
Getting everything to look right is an important aspect of feeling "at home".
**hikari** offers a couple of options to tweak visuals to the users content. All
of these configuration options are part of the *ui* section.

* **border**

  Defines the thickness of view borders in pixels.

Standard border thickness is set to **1**.

```
border = 1,
```

* **gap**

  A gap is some extra space that is left between views when using a layout or
  snapping views. The value also specifies a pixel value.

The standard **gap** value is 5.

```
gap = 5,
```

* **spill**

  Whether a view may be painted outside the output it belongs to. Every output
  carries its own workspace and each workspace displays its own sheet, so a view
  overhanging an output edge is drawn over an output showing a different set of
  views, and the position constraint permits that overhang down to the last few
  pixels of the view's own output.

  Either *drag*, *always* or *never*, defaulting to *drag*.

  With *drag* a view may overhang while it is being moved or resized, which is
  how you see that a drag is about to cross to the other output, and is cropped
  to its own output once it is released. A **floating** view is exempt and is
  never cropped -- it was positioned by hand, and cropping it would overrule the
  position the user chose. *always* disables cropping entirely and is the
  behaviour of every release before this one. *never* crops every view at all
  times, mid-drag and floating views included.

  Cropping is visual only. It changes where a view is painted and never where it
  is, so no view is moved, resized or reported differently to its client.

```
spill = "drag",
```

* **font**

  Specifies the font that is used for indicator bars.

**hikari** uses *monospace 10* as its default font setting.

```
font = "monospace 10",
```

* **step**

  The step value defines how many pixels move and resize operations should
  cover.

The standard **step** value is 100.

```
step = 100,
```

Animation
---------
The *animation* section smooths views **moving**: re-tiling when a view opens or
closes, **view-snap-\***, **view-move-\***, and applying a layout.

Position only. A view's size is not the compositor's to interpolate -- a resize
is a protocol round trip, so there is no sequence of intermediate sizes to draw,
only the client's old buffer and then its new one. Interactive move and resize
are never animated either, since lag behind the pointer is precisely what a drag
must not have, and neither is a view's first placement, which would otherwise
travel from the corner of the output.

* **enabled**

  Defaults to *false*.

* **duration**

  Milliseconds for a view to travel, *0* to *1000*, defaulting to *120*. *0* is
  the same as disabling animation.

* **easing**

  One of *linear*, *ease-out* or *ease-in-out*, defaulting to *ease-out*.

```
animation = {
  enabled  = false,
  duration = 120,
  easing   = "ease-out",
},
```

Lock screen
-----------
The *lock* section controls what the screen locker draws and how long it stays
lit. Everything here is drawn by **hikari** itself, so it is present with no
session running, survives a client crash, and cannot be impersonated by a window
that merely looks like a clock.

* **blur**

  Blurs the workspace as it looked at the moment the screen was locked, and
  shows that behind the lock screen. Accepts either a boolean or an object, so
  that disabling it and tuning it use the same key.

  *radius* is in pixels; *passes* is how many box-blur passes approximate a
  Gaussian. Three is the standard approximation and is rarely worth changing.

```
blur = false,

-- Or, instead of disabling it, a table tuning the strength. Write one form
-- or the other -- a key written twice keeps the last one, silently.
-- blur = { radius = 12, passes = 3 },
```

* **clock**

  Whether to draw the clock and date at all. Both are **strftime**(3) format
  strings. Setting *date-format* to an empty string draws the time alone.

```
clock            = true,
["clock-format"] = "%H:%M",
["date-format"]  = "%A, %e %B",
["clock-font"]   = "sans 72",
["date-font"]    = "sans 20",
```

* **clock-color**

  The color of the clock and date. Accepts the same forms as any colorscheme
  entry, including the quoted *"#RRGGBBAA"* string with alpha.

  The text is drawn with a soft shadow behind it, because it sits over a blurred
  photograph of the user's own desktop whose brightness is not known in advance.
  White therefore stays legible over a light wallpaper without any configuration.

```
["clock-color"] = "#FFFFFF",
```

* **blank-timeout-ac**, **blank-timeout-battery**

  Seconds of inactivity before the outputs are powered down while locked. The
  two are separate because the useful values differ by an order of magnitude: on
  battery the screen should die quickly, while on mains a visible clock is
  rather the point.

  A value of **0** disables blanking entirely. The power source is read each
  time the timer is armed rather than once at lock time, so unplugging the mains
  while the screen is locked takes effect on the next keystroke.

```
["blank-timeout-ac"]      = 180,
["blank-timeout-battery"] = 60,
```

Views marked **public** are still displayed on the lock screen as before, and
are drawn above the blurred backdrop. See **view-toggle-public**.

### bar

The *bar* subsection of *ui* governs how the top bar handles blocks whose text
is longer than the space available. In practice that is the media block, whose
content is whatever the currently playing track happens to be called and is
therefore unbounded.

An over-long block is cut to *max-block-chars* and then scrolled as a banner, so
the whole title remains readable over time without any one block painting across
the clock or the status icons.

These keys are read by the compositor rather than by the **hikari-topbar**
helper, so changes take effect on a configuration reload and do not require the
session to be restarted.

* **max-block-chars**

  Longest a block may be, in **characters**, before it begins to scroll.
  Counted in characters rather than bytes, so accented, CJK and emoji titles are
  cut where one would expect rather than part way through a character.

  A value of **0** disables capping and scrolling. Blocks are still prevented
  from painting outside their own section of the bar; that guarantee is
  structural and does not depend on this setting.

  Accepts **0**-**256**.

* **scroll-interval**

  Milliseconds per character of scroll. The timer that drives it is only running
  while some block is actually over the limit, so a bar with nothing playing
  costs nothing.

  Accepts **50**-**10000**.

* **scroll-separator**

  Placed between the end of the text and its beginning as the banner wraps
  around, so that it reads as a continuous loop rather than snapping back.

```
bar = {
  ["max-block-chars"]  = 26,
  ["scroll-interval"]  = 300,
  ["scroll-separator"] = "   •   ",
},
```

Palette
-------
The *palette* section defines sixteen positional colors, *color0* to *color15*,
in the conventional terminal order -- *0* to *7* normal, *8* to *15* bright.

The palette carries no meaning of its own. Every color **hikari** actually draws
with is one of the semantic slots of the *colorscheme* section below, and the
palette exists so those slots can be written as references into one place rather
than as sixteen literals spread through the file. It is also the palette that is
handed to the **hikari-topbar** helper, which is what lets the compositor and
the bar share one theme.

Every key is optional; an unmentioned slot keeps its default. A palette entry
must be a literal -- one entry cannot refer to another, because that would make
the meaning of the file depend on the order its keys happen to appear in. The
palette is resolved before anything that can refer to it, so it may be placed
above or below *colorscheme*.

```
palette = {
  color0  = "#2b1e3a",
  color1  = "#c96464",
  color2  = "#df9f87",
  color3  = "#e4b382",
  color4  = "#8e7cc3",
  color5  = "#b18fc7",
  color6  = "#9fa0a6",
  color7  = "#d4d4d9",
  color8  = "#5e5966",
  color9  = "#df8787",
  color10 = "#f2bda8",
  color11 = "#f5cf9e",
  color12 = "#aba0d9",
  color13 = "#cfaedc",
  color14 = "#b8b9be",
  color15 = "#f0edf2",
},
```

Note that a comment runs to the end of its line, so palette entries must be
written one per line if they carry trailing comments.

Almost every colour reaches the screen through one of the semantic slots below,
rather than by being referred to positionally. The exception is the top bar's
battery block, which selects from the palette by charge level:

| state | palette entry |
|---|---|
| plugged in or charging | *color6* |
| 75-100% | *color4* |
| 60-75% | *color5* |
| 50-60% | *color11* |
| 35-50% | *color3* |
| 20-35% | *color2* |
| 10-20% | *color9* |
| 0-10% | *color1* |

Those bands are not separately configurable: they follow the palette, so
retheming the desktop retints the battery with it.

"Plugged in" is read from the AC line itself rather than being deduced from
what the battery is doing: *hw.acpi.acline* on FreeBSD, and the *online*
attribute of a *Mains* supply under */sys/class/power\_supply* on Linux. Either
way it is the same source the lock screen consults to choose its blanking
timeout. On a machine that offers neither, a desktop or a VM, it falls back to
what the battery itself reports -- the ACPI charge flags on FreeBSD, the
supply's *status* on Linux -- and accepts only the states that positively mean
external power, so a critically flat battery is never painted as though it were
on mains.

Note that **hikari-topbar** is spawned once at startup, so a palette change
reaches the bar on the next start of the compositor rather than on a reload.

Colorschemes
------------
**hikari** uses color to indicate different states of views and their indicator
bars. By specifying a *colorscheme* section the user can control these colors. A
colorscheme is a number of properties that can be changed individually. Colors
are specified as a reference into the *palette* above, as a hexadecimal RGB
value (e.g. 0xE6DB74), or as a quoted string in either *"#RRGGBB"* or
*"#RRGGBBAA"* form. The string form is the only way to specify an alpha channel:

```
active = "color15",       -- palette reference

-- The same slot written the other three ways. Write one of the four, not
-- all of them -- a key written twice keeps the last one, silently.
-- active = 0xFFFFFF,     -- integer, always fully opaque
-- active = "#FFFFFF",    -- identical, written explicitly
-- active = "#FFFFFF80",  -- 50% alpha
```

A palette reference cannot carry alpha, since the palette stores opaque colors;
write the literal string form on the one or two slots that need it.

Alpha deliberately cannot be written as an integer. *0x0080FFCC* and *0x80FFCC*
parse to values that cannot be told apart, so guessing 8-versus-6 digits would
silently misread any color whose red channel is zero. The quoted form makes the
digit count explicit, and every existing integer setting keeps its exact meaning.

* **active**

  Indicates view focus.

* **background**

  Specifies the background color. This will be obscured by a wallpaper

* **bar**

  Background color of the native top bar. Independent of **background**, so the
  bar can be tinted or made translucent without altering the desktop behind
  every window. Defaults to *"#2b1e3ae6"* -- *color0* at ~90% opacity, written
  as a literal because a palette reference cannot carry alpha.

* **conflict**

  Conflicts can happen when the user attempts to overwrite something (e.g.
  binding a mark to a view that is already taken up by another view) or does
  something illegal (e.g. defining a new group with a leading digit in its
  name).

* **first**

  Signals that the indicated view is the topmost view of a group.

* **foreground**

  Font color for indicator bars.

* **grouped**

  Views that belong to the same group are indicated using this color.

* **inactive**

  Indicates that a view does not have focus.

* **insert**

  Indicates indicator bars that are in insert mode (e.g. assigning a view to a
  group) or views that have an input grab using *mode-enter-input-grab*.

* **selected**

  This color is used to indicate that a view is selected.

These are the default settings for the **hikari** colorscheme. Every slot is
derived from the default palette rather than carrying a literal of its own, so
overriding the palette alone yields a coherent scheme.

```
colorscheme = {
  background = "color0",
  foreground = "color0",
  active     = "color15",
  inactive   = "color8",
  selected   = "color12",
  first      = "color4",
  grouped    = "color5",
  insert     = "color13",
  conflict   = "color9",
  bar        = "#2b1e3ae6",
},
```

INPUTS
======

The *inputs* section is used to configure input devices. Device names can be
determined using libinput(1).

Pointers
--------
Pointers can be configured in the *pointers* subsection. The following options
are available.

* **accel**

  Sets mouse acceleration for the pointer device to a value between **-1** and
  **1**.

* **accel-profile**

  Sets mouse acceleration profile for the pointer device to the given mode.
  Valid values are *none*, *flat* and *adaptive*.

* **disable-while-typing**

  Enable or disable *disable-while-typing* if available. This setting expects a
  boolean value.

* **middle-emulation**

  Enable or disable middle click emulation if available. This setting expects a
  boolean value.

* **natural-scrolling**

  Enable or disable *natural-scrolling* if available. This setting expects a
  boolean value.

* **scroll-button**

  Configures the pointer scroll button. Valid values are *right*, *middle*,
  *left*, *side*, *extra*, *forward*, *back* and *task*.

* **scroll-method**

  Sets the pointers scroll method. Valid values are *no-scroll*,
  *on-button-down*.

* **tap**

  Enable or disable *tap* if available. This setting expects a boolean value.

* **tap-drag**

  Enable or disable *tap-drag* if available. This setting expects a boolean
  value.

* **tap-drag-lock**

  Enable or disable *tap-drag-lock* if available. This setting expects a boolean
  value.

Configuring the *System mouse* input device could look like this.

```
inputs = {
  pointers = {
    ["System mouse"] = {
      accel             = 1.0,
      ["scroll-method"] = "on-button-down",
      ["scroll-button"] = "middle",
    },
  },
},
```

A special name "\*" is used to address all pointers. Values defined for this
pseudo pointer override unconfigured values for any other pointer.

Keyboards
---------

`hikari` is using `xkb` to configure its keyboards via the *keyboards* section.
`xkb` rules can be set independently or via a file using the *xkb* attribute.

To specify rules individually one can use the following options. Refer to
xkeyboard-config(7) for possible settings.

* **rules**

  Specifies the `xkb` rules. The default value is `evdev`.

* **model**

  Sets the keyboard model.

* **layout**

  Sets the keyboard layout.

* **variant**

  Sets the keyboard variant.

* **options**

  Sets keyboard options.

Additionally **hikari** can also configure key repeat using the following
attributes.

* **repeat-delay**

  Takes a positive integer to specify the key repeat delay in milliseconds. The
  default value is 600.

* **repeat-rate**

  Takes a positive integer to specify the key repeat rate in Hz. The default
  value is 25.

Configuring the *AT keyboard* input device could look like this.

```
inputs = {
  keyboards = {
    ["*"] = {
      xkb = {
        layout  = "de(nodeadkeys)",
        options = "caps:escape",
      },
      ["repeat-rate"]  = 25,
      ["repeat-delay"] = 600,
    },
  },
},
```

A special name "\*" is used to address all keyboards. Values defined for this
pseudo keyboard override unconfigured values for any other pointer.

Keyboards can also be configured using *XKB* environment variables. `hikari`
will automatically fall back to these settings if a keyboard is not explicitly
configured.

* **XKB\_DEFAULT\_LAYOUT**
* **XKB\_DEFAULT\_MODEL**
* **XKB\_DEFAULT\_OPTIONS**
* **XKB\_DEFAULT\_RULES**

To specify a layout set **XKB\_DEFAULT\_LAYOUT** to the desired layout. This
needs to happen before starting **hikari**.

```
XKB_DEFAULT_LAYOUT "de(nodeadkeys),de"
```

Switches
--------
Switches can be configured in the *switches* subsection. A switch just takes an
action and functions like a regular key binding using the name of the switch as
an identifier. The *begin* action is triggered when turning the switch on and
*end* is triggered when turning it off.

```
inputs = {
  switches = {
    ["Control Method Lid Switch"] = "lock",
  },
},
```

Gestures
--------
Trackpad gestures reported via *wlr\_pointer\_gestures\_v1* can be bound in the
*gestures* subsection, same as switches. A binding key has the form
*swipe-\<direction\>-\<fingers\>*, *pinch-\<direction\>-\<fingers\>*, or
*hold-\<fingers\>*, where *direction* is one of *up*, *down*, *left*, *right*
for swipe or *in*, *out* for pinch, and *fingers* is the number of touchpoints
the gesture requires.

A gesture that matches a configured binding triggers that action instead of
being forwarded to the focused client; any gesture without a matching binding
is delivered to the client. Update events are buffered until the gesture ends
(in case it turns out to match a binding); a gesture with more than 128 update
events has the excess silently dropped from what is delivered to the client.

```
inputs = {
  gestures = {
    ["swipe-left-3"]  = "workspace-cycle-next",
    ["swipe-right-3"] = "workspace-cycle-prev",
    ["pinch-in-3"]    = "view-toggle-maximize-full",
    ["hold-3"]        = "action-terminal",
  },
},
```

Touch
-----
Touchscreen input is supported natively: a touch device is attached to the
cursor's output layout like a pointer, and touch events are forwarded to
clients via the standard Wayland touch protocol. The first touch point of a
new touch sequence additionally drives the same focus, raise, move, and
resize behavior as a left mouse click, so windows can be managed by tapping
and dragging on a touchscreen without a mouse. Further simultaneous touch
points are left untouched for the client to interpret (e.g. pinch-to-zoom in
a PDF viewer).

OUTPUTS
=======

The *outputs* section allows users to define the background and position for a
output using its name. A special name "\*" is used to address all outputs.
Values defined for this pseudo output override unconfigured values for any other
output.

Backgrounds are configured via the *background* attribute which can be either
the path to the background image, or an object which enables the user to define
additional attributes for the background image. Background file format has to be
*PNG*.

When defining a *background* object the following attributes are available.

* **path**

  This attribute giving the *path* to the wallpaper image file is mandatory.

* **fit**

  Specifies how the wallpaper should be displayed. Available options are
  *center*, *stretch* and *tile*. *stretch* is the default even when specifying
  the background image as a string.

Configuring output *eDP-1* and *WL-1* could look like this.

```
outputs = {
  ["eDP-1"] = {
    background = "/path/to/wallpaper",
  },

  ["WL-1"] = {
    background = {
      path = "/path/to/wallpaper",
      fit  = "center",
    },
  },
},
```

Runtime display configuration
-----------------------------

hikari implements *wlr-output-management-unstable-v1*, so position, resolution,
refresh rate, scale and transform can be changed while the session is running by
any client speaking it -- **wlr-randr**(1), **kanshi**(1), **wdisplays**(1).

Whether such a client may change anything is decided by a single top level
configuration key.

* **output\_management\_overrides\_config**

  A boolean, *true* by default.

  *true* lets output management clients lead. Their configurations are applied,
  and reloading the configuration no longer moves an output back to the
  *position* set in the *outputs* section. That section still seeds an output
  when it first appears, so a configured position is applied on plug-in either
  way; what changes is whether the configuration keeps asserting it afterwards.

  *false* gives the lead to the configuration file. Clients may still read the
  current configuration -- listing outputs and their supported modes still works
  -- but every apply and test is refused with the reason logged, and a reload
  re-applies the configured position.

  This is one setting covering every output rather than one per output. The
  protocol answers a whole configuration with a single success or failure and
  has no way to refuse one head out of several, while clients customarily submit
  every head on every request, so refusing on behalf of a single output would
  fail requests that did not concern it.

Switching an output off evacuates its views to another output, drops its scene
output and removes it from the output layout, so nothing is arranged against a
screen that is not being painted. Switching it back on restores it at the
position it held; the views stay where they were moved, as they do when a
monitor is unplugged and reconnected. The lock screen's blank timer is a
separate thing and does not disturb this -- unlocking relights only the outputs
that were part of the desktop to begin with.

The *outputs* section itself carries wallpaper and position only, so persisting
a mode or a scale across restarts is a job for **kanshi**(1), which speaks this
same protocol.

Moving views between outputs
----------------------------

Every output carries its own workspace, and each workspace displays its own
sheet. A view therefore belongs to exactly one output at a time, and moving it to
another output also moves it to the sheet that output is currently displaying.

Which output a view belongs to is decided by its **top left corner**. This is the
rule for both ways of moving a view, so that they agree. **view-move-\*** moves
the corner by **step** pixels and hands the view over when the corner lands on
another output. A pointer drag hands it over when the corner crosses, not when
the pointer does -- the view is held wherever it was taken hold of, so the
pointer is usually some distance inside it, and handing over on the pointer would
move a view to an output it was not yet on.

What happens on arrival depends on the view. A **floating** view keeps the
position it was dropped at and is never folded into a layout. A **tiled** view
joins the destination sheet's layout according to **insert**, and the sheet it
left closes the gap according to **reflow-on-close**; with pointer drags both
happen once, when the button is released. See **LAYOUT POLICY**.

Two outputs of different heights placed side by side leave a band of the layout
that belongs to no output at all -- a 1920x1200 panel beside a 1920x1080 one
leaves everything below 1080 on the right-hand side unclaimed. The pointer can
enter that band. A drag holds the view at its last position while it is there and
resumes when the pointer is over an output again, rather than moving the view
somewhere that cannot be painted.

**view-move-\*** treats the band differently, and this is the one case where the
two ways of moving a view part company. A corner that lands there has landed on
no output, so there is no output to hand the view to and the move is performed on
the output the view is already on. The practical consequence is that a view low
enough on the taller screen to be beside the band cannot be moved onto the
shorter screen with the keyboard: every move in that direction stops it at the
edge of its own screen instead. Move it up first, or drag it. The position
constraint keeps the corner on the view's own output throughout, so a view is
never left with its corner in the band.

Output position can be given explicitly using the *position* attribute. If none
is given during startup **hikari** will automatically configure the output.

```
["eDP-1"] = {
  position = {
    x = 1680,
    y = 0,
  },
},

["HDMI-A-1"] = {
  position = {
    x = 0,
    y = 0,
  },
},
```

PANELS AND DOCKS
================

Panels, docks, notification daemons and wallpaper setters are ordinary clients
speaking *wlr-layer-shell* (**saber**, **waybar**, **mako**, **swaybg**). The
protocol is compiled in with *WITH\_LAYERSHELL*, which is on by default. There
is nothing to configure for them in *hikari.lua*: a layer-shell client asks for
its position, its size and its share of the screen over the protocol, and the
compositor honours the request.

A layer surface names the output it wants and is placed there; one that names
none is placed on the output holding the focused workspace. Each of the
protocol's four layers is a separate part of the scene graph, and views sit
between them. The full order, back to front, is:

```text
background -- bottom -- views -- top -- fullscreen -- overlay -- lock
```

*background* and *bottom* are painted below every window; *top* and *overlay*
above them. The band worth knowing about is **fullscreen**, which sits between
the two upper layers: a view entering fullscreen is promoted into it, so it
covers a panel on *top* but **not** a surface on *overlay*. A client picks its
own layer, so that choice is what decides whether a fullscreen window covers it
-- **saber**, for instance, puts its panel on *top* and its dash and window
spread on *overlay*, and is covered on one and not the other. X11
override-redirect surfaces (menus, tooltips and dropdowns from XWayland
clients) are promoted into the same band, above the fullscreen views within it,
so a menu belonging to a fullscreen window is not buried under it.

Nothing from any layer is shown over the lock screen. Entering lock mode
disables *bottom*, *views*, *top*, *fullscreen* and *overlay*, and disabling a
scene node disables everything inside it, so a surface mapped after the lock
began is invisible for the same reason. The two bands left enabled are
*background*, so the wallpaper still shows behind the lock screen, and *lock*
itself, which is what the lock screen is drawn on.

The usable area
---------------

Each output keeps a **usable area**: the box left over once the space other
things have reserved is taken out of it. It is derived, in order, as the full
output, minus the top bar's strip, minus the *exclusive zone* of every layer
surface mapped on that output.

That box is what **hikari** lays views out in. Layouts are applied to it, named
view positions (*center*, *top-right*, ...) are resolved against it, and
**workspace-cycle-\[next|prev\]** parks the pointer in its centre when the
workspace it arrives at has nothing to focus -- so a tiled window is never
placed under a panel, and a centred window is centred in what is actually
visible.

A surface with a zero exclusive zone -- what an overlay launcher such as
**sofi** uses, and what a wallpaper setter uses -- reserves nothing and draws
over the layout on purpose.

Re-tiling when the reserved space changes
-----------------------------------------

A panel that reserves space -- one with a nonzero exclusive zone -- changes that
output's usable area when it starts, exits, grows, hides on autohide or moves to
another output. The sheet displayed there is then laid out against a box that no
longer exists, so the compositor requests a re-tile for it, in exactly the same
way it does when an output is moved or changes mode. A surface that reserves
nothing leaves the usable area alone and triggers none of this, whatever it
does.

The request goes through the ordinary reflow path described under **LAYOUT
POLICY**, which means it is subject to the same rules: it happens only when
*layout* *auto* is set, it is deferred until the sheet is quiet, it is held for
the duration of a pointer drag, and it is dropped while the screen is locked.
With *auto* unset nothing is moved on its own -- tiled views keep the geometry
they had, which may now be underneath the panel, until **layout-restack-append**
or a layout action is issued.

Floating views are never re-tiled, by definition, so a floating view can end up
beneath a panel that appeared after it.

Keyboard focus
--------------

Focus follows the mouse, but a layer surface only takes the keyboard if it asks
for it. A panel that requests *keyboard\_interactivity none* -- the normal
setting for a bar or a dock -- is hovered and clicked without the keyboard or
the active workspace moving anywhere, so pointing at a panel on a second monitor
does not redirect the keys or the view-cycling actions to that monitor. A
surface that requests the keyboard does move both, which is what a panel's own
menu wants.

*keyboard\_interactivity on\_demand* is currently treated as *exclusive* rather
than as click-to-focus: such a surface is given the keyboard while the pointer
is over it. This affects panels that open keyboard-driven menus and nothing
else.

TOP BAR
=======

**hikari** draws a bar across the top of every output in its own scene graph.
The bar's *content* comes from **hikari-topbar**, a separate unprivileged
binary the compositor spawns at startup and reads a swaybar-protocol JSON
stream from over a pipe.

The helper is a separate process deliberately. Its sensors are sampled with
blocking **popen**(3) calls several times a second, and running those inside the
compositor would stall the Wayland event loop on every tick. A slow or wedged
sensor therefore costs the bar its freshness and nothing else. The stream is
display-only; the bar handles no click events.

The bar's height is derived from *ui.font*, so it scales with the configured
font rather than a fixed pixel count, and the usable area of each output shrinks
by that height. Its colours come from *ui.palette*, which the compositor passes
to the helper on its command line -- so a palette change reaches the bar on the
next compositor start rather than on a configuration reload.

Blocks
------
Sampling runs on two cadences: the battery every 200ms, since it is cheap and
fast-changing, and everything else once a second.

| Block | Source | Shown when |
| ----- | ------ | ---------- |
| CPU usage | *kern.cp\_time* | always |
| RAM usage | *hw.physmem*, *vm.stats.vm.\** | always |
| CPU temperature | *dev.cpu.0.temperature*, falling back to *hw.acpi.thermal.tz0.temperature* | a sensor is readable |
| GPU usage, VRAM and temperature | **nvidia-smi** | an NVIDIA GPU is detected at startup |
| *$HOME* partition usage | **getfsstat**(2) / **statfs**(2) | always |
| Media / now playing | **playerctl** | always; reads *Idle* with nothing playing |
| Network | **getifaddrs**(3) | always; *WIFI*, *ETHERNET* or *DISCONNECTED* |
| Backlight | **backlight**(8) | the tool reports a level |
| Volume | **pactl** | a default sink is readable |
| Battery | *hw.acpi.battery.\**, *hw.acpi.acline* | a battery is present |
| Clock and date | **strftime**(3) | always |

The sysctls and the **backlight**(8) pipeline in that table are the FreeBSD
sources. Five of the sensors are written once per platform and the build links
exactly one of the pair, so on Linux the same five blocks read *sysfs* and
*procfs* directly instead -- no fork, no shell, and no tool to install:

| Block | FreeBSD | Linux |
| ----- | ------- | ----- |
| CPU usage | *kern.cp\_time* | */proc/stat* |
| RAM usage | *hw.physmem*, *vm.stats.vm.\** | */proc/meminfo* |
| CPU temperature | *dev.cpu.0.temperature*, then *hw.acpi.thermal.tz0.temperature* | */sys/class/hwmon*, then */sys/class/thermal* |
| Backlight | **backlight**(8) | */sys/class/backlight* |
| Battery | *hw.acpi.battery.\**, *hw.acpi.acline* | */sys/class/power\_supply* |
| GPU (AMD) | not available | */sys/class/drm/cardN/device* |

The remaining blocks are platform-neutral and are the same code on both: the
partition, media, network, volume and clock blocks all use the interfaces named
in the first table, and so do the GPU blocks when an NVIDIA card answers. The sentinels, units and ranges of the
five split sensors were taken from the FreeBSD implementation one function at a
time, so a block that is omitted on one platform for want of a reading is
omitted on the other for the same reason.

A block whose source cannot be read is omitted entirely rather than drawn empty,
so a desktop with no battery and integrated graphics simply has a shorter bar.

The battery block picks its colour from the charge level rather than from a
*colorscheme* slot, referring to specific *palette* entries directly. Those
bands are not separately configurable -- they follow whatever is in the palette,
so retheming the desktop retints the battery with it. See the annotated default
configuration for the exact mapping.

Runtime dependencies
--------------------
None of these are required to build or start **hikari**; each affects only the
block that uses it.

* A **Nerd Font**, installed and selected through *ui.font*. The blocks are
  labelled with private-use-area glyphs, so without one the bar renders boxes.

* **playerctl** for the media block and **pactl** (PulseAudio or
  PipeWire-pulse) for the volume block.

* **nvidia-smi** for the GPU blocks on an NVIDIA machine, probed once at
  startup. An AMD card on Linux needs no tool at all: *gpu\_busy\_percent*,
  *mem\_info\_vram\_used*, *mem\_info\_vram\_total* and a hwmon temperature are
  read from */sys/class/drm/cardN/device* directly, and all four must be
  readable or the blocks are omitted, since a partial GPU reading is worse than
  none. On a machine with several cards, the lowest-numbered one that answers
  completely is used -- the card number says nothing about which GPU is
  discrete, so the rule is chosen for stability rather than for guessing. When
  an NVIDIA card and an AMD card are both present the NVIDIA reading wins,
  because the bar has one GPU slot and changing which card an existing machine
  reports would be a regression for its owner.

  Intel is not supported, and that is a limitation of the interfaces rather
  than an omission. Neither i915 nor xe exposes a utilisation figure in sysfs;
  engine busyness is available only through their perf PMUs, which are
  system-wide and therefore need *CAP\_PERFMON* on a kernel with the default
  *perf\_event\_paranoid* of 2. Integrated graphics also has no VRAM pool to
  report. Rather than show a different quantity under the same label, the
  blocks are omitted, which is what they already do when no GPU answers.

* **backlight**(8) for the backlight block, on FreeBSD only -- it is part of
  the FreeBSD base system, so there is nothing to install. The Linux build
  needs no tool for that block at all; it reads */sys/class/backlight* itself.

How over-long blocks are handled is configured in the *bar* subsection of *ui*,
described under **UI CONFIGURATION**. Those keys are read by the compositor
rather than by the helper, so unlike the palette they do take effect on a
configuration reload.

Palette fallback
----------------
Started by hand with no palette argument, **hikari-topbar** falls back to reading
*~/.cache/wal/colors* (pywal), and to white if that is absent too. A malformed
argument is rejected outright rather than half-applied, so a typo yields the
pywal fallback rather than a half-themed bar. The compositor always passes its
own palette, so this path is only reached when the helper is run standalone.

CONTROL SOCKET
==============

**hikari**'s sheet model -- ten sheets per workspace, permanently bound to their
output -- is not expressible in any standards-track Wayland protocol.
*wlr-foreign-toplevel-management* has no notion of a workspace, and
*ext-workspace-v1*'s *assign* moves a workspace to an output group rather than a
window to a workspace. An external sheet switcher therefore has nothing to read
and nothing to call.

The control socket is that missing surface, and is deliberately kept as small as
it can be. It is not a general scripting interface and will not grow into one:
anything expressible as a Wayland protocol belongs in a Wayland protocol.

**sofi -show sheets** is its reference client.

The socket lives at *$XDG\_RUNTIME\_DIR/hikari.sock* with mode **0600**, and is
unlinked when the compositor exits. It is served from the compositor's own event
loop, so handlers run on the main thread. At most **8** clients may be connected
at once, and a request longer than **512** bytes is refused rather than
buffered.

The protocol is one request per connection: send a single line, read the
response, and the compositor closes the connection.

Requests
--------

* **state**

  Report the displayed sheet, its output, and how many views each of the ten
  sheets holds. The counts are what let a switcher dim empty sheets. Sheet
  **0** is reported on the same footing as the rest even though its views stay
  visible underneath whichever sheet is displayed; the caller is given the
  number and decides how to present that asymmetry.

  The response is four lines:

```
sheet <0-9>
output <name>
counts <c0> <c1> <c2> <c3> <c4> <c5> <c6> <c7> <c8> <c9>
END
```

* **sheet** *\<0-9\>*

  Switch the current workspace to the given sheet. Responds *ok*.

* **pin** *\<0-9\>*

  Move the focused view to the given sheet. This is the operation no Wayland
  protocol expresses, which is why it exists here. Responds *ok*.

Errors
------
Every failure is a single line beginning with *error*.

* **error compositor busy**

  The compositor is not in normal mode. This gate sits in front of the whole
  command table rather than in each handler, so it applies to **state** as
  well: the per-sheet view counts would otherwise report how many windows are
  open to anything that can reach the socket while the screen is locked. Since
  the lock screen is a mode, one test closes both the modal-abort hole and the
  locked-screen leak.

* **error unknown command**

  The verb is not one of the three above.

* **error sheet number must be 0-9**

  The argument was absent, out of range, or not exactly one decimal number.

* **error no active workspace**

  There is no workspace to act on, which happens transiently during output
  teardown.

* **error no focused view**

  **pin** was issued with nothing focused.

* **error view busy**

  **pin** was issued against a view with a resize still in flight. Queuing a
  second operation over one still awaiting the client's acknowledgement would
  lose the first and leave the view dirty, so the request is refused instead.

* **error response too long**

  The response did not fit its buffer. Not reachable with the current command
  set.

A connection refused with **ECONNREFUSED** means the socket file is stale --
left by a compositor that did not exit cleanly -- rather than that the
compositor is busy.

Failure to create the socket at startup is warned about and is not fatal:
losing it costs an external sheet switcher, not the session.

Example
-------

```
$ printf 'state\n' | nc -U "$XDG_RUNTIME_DIR/hikari.sock"
sheet 1
output eDP-1
counts 0 3 1 0 0 0 0 0 0 2
END

$ printf 'sheet 3\n' | nc -U "$XDG_RUNTIME_DIR/hikari.sock"
ok
```

FILES
=====

* *$XDG\_CONFIG\_HOME/hikari/hikari.lua*

  User configuration. When **XDG\_CONFIG\_HOME** is unset this is
  *~/.config/hikari/hikari.lua*.

* *${ETC\_PREFIX}/etc/hikari/hikari.lua*

  System-wide default configuration, used when no user configuration is
  readable. **ETC\_PREFIX** is fixed at compile time. On FreeBSD it tracks
  **PREFIX** and so defaults to */usr/local*, giving
  */usr/local/etc/hikari/hikari.lua*. On Linux it is empty by default, so this
  path is */etc/hikari/hikari.lua* -- Linux-PAM compiles */etc/pam.d* into the
  library rather than resolving it under a prefix, and the two must agree. A
  distribution packager can override it either way.

* *~/.config/hikari/autostart*

  Executed at startup if it is a regular file that is both readable and
  executable. Overridden for one run by **-a**.

* *${ETC\_PREFIX}/etc/pam.d/hikari-unlocker*

  PAM policy consulted by **hikari-unlocker** when checking credentials to
  unlock the screen.

* *${PREFIX}/share/wayland-sessions/hikari.desktop*

  Session entry read by display managers, shown as **Hikari**. Runs
  **start-hikari**, the session wrapper script.

* *${PREFIX}/share/wayland-sessions/hikari-sakura.desktop*

  Second session entry, shown as **Hikari Sakura**. Runs
  **dbus-run-session hikari** -- the compositor binary, with only the session
  message bus placed in front of it. Both entries declare
  *DesktopNames=Sakura*, so the desktop reports the same name whichever is
  chosen.

* *${PREFIX}/share/xdg-desktop-portal/sakura-portals.conf*

  Portal backend map, naming *wlr* for **Screenshot** and **ScreenCast** and
  falling back to *gtk* for every other interface. Required: without it,
  backend selection falls back to matching **XDG\_CURRENT\_DESKTOP** against the
  *UseIn=* field of **xdg-desktop-portal-wlr**'s portal file, which lists no
  Sakura name -- so screen sharing silently finds no provider. Its filename is
  derived from **XDG\_CURRENT\_DESKTOP**, lower-cased, and so is coupled to the
  *DesktopNames* of both session entries. Requires xdg-desktop-portal 1.17 or
  newer, where *portals.conf* was introduced.

* *${PREFIX}/share/backgrounds/hikari/hikari\_wallpaper.png*

  Default wallpaper, which the default configuration's *outputs.background*
  points at.

* *$XDG\_RUNTIME\_DIR/hikari.sock*

  Control socket, mode **0600**. See **CONTROL SOCKET**.

ENVIRONMENT
===========

* **XDG\_RUNTIME\_DIR**

  Directory for the Wayland socket and the control socket. Must be an existing
  directory owned by the invoking user with mode **0700**. **start-hikari**
  creates one at */tmp/hikari-runtime-$UID* when the system provides none, and
  validates it either way.

* **XDG\_CONFIG\_HOME**

  Root of the configuration directory. Falls back to *$HOME/.config*.

* **TERMINAL**

  Terminal emulator spawned by the default configuration's *terminal* action
  and bound to its *s* mark, as `env.TERMINAL or "foot"` -- so **foot** is
  used when it is unset. Read when the configuration file is read, not when
  the action fires; see *Environment Variables* under **CONFIGURATION**.

* **HIKARI\_LOG**

  When set to a writable file path, **start-hikari** redirects its own
  descriptors there before **exec**, capturing everything **hikari** and wlroots
  write to standard error -- including the configuration errors described under
  **CONFIGURATION**, which a session started from a display manager would
  otherwise discard. Note that a duplicate key is *not* among them: Lua
  collapses it before the compositor sees it, so no amount of logging will ever
  surface one. The redirect is a real **exec** rather than a pipe, so the
  compositor's exit status and signal disposition are preserved. Output no
  longer echoes to the terminal while logging.

* **XKB\_DEFAULT\_LAYOUT**, **XKB\_DEFAULT\_MODEL**, **XKB\_DEFAULT\_OPTIONS**,
  **XKB\_DEFAULT\_RULES**

  Fallback keyboard configuration for keyboards with no entry in the *keyboards*
  section. Read once at startup; see **INPUTS**.

* **XDG\_CURRENT\_DESKTOP**, **XDG\_SESSION\_DESKTOP**

  This desktop's name, *Sakura*. A single name rather than a colon-separated
  list, because everything that reports the running desktop reads the variable
  verbatim.

  Set in two places, so that both session entries agree, and the precedence
  differs between them by design.

  The compositor sets these two and **XDG\_SESSION\_TYPE** itself, but only when
  they are not already set, so on the wrapperless entry a display manager's own
  value -- taken from the chosen entry's *DesktopNames* -- is left alone, and
  the session is still named correctly when nothing set it at all.

  **start-hikari** exports them unconditionally, so on that entry the wrapper's
  value wins over whatever the display manager set. That is deliberate: the
  script exists to establish a known-good environment, and every session entry
  that runs it declares this same desktop, so there is no value it could be
  overriding that was meant to survive.

  The name once carried a *:wlroots* suffix, because the portal backend was
  selected by matching this variable against the *UseIn=* field of
  **xdg-desktop-portal-wlr**'s portal file. That coupling is gone:
  *sakura-portals.conf* names the backends directly. The two are a matched
  pair -- dropping the suffix without installing that file leaves screen
  sharing with no provider and nothing logged to say why. See **FILES**.

* **DBUS\_SESSION\_BUS\_ADDRESS**

  When unset, **start-hikari** wraps the compositor in **dbus-run-session**.

Any environment variable may additionally be read from the configuration file
through the global `env` table -- `env.TERMINAL`, and so on. There is no
substitution inside a string value; see *Environment Variables* under
**CONFIGURATION**.

EXIT STATUS
===========

**hikari** exits **0** after the **quit** action, and on **-h** or **-v**.

It exits non-zero when no readable configuration file could be resolved -- from
**-c**, from the user configuration, or from the compiled-in default, in that
order -- reporting *could not load configuration*; when an unrecognised option
was given, in which case the usage message is printed; and when it is started as
root or with inconsistent privileges, which is refused outright. Note that only
uid 0 counts as privileged: gid 0 (*wheel*) is an ordinary primary group for a
normal user and is not rejected.

SEE ALSO
========

**sofi**(1), cwm(1), libinput(1), xkeyboard-config(7), strftime(3),
backlight(8), mixer(8), devd(8), acpi(4)

* Hikari Sakura -- <https://github.com/orpheus497/hikari-sakura>
* sakura, the display manager -- <https://github.com/orpheus497/sakura>
* sofi, the shell -- <https://github.com/orpheus497/sofi>
* saber, the panel -- <https://github.com/orpheus497/saber>
