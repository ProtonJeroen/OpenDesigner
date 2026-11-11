# Unified selection and explicit modeling commands (v0.5.1)

Selection is passive. There is no Faces/Parts dropdown.

| Input | Selection |
| --- | --- |
| Left click near a visible edge | One Edge (straight or curved) |
| Left click inside a face | One Face |
| Second distinct click on the sole selected Edge/Face | Owning Part, with no double-click timing requirement |
| Left click another Edge/Face, including on a selected Part | Start a new subelement selection |
| Unmodified left click empty space | Clear selection |
| Ctrl + left click | Add/remove Edges or Faces in a same-type set; no promotion |
| Alt + left click | One Part |
| Ctrl + Alt + left click | Add/remove Parts |

Releasing Alt does not change the current selection or its type. Structure
Browser rows select Parts. Selecting a different type starts a fresh typed set. Edge/Face/Part sets are not mixed.
Promotion applies only to a sole selected subelement; Ctrl and multi-element
sets deliberately do not promote. Ctrl-background clicks retain their existing
typed set, matching the prior additive-selection behavior.
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

## Temporary Project 3D Geometry selection (v0.4.2)

During an active Sketch projection session, visible Parts temporarily expose
OCCT Vertex, Edge, Face or Mixed subshape selection. The existing OCCT owners and
cyan highlights are authoritative; captured Part-local subshapes, stable Part
IDs and separate placements are transient validation operands, not document
dependencies. Whole Parts, construction objects, 2D entities and blue previews
cannot be acquired as sources. Click replaces and Ctrl+click toggles; mode changes
retain only eligible types. Repeated/reversed instances of the same Part subshape
are canonicalized before the kernel's geometric duplicate checks.

Normal 2D selection is restored on close. Cancel restores safely pickable prior
Sketch selection; commit and Undo/Redo clear selection. The projection context
menu has Clear/Cancel/Fit All and does not turn the session into a normal 3D face
edit. No new persistent selection system or browser subshape hierarchy is added.
See [the full workflow](SKETCH_PROJECTION.md#phase-5-interactive-project-3d-geometry).

## Unified picking and highlighting (v0.5.1)

Normal picking activates OCCT Edge and Face modes together. OCCT prioritizes the
nearest visible geometry and resolves adjacent-edge sensitivity in screen space.
The aperture is four logical viewport pixels: event positions and the native
view size already use Qt logical coordinates, so the aperture is not multiplied
again on HiDPI displays. Interior face picks remain Faces. Mode activation changes
only when necessary; ordinary hover does not recreate selection owners.

Tool-specific picking takes precedence: Fillet/Chamfer pick Edges, Move Face
picks Faces, rotation/reference pickers keep their requested types, and Part
transform manipulators receive mouse presses before ordinary selection.
Subelement selection is passive. A selected Edge's context menu offers the
existing Fillet/Chamfer commands, which then request their command-specific edge.
Structure Browser selection remains whole-Part selection.

Selected contours are light blue `#87CEFA`, approximately 2 logical pixels;
hover contours are cyan `#00FFFF`, approximately 4 logical pixels. Widths scale
with Qt's device pixel ratio. Hover overrides a selected Face/Part and pointer
exit restores the selection. Edges retain OCCT's individual-edge highlighting.

Faces use exact, visible 3D HLR outlines and visible trimmed-region boundaries,
excluding seams, isoparameters and overlapping silhouette segments. The contours
are drawn in the owning shaded Part presentation, preserving its material and
scene occlusion. Camera projection and Part placement enter the HLR projector;
contours are cached until the Face, camera, placement or appearance changes.
The complete Part retains its native silhouette renderer and adds only geometric
Fillet transition edges for selection/hover. Certified feature face membership
and analytic tangent-support classification retain cylindrical/toroidal Fillet
boundaries even at smooth joins; seams and unrelated sharp edges are excluded.
The independent Active Part presentation retains its existing green features.
Face/Part owners suppress OCCT's stock full wireframe while retaining their
sensitive geometry and selection semantics. On OCCT builds without native Part
silhouette support, only classified transition edges are available for selected
Parts. Active Part green returns when whole-Part selection/hover ends.

Promotion compares the current selected and detected AIS owners' actual topology
and owning presentation. No last-click TopoDS pointer, face index or timer is
stored. Existing geometry-replacement/history cleanup remains authoritative;
stale Edge owners are rejected by the same membership policy as Faces.

### Manual Fedora KDE acceptance

1. Click a box Edge, wait longer than the double-click interval and click again:
   expect Edge then Part. Repeat inside a Face. Click another element to reset.
2. Select a Part in the Structure Browser; click an Edge or Face to return to it.
   Check Alt immediate Part selection, Ctrl typed multi-selection, Ctrl+Alt Part
   multi-selection, and clearing on empty space.
3. Hover a selected Edge/Face and an Alt-hovered Part: light blue should become
   cyan and thicken from approximately 2 to 4 logical pixels. Check circular rims, occluded geometry,
   orbiting, leaving the viewport, and moving between screens at different scale.
4. Run Fillet and Chamfer Edge picking and preview, then Move Face, Pull, Rotate
   Face and Part transform drags. Check command picking never promotes to Part.
5. Confirm geometry changes, Undo/Redo and Save/Open; make fresh subelement picks.
   Verify no stale owners or hover artifacts and no accidental modeling on click.

Manual KDE/Wayland visual acceptance remains pending. The automated normal and
`QT_SCALE_FACTOR=2` tests check selection semantics and highlight configuration;
line rasterization and native silhouette support still depend on OCCT/driver.

### Corrected contour validation — 2026-11-11

Incremental builds of `OpenDesigner` and `OpenDesignerSelectionContextTests`
passed with Ninja `-j2`. The exact focused CTest command was:

```sh
ctest --test-dir build -R '^(selection-contours|automatic-selection|automatic-selection-hidpi|selection-context|active-part-sessions)$' --output-on-failure -j 1
```

| Test | Result | Seconds |
| --- | --- | ---: |
| active-part-sessions | PASS | 2.33 |
| selection-contours | PASS | 0.23 |
| automatic-selection | PASS | 3.98 |
| automatic-selection-hidpi | PASS | 4.24 |
| selection-context | PASS | 30.78 |

All five passed in 41.57 seconds. Coverage distinguishes cylinder silhouettes
from seams (including a seam coincident with a silhouette), retained trimmed
rims, planar contours, toroidal Faces and tangent Fillet transition edges,
imported analytic classification, camera changes/cache reuse, translated Faces,
canonical BRep immutability, hover color/width and selection restoration in the
actual shaded presentation at normal/2x DPI. Existing Part rotation and selection
history assertions also pass. No full suite, unrelated geometry tests or clean
rebuild was run. Existing untracked `.odc` files were left untouched.

### Initial selection validation — 2026-11-11

Incremental Release builds of `OpenDesigner` and
`OpenDesignerSelectionContextTests` passed with Ninja `-j4`. Exact CTest names
were run with `-j2`; automatic-selection tests run serially against the display.

| Test | Result | Seconds |
| --- | --- | ---: |
| automatic-selection | PASS | 4.12 |
| automatic-selection-hidpi | PASS | 4.19 |
| selection-context | PASS | 31.89 |
| active-part-sessions | PASS | 2.39 |
| edge-blend-sessions | PASS | 7.89 |
| edge-blend-feature-sessions | PASS | 10.77 |
| native-document-sessions | PASS | 3.25 |

All seven targeted tests pass across focused runs. The new tests cover exact and
near Edge picks, visible-edge depth preference, interior Face picks, delayed
promotion, Part-to-subelement return, Alt, Ctrl typed selection, selected-owner
hover, width/color/depth configuration, leave cleanup, circular picking, Fillet
picker precedence and selection after geometry replacement/Undo/Redo.
Existing modeling tests keep their geometry and history assertions. The Active
Part test checks the new selected width of 2 logical pixels, and the independent
second Pull case explicitly clears its retained Face before a fresh click (a
repeat click now intentionally promotes to Part).

The full CTest suite and unrelated expensive OCCT geometry tests were not run.
Manual KDE/Wayland visual acceptance remains pending.
