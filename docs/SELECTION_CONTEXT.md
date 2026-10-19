# Selection and explicit modeling commands (v0.3.3)

Selection is passive. There is no Faces/Parts dropdown.

| Input | Selection |
| --- | --- |
| Left click | One Face |
| Ctrl + left click | Add/remove Faces |
| Alt + left click | One Part |
| Ctrl + Alt + left click | Add/remove Parts |

Releasing Alt does not change the current selection or its type. Structure
Browser rows select Parts. Selecting a different type starts a fresh typed set.
Selection alone never activates a modeling function or displays manipulators.
Middle-button orbit, Shift+middle pan and wheel zoom retain their behavior.

## Context menus

Ordinary right click on selected geometry opens the menu for its existing type;
Alt is unnecessary. It does not convert Part owners to Faces or clear selection.
With no selection, right click can select a Face and open its menu. Background
and unrelated-object right clicks do not replace an existing selection.

Part menus provide Move Part, Rotate Part, Duplicate, supported Boolean
operations, Hide and Color. Hidden Parts can still be shown through the Structure
Browser. Face menus contain only supported operations: Move Face for accepted
planar B-Reps and Primitive Pull / Resize for supported primitive/Box faces.
Unsupported Face functions and future Rotate/Align Face placeholders are absent.
An unsupported Face with no available operations has no empty context menu.

## Explicit activation and complete termination

Choose a context action or Modeling menu command to activate its manipulators.
Move Face and Resize use orange arrows. Move Part uses local XYZ axes; Rotate
Part uses local rings and the existing volume centroid pivot. The old combined
Move/Rotate gizmo is removed: one command has one manipulator set.

OK/Enter confirms and closes a session. Cancel/Esc restores its original state
and closes it. All arrows, rings, highlights, previews and parameter controls
then disappear, and the tool indicators reset. Selection can remain for another
explicit command; clicking again never restarts the last function. Choosing
another command cancels an unfinished preview before activating the new one.
Duplicate, creation, modal numeric operations and Undo/Redo also clear old tools.
Invalid parameters leave the explicit editor available for correction or
Cancel/Esc, rather than installing invalid geometry. An unsupported Move Face
selection reports an error and clears the pending tool.

Boolean has its own explicit operand-selection phase inside the same lifecycle:
Target → Tools → Keep Tools → Preview → OK/Cancel. Tool picking through the
viewport or browser keeps that session alive. Releasing a Tool click does not
open a numeric transform editor. After termination, normal modifier-based
Face/Part selection resumes. See [Boolean Operations](BOOLEAN_OPERATIONS.md).

## Architecture and verification

`SelectionContext` resolves actual OCCT owners and stable part slots. Menus only
dispatch to existing modeling operations. `resetModelingTools()` is the shared
cleanup path used by session termination, explicit tool changes and one-shot
operations; it does not clear the retained selection. `CommandSession` carries
preview/rollback state and commits to the existing chronological history.

`selection-context` covers passive Face/Part/Ctrl selection, Alt release, ordinary
right-click and default Face picking, explicit activation, exclusive gizmos,
OK/Cancel, tool changes, Pull rollback/history, Boolean Tool capture and complete
termination, Duplicate cleanup and unsupported geometry actions. Geometry,
pivots, world transforms, numeric input and Undo/Redo remain covered by the
existing Pull, rotation, command-session, Move Face and Boolean suites.

For Fedora KDE, select Faces/Parts without invoking a command and verify no
arrows appear. Start each context function explicitly, test OK/Enter and
Cancel/Esc, then click more geometry and verify the old function stays inactive.
Repeat with groups, transformed primitives and Boolean results. Test Boolean
Tools through the browser, repeated previews, Keep Tools, navigation, Undo/Redo
and keyboard focus in the floating editor. Manual retest is pending.
