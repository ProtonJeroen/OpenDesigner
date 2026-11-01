# Active Part and target picking

## Architectural decisions (before implementation)

The current document owner is CadViewport: its CadPart vector owns stable
persistentId UUIDs, geometry, transforms and AIS presentations. NativeDocumentSnapshot
is the persistence boundary. Keep Active Part here as one UUID, independent of
selectedPartIndices and OCCT selected owners. The existing `active` part flag means
membership in the document (including undo tombstones), not Active Part.

Explicit activation is saved UI/modeling metadata, marks the document dirty, and
is not a geometry undo transaction. Geometry undo/redo preserves a still-valid
reference; removal clears it, and restoring a removed part does not reactivate it.
Invalid references are cleared at the existing partsChanged lifecycle boundary.

Store an optional named XCAF metadata child without changing the schema marker.
Old files have no reference; old readers ignore this child. Validate imported IDs.

Use the existing AIS shading aspect's native OpenGL silhouette option for both
Active Part (#A7D400) and Selected Part (existing cyan), at 2.8 pixels. Selected
Part takes precedence. The inverted-hull pass uses camera matrices and the
existing triangulation; it does not extract topological edges or duplicate BReps.
PartPresentation retains AIS_Shape picking and sensitive geometry. Only its global
selection owner's wireframe-highlight callback is suppressed; local face/edge
selection and dynamic hover still use OCCT's original callbacks. Generic face
boundaries and polygon edges remain disabled. PartPresentation adds only sharp
feature edges to the existing shaded presentation using StdPrs_WFShape. It rejects
cylindrical seams and degenerate edges, then uses BRepLib::ContinuityOfFaces to
exclude geometrically tangent joins (angular tolerance 1e-4 radians). Hole rims,
boss rims and sharp transitions remain. Edges share the scene depth buffer and
part transform, so occluded portions disappear as the camera moves. No BRep is
copied and the feature-edge container is never used for selection. Non-manifold
or unclassifiable joins are omitted. Older OCCT builds without the native API
report that only sharp feature edges can be shown; these do not replace the
missing silhouette.

A shared callback-based target picker resolves a detected AIS owner to a stable
part ID and checks visibility/solid eligibility. Commands retain their own temporary
target. Extrude Add Material and Remove Material consume this adapter.

The current model/browser is flat, with no nested assembly state. This change
adds no assembly activation semantics; a future hierarchy must retain part UUIDs.

## Modeling workflow

Right-click a Part in the Structure Browser or its geometry in the viewport and
choose **Set Active Part**. The green browser icon and yellow-green silhouette
identify the current modeling target. Set another Part active to switch; choose
**Deactivate Part** on the current Part or **Clear Active Part** to clear it.
The browser root and empty browser space also offer Clear Active Part.

Active Part is independent of ordinary selection. Faces, edges, vertices, whole
Parts and multiple selections keep their existing feedback. Activation leaves
shape, placement and material unchanged. A hidden Active Part retains its identity
but displays no outline, and cannot be used as a visible command target.

Extrude Add Material and Remove Material use an eligible Active Part initially and shows its name in Target
Part. Without one, click visible geometry at the **Select target part** prompt.
A face click resolves to its owning Part using OCCT's nearest detected owner.
Use **Pick target part** to change the temporary target, or use the target list or
Structure Browser as a fallback. Temporary target changes never activate a Part.
An ineligible Active Part is explained in the status bar; choose a target explicitly.
Empty space keeps picking pending and shows a prompt. Escape ends picking while
keeping the command open; press Pick target part to resume. Once picking has ended,
Escape cancels the command. Panel Cancel always cancels the whole command.
Extrude still defaults to a new solid and does not merge with Active Part.

Activation/deactivation marks the document modified but does not consume a geometry
Undo step or clear Redo. Undo/Redo preserves activation through transforms and
in-place geometry replacement. Removing its Part clears activation; restoring that
Part does not automatically reactivate it. New clears activation. Save/Open stores
and restores it; older files open without an Active Part. Invalid references are
rejected before document replacement.

## Manual Fedora KDE acceptance (pending user verification)

- Activate a Part from the Structure Browser; verify the green icon and subtle outline.
- Select another Part and faces/edges/vertices; verify Active Part stays fixed.
- Switch Active Part; verify only the new Part has the indicator.
- Deactivate it and verify the green outline disappears; also test Clear Active Part.
- Activate/deactivate from viewport context menus with a different Part selected.
- Right-click empty space; verify it offers no Set Active Part action.
- Start Cut Extrude with Active Part; check the named target, Blind/Through All,
  Reverse direction, region selection, live preview and confirmation.
- Start Cut Extrude without Active Part; click geometry, including overlapping Parts.
- Change the command target with Pick target part and the fallback list/browser;
  verify Active Part stays fixed.
- Click empty space while picking; press Escape, resume picking, then Panel Cancel;
  verify geometry remains unchanged.
- Check hover and selected-part/face colors remain readable over the green outline.
- Hide/show Active Part; verify the outline follows visibility.
- Move/rotate/modify Active Part, then Undo/Redo; check placement and outline.
- Undo an operation that removes a Part; verify there is no stale active reference.
- Save, close and reopen; check identity, outline, material and target initialization.
- Verify native and HiDPI command panel positioning, including opening, moving,
  resizing and switching commands on Fedora KDE/Wayland with XWayland.

Native OpenGL outlines require closed triangulation with vertex normals and
backface-culling, as in shaded solid Parts. Open shells, transparent geometry and
non-OpenGL rendering are not certified. Tessellation and vertex normals affect
outline quality; this is a GPU outline, not an exact BRep HLR computation. The current flat browser has no nested assemblies to exercise.
Interactive desktop acceptance is separate from automated session tests.

## Automated validation — 2026-10-31

- `cmake -S . -B build` and `cmake --build build -j 4` succeeded.
- Numerical extrusion regression passed; all seven affected document, selection,
  Active Part and normal/HiDPI extrusion session tests passed.
- Panel positioning and normal/HiDPI extrusion sessions each passed five
  consecutive runs after isolating focus-sensitive standalone tests.
- Final `ctest --test-dir build --output-on-failure -j 4`: **61/61 passed**,
  197.37 seconds. GUI tests required desktop access outside the filesystem sandbox;
  sandboxed GUI processes aborted during startup while native storage passed.

Investigation of initial failures found that redisplaying every Part during an
Active Part refresh could revive OCCT selection highlighting. Refresh now only
redisplays presentations whose outline state changed. The overlapping-pick test's
manual transform also needed the normal camera depth refresh. Finally, parallel
native GUI processes reproduced focus theft in the standalone panel tests; those
two tests now run serially and reuse the bounded initial-anchor wait. No geometry
assertions, positioning tolerances or test coverage were relaxed.

Native silhouettes were exercised programmatically with OCCT 7.9.3. The older-OCCT
boundary fallback has not been runtime-tested on an older installation. These
results do not constitute interactive Fedora KDE acceptance.


## Silhouette-only rendering refinement

The former face-boundary compatibility fallback and selected-part wireframe are
removed from part highlighting. The native method is implemented in OCCT's
[OpenGl_PrimitiveArray outline pass](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_9_3/src/OpenGl/OpenGl_PrimitiveArray.cxx#L1003).
It expands back-facing triangles along vertex normals in the outline shader,
uses the scene depth buffer, and reads the current camera/object transforms on
each draw. It requires native outline shader support and closed, oriented meshes.
There is no silent fallback to seam/edge highlighting. Local subshape highlighting
and hover can still show their own edges by design.

Only OpenDesigner and OpenDesignerSelectionContextTests were built for this
refinement. The relevant Active Part, selection-context and normal/HiDPI extrusion
viewport session checks passed (4/4). Full-suite and stress tests were not run.
Manual Fedora KDE visual acceptance is pending.


## Yellow-green and feature-edge refinement

Active Part now uses #A7D400 at 2.8 pixels. Selected Part retains cyan at 2.8 pixels
for both silhouettes and feature edges, with priority over Active Part. Unlike
the earlier generic boundary highlighting, feature classification uses actual
face continuity rather than relying solely on stored regularity flags. Existing
AIS depth testing and polygon offset handle edge visibility against shaded faces.
The native silhouette limitations above still apply.

Only OpenDesigner and OpenDesignerSelectionContextTests were built. The four
relevant Active Part, selection and normal/HiDPI extrusion viewport tests passed.
Regression checks cover feature-line presentation color/width, cylinder rims
without seams, four hole/boss rims, and omission of a sewn coplanar tangent join.
No full suite or stress tests were run. The referenced CoCreate screenshot was
not attached to this request; visual matching and Fedora KDE acceptance remain
pending manual review.
