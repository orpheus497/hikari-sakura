NAME
====
**Hikari Sakura** - FreeBSD Wayland Compositor

SYNTAX
======
**hikari** [-vh] [-a \<executable\>] [-c \<config\>]

DESCRIPTION
===========

**Hikari Sakura** is a FreeBSD-focused revamp and modernization of the original Hikari (originally by antaz, since abandoned upstream). It is explicitly designed as a comprehensive, focused Wayland desktop environment for FreeBSD.

It is a stacking Wayland compositor with additional tiling capabilities,
it is heavily inspired by the Calm Window manager (cwm(1)). Its core concepts
are views, workspaces, sheets and groups.

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

**hikari** is configured using libucl(3) as a configuration file format. The
configuration is located under _$XDG_CONFIG_HOME/hikari/hikari.conf_. If this
file is not found **hikari** is going to try _hikari.conf_ from the install
_etc_ directory.

The default configuration is going to use **$TERMINAL** as your standard
terminal application.

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

Duplicate keys
--------------
libucl neither merges a key that is written twice nor rejects it -- it keeps
both values, and **hikari** reads only the first. The second therefore has no
effect at all, which is easy to do by accident when two parts of a configuration
both claim the same binding.

Rather than fail the whole configuration over it -- which would stop a desktop
that had quietly carried a duplicate for months from starting -- **hikari**
reports it on standard error and carries on:

```
configuration warning: "L+n" is set 2 times in bindings.keyboard -- hikari uses the first and ignores the rest
    in effect: action-notifications
    ignored:   workspace-switch-to-sheet-next-inhabited
```

This applies to every section, not only bindings. If something configured
appears to have no effect, check the startup output first. Note that warnings go
to standard error, which a session started from a display manager may discard --
see `HIKARI_LOG` in **start-hikari** to capture it.

Environment Variables
---------------------

**hikari** supports the use of environment variables in its string values.
Occurrences of `${VARIABLE}` or `$VARIABLE` inside a string will be substituted
with the value of an environment variable named `VARIABLE`. If no such
environment variable exists, no substituation takes place.

You can use double dollar signs to escape variables: `$${VARIABLE}` will result
in `${VARIABLE}`, `$$VARIABLE` will result in `$VARIABLE`.

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

  Moves the focused view **step** pixels into the given direction.

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
terminal = sakura
```

You can also define actions for laptop media controls, such as volume and brightness:

```
vol-up = "mixer vol +5"
vol-down = "mixer vol -5"
bright-up = "backlight +5"
```

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
"LS+a" = action1 # symbol binding
"LS-38" = action2 # code binding
```

For laptop media keys, you can bind `XF86` keysyms directly without a modifier
by using `0`:

```
"0+XF86AudioRaiseVolume" = action-vol-up
"0+XF86AudioLowerVolume" = action-vol-down
"0+XF86MonBrightnessUp" = action-bright-up
```

The *bindings* section can contain 2 subsections *keyboard* and *mouse* for
keyboard and mouse bindings.

Valid values for mouse button names are *right*, *middle*, *left*, *side*,
*extra*, *forward*, *back* and *task*.

Bindings can have a dedicated *end* action that gets triggered whenever a key is
released or additional keys are pressed. It ensures that a *begin* action
definitely is followed by the *end* action.

```
"L+t"  = {
  begin = action-push-to-talk-start
  end = action-push-to-talk-stop
}
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
marks {
  s = sakura
}
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
systat = {
  group = monitor
  sheet = 0
  position = {
    x = 1429
    y = 1077
  }
  focus = true

  inherit = [ group, sheet, { floating = true } ]
}
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
  left = ...
  right = ...
}
```

Respectively to split horizontally you have to specify *top* and *bottom*.

Notice that the order in which you specify *left*, *right*, *top* and *bottom*
is important, since it defined the orientation of the split. The side of the
split that gets specified first is the part the gets filled first when tiling a
sheet, it becomes the dominant part of the split.

Sometimes splitting a region of space should not result in equally sized
subdivisions and the dominant part of the split should be scaled up or down.
This can be done by specifying the *scale* attribute which can vary between
**0.1** to **0.9**, if no *scale* is specified this value defaults to **0.5**.

To horizontally split a region on space where the top portion of the split
should take up 75% would be specified like so:

```
{
  scale = 0.75
  top = ...
  bottom = ...
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
    min = 0.5
    max = 0.75
  }
  left = single
  right = stack
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
f = full
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
  views = 4
  layout = queue
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
  closing one re-tiles the survivors. When *false*, which is the default and the
  historical behaviour, a layout only ever changes in response to a tiling
  action.

  Re-tiling is deferred rather than immediate. A view that has a resize in
  flight cannot be laid out -- it is not *tileable* until the client answers the
  configure -- so a re-tile performed at map time would arrange every view
  except the one that triggered it. The request is instead held until the sheet
  is quiet. Requests for a sheet that is not currently displayed are dropped and
  re-offered when it is displayed again, and requests made while the screen is
  locked are dropped outright.

* **insert**

  Either *append* or *prepend*, defaulting to *append*. Decides whether an
  automatically incorporated view is placed at the end of the layout order or
  becomes the main view. Views already in the layout keep their relative order
  in both cases; these are the same semantics as **layout-restack-append** and
  **layout-restack-prepend**, which the automatic path dispatches to.

* **reflow-on-close**

  Defaults to *true*. Kept separate from *auto* because incorporating a new view
  only adds to a layout, whereas closing one moves every remaining view.

* **default-register**

  A single layout register, *a* to *z* or *0* to *9*, adopted by a sheet that
  has no layout yet. Unset by default, in which case the per-sheet default
  registers *1* to *9* apply as usual -- and where those are undefined too the
  sheet simply stays stacking. A register that names no layout falls back the
  same way rather than failing the configuration.

```
layout {
  auto             = false
  insert           = append
  reflow-on-close  = true
  # default-register = "g"
}
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
border = 1
```

* **gap**

  A gap is some extra space that is left between views when using a layout or
  snapping views. The value also specifies a pixel value.

The standard **gap** value is 5.

```
gap = 5
```

* **font**

  Specifies the font that is used for indicator bars.

**hikari** uses *monospace 10* as its default font setting.

```
font = "monospace 10"

```

* **step**

  The step value defines how many pixels move and resize operations should
  cover.

The standard **step** value is 100.

```
step = 100
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
animation {
  enabled  = false
  duration = 120
  easing   = ease-out
}
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
blur = false
blur = { radius = 12; passes = 3 }
```

* **clock**

  Whether to draw the clock and date at all. Both are **strftime**(3) format
  strings. Setting *date-format* to an empty string draws the time alone.

```
clock        = true
clock-format = "%H:%M"
date-format  = "%A, %e %B"
clock-font   = "sans 72"
date-font    = "sans 20"
```

* **clock-color**

  The color of the clock and date. Accepts the same forms as any colorscheme
  entry, including the quoted *"#RRGGBBAA"* string with alpha.

  The text is drawn with a soft shadow behind it, because it sits over a blurred
  photograph of the user's own desktop whose brightness is not known in advance.
  White therefore stays legible over a light wallpaper without any configuration.

```
clock-color = "#FFFFFF"
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
blank-timeout-ac      = 180
blank-timeout-battery = 60
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
bar {
  max-block-chars  = 26
  scroll-interval  = 300
  scroll-separator = "   •   "
}
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
palette {
  color0  = "#2b1e3a"
  color1  = "#c96464"
  color2  = "#df9f87"
  color3  = "#e4b382"
  color4  = "#8e7cc3"
  color5  = "#b18fc7"
  color6  = "#9fa0a6"
  color7  = "#d4d4d9"
  color8  = "#5e5966"
  color9  = "#df8787"
  color10 = "#f2bda8"
  color11 = "#f5cf9e"
  color12 = "#aba0d9"
  color13 = "#cfaedc"
  color14 = "#b8b9be"
  color15 = "#f0edf2"
}
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

"Plugged in" is read from *hw.acpi.acline*, which reports the AC line itself
rather than being deduced from what the battery is doing -- the same sysctl the
lock screen consults to choose its blanking timeout. On a machine that has no
such sysctl, a desktop or a VM, it falls back to the ACPI charge flags and
accepts only the states that positively mean external power, so a critically
flat battery is never painted as though it were on mains.

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
active = color15       # palette reference
active = 0xFFFFFF      # integer, always fully opaque
active = "#FFFFFF"     # identical, written explicitly
active = "#FFFFFF80"   # 50% alpha
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
colorscheme {
  background = color0
  foreground = color0
  active     = color15
  inactive   = color8
  selected   = color12
  first      = color4
  grouped    = color5
  insert     = color13
  conflict   = color9
  bar        = "#2b1e3ae6"
}
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
inputs {
  pointers {
    "System mouse" = {
      accel = 1.0
      scroll-method = on-button-down
      scroll-button = middle
    }
  }
}
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
inputs {
  keyboards {
    "*" = {
      xkb = {
        layout = "de(nodeadkeys)"
        options = "caps:escape"
      }
      repeat-rate = 25
      repeat-delay = 600
    }
  }
}
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
inputs {
  switches {
    "Control Method Lid Switch" = lock
  }
}
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
inputs {
  gestures {
    "swipe-left-3"  = workspace-cycle-next
    "swipe-right-3" = workspace-cycle-prev
    "pinch-in-3"    = view-toggle-maximize-full
    "hold-3"        = action-terminal
  }
}
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
outputs {
  "eDP-1" = {
    background = "/path/to/wallpaper"
  }

  WL-1 = {
    background = {
      path = "/path/to/wallpaper"
      fit = center
    }
  }
}
```

Output position can be given explicitly using the *position* attribute. If none
is given during startup **hikari** will automatically configure the output.

```
"eDP-1" = {
  position = {
    x = 1680
    y = 0
  }
}

"HDMI-A-1" = {
  position = {
    x = 0
    y = 0
  }
}
```
