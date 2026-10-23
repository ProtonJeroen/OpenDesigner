# Selection and explicit modeling commands (v0.3.3–v0.4.0)

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

## Selection after Undo/Redo (v0.3.6)

Every successful Undo/Redo deselects all Faces, Edges and Parts, including browser-
only selections of hidden Parts. The shared history path first cancels active
sessions and removes previews/manipulators, releases AIS selection/detection/pick
owners before topology replacement, and clears any selection made by restoration
adapters afterward. Structure Browser selection and its current item are cleared
through the existing selection signal. Geometry, properties, stable Part slots
and cached history snapshots keep their existing semantics.

This applies equally to the toolbar, Edit-menu and Ctrl+Z/Ctrl+Y actions, including
when an active command's floating parameter editor has keyboard focus. Empty-space
right click afterward opens Undo/Redo and Fit All. Selection is still
preserved on ordinary Cancel; history navigation deliberately requires a fresh
selection. Regression coverage exercises Move Face, Rotate Face and Boolean Undo
and Redo through all three entry points (with viewport and parameter-editor keyboard focus),
including pending previews, mixed AIS
subshape/Part owners, hidden browser selections, highlight cleanup and cached
geometry restoration.

## Context menus

Ordinary right click on selected geometry opens the menu for its existing type;
Alt is unnecessary. It does not convert Part owners to Faces or clear selection.
With no selection, right click can select a Face and open its menu. Background
and unrelated-object right clicks open the existing selection menu without
replacing or clearing that selection.

With **no selection**, right-clicking empty viewport space offers **Undo
(Ctrl+Z)**, **Redo (Ctrl+Y)** and **Fit All**. The history actions are the exact
same QAction objects used
by Edit and the toolbar, connected to the existing modeling history. Availability
updates after every committed edit, Undo and Redo; empty stacks disable the
corresponding action. Opening/dismissing the menu starts no modeling command.
With a selection, **Fit Selected** is available in the Face, Part and construction
menus, including when right-clicking empty space. Parts use their placed bounds;
Faces use only the selected located Faces; construction objects use their finite
display bounds. These actions change only the camera and retain selection,
Undo/Redo and the document dirty state. Fit All uses the existing View command.
Active sessions and camera orbit/pan suppress the
context menu. Middle-button camera behavior is unchanged.


Part menus provide Move Part, Rotate Part, Duplicate, supported Boolean
operations, Hide and Color. Hidden Parts can still be shown through the Structure
Browser. Face menus contain only supported operations: Move Face, Rotate Face
and Align Face for accepted planar analytic B-Reps, plus Pull / Resize for
recognized complete analytic primitives/boxes and cylindrical faces in a
GeneralBRep, including holes. Capabilities are derived from the current B-Rep;
creation history and primitive flags are not prerequisites. A requested offset
must validate as one closed solid before it can be committed. Rotate Face asks
for a straight target edge
or projected World X/Y/Z axis. Align Face asks for a separate planar reference
and offers Coplanar/Parallel. Unsupported Face functions are absent.
Fit Selected remains available even when a Face has no supported modeling operations.

**Delete Face** is available for nonempty Face selections, including multiple
Parts. It validates reconstruction after explicit activation and reports an
impossible or unsupported closure through the shared geometry error popup.
All selected regions must succeed together; see [Delete Face](DELETE_FACE.md).

## Explicit activation and complete termination

Choose a context action or Modeling menu command to activate its manipulators.
Move Face and Resize use orange arrows. Rotate Face uses an orange ring around
the chosen edge/reference axis. Move Part uses local XYZ axes; Rotate Part uses
local rings and the existing volume centroid pivot. The old combined
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

Rotate Face and Align Face use a separate temporary B-Rep presentation and
commit `FaceChange` snapshots to the existing undo history. Align Face keeps the
reference Part read-only. OCCT rejects unsupported topology without mutating
the source.

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
existing Pull, rotation, command-session, Move Face, face-transform and Boolean suites.

For Fedora KDE, select Faces/Parts without invoking a command and verify no
arrows appear. Start each context function explicitly, test OK/Enter and
Cancel/Esc, then click more geometry and verify the old function stays inactive.
Repeat with groups, transformed primitives and Boolean results. Test Boolean
Tools through the browser, repeated previews, Keep Tools, navigation, Undo/Redo
and keyboard focus in the floating editor. The user confirmed the Fedora KDE
manual tests passed for v0.3.5 (2026-10-20).

The selection-context suite also tests an actual background popup without any
geometry/selection, the two shared history actions plus Fit All, shortcuts, empty and
nonempty stack availability, execution of Undo/Redo, camera suppression and
selection preservation. Zoom regressions check Fit Selected for placed Parts,
Faces, multiple Parts and construction Points, plus background access and
unchanged selection/history/dirty state. Existing Face/Part picking tests remain.
The user confirmed all tested v0.3.5 functionality on Fedora KDE, including the
Unite riser, visible previews and context-menu refinements.

## v0.4.0 construction selection

Construction objects form an additional typed set, mapped by UUID to their AIS
presentations and browser rows. Ordinary click/Ctrl selects these objects; Alt
Part picking passes through them. Workplanes are picked on boundary/axis edges
rather than the complete transparent face. Hidden/locked objects stay available
for browser management but cannot supply geometry operands. A construction
command explicitly arms Point/Vertex, Plane/Face, Axis, straight Edge, circular
Edge or general Edge roles. Captured references are temporary analytic values;
results retain no source dependencies. Undo/Redo/New/Open clear all typed sets.
See [Construction geometry](CONSTRUCTION_GEOMETRY.md) for workflow and tests.

Context zoom validation (2026-10-21): OpenDesigner and the selection/session
test executable built successfully. `selection-context`, `native-document-sessions`
and `construction-sessions` all passed (31.45 seconds), including placed Part/Face
framing, multi-Part and construction Point framing, background menu access and
selection/history/dirty-state preservation. `git diff --check` passed.

## v0.4.1 2D selection

Sketch edges form their own typed selection, with stable entity UUIDs and browser
rows beneath the owning Workplane. Ordinary click/Ctrl selects 2D sets; Alt Part
selection passes through them. Sketch edges cannot be interpreted as Part Faces
or supplied to existing solid operations. Both entity and owner visibility/locks
gate viewport selection, while the browser retains management access. Undo/Redo,
New and Open clear the new selection alongside every existing type. See
[Sketch Editor](SKETCH_EDITOR.md).

In explicit Edit Sketch mode, only eligible 2D entities of the active Workplane
participate in viewport picking; other Workplanes and solids remain visible.
Drawing consumes left clicks as temporary local-XY input, with independent analytic
snap acquisition inside the configured pixel radius. Finish 2D or starting a 3D
modeling command restores ordinary picking. Alt-click outside an active drawing
also returns to Part picking. Changing the toolbar Workplane cancels any pending
drawing; hiding/locking/deleting its owner exits the editing context. Context and
snap settings are transient, are not persisted, and do not dirty the document.

Overdraw consumes clicks on analytic boundary nodes and connected bounded pieces
of Construction entities on the active Workplane. It uses the pixel radius for
acquisition, while choosing only exact analytic portions. Where several nodes or
pieces are inside the radius, its panel requires explicit selection; there is no
automatic route. Green/blue overlays and node markers are nonselectable and never
become Part/Face operands or browser rows before OK. Ctrl+Z/Redo retain the shared
session/history policy; Backspace corrects only the last temporary Overdraw choice.

Offset/common-tangent Construction tools arm named 2D operand roles. Offset accepts
straight Construction/Profile references; common tangent accepts Construction
circles/arcs only. Sources must be visible/unlocked on the active Workplane,
independently of provenance. Browser rows supply armed roles; viewport acquisition
reuses analytic nearest points and the pixel radius. Ambiguous hits require the
named list. Green tangent alternatives are nonselectable overlays; an explicit
list choice or unique candidate click chooses blue output. Neither previews nor
markers become Part/Face operands or browser rows before OK. Mouse offset-side
choice changes only the exact distance's sign.
