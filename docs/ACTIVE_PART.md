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

Use the existing AIS shading aspect's native silhouette option for the persistent
green outline; no duplicate BRep or selectable overlay. Selection and hover remain
OCCT highlights. Older OCCT versions without that API use drawer face boundaries
as a compatibility fallback. Both follow the object's transform and visibility.

A shared callback-based target picker resolves a detected AIS owner to a stable
part ID and checks visibility/solid eligibility. Commands retain their own temporary
target. Cut Extrude consumes this adapter; Extrude Add semantics stay unchanged.

The current model/browser is flat, with no nested assembly state. This change
adds no assembly activation semantics; a future hierarchy must retain part UUIDs.

## Modeling workflow

Right-click a Part in the Structure Browser or its geometry in the viewport and
choose **Set Active Part**. The green browser icon and light-green boundary outline
identify the current modeling target. Set another Part active to switch; choose
**Deactivate Part** on the current Part or **Clear Active Part** to clear it.
The browser root and empty browser space also offer Clear Active Part.

Active Part is independent of ordinary selection. Faces, edges, vertices, whole
Parts and multiple selections keep their existing feedback. Activation leaves
shape, placement and material unchanged. A hidden Active Part retains its identity
but displays no outline, and cannot be used as a visible command target.

Cut Extrude uses an eligible Active Part initially and shows its name in Target
Part. Without one, click visible geometry at the **Select target part** prompt.
A face click resolves to its owning Part using OCCT's nearest detected owner.
Use **Pick target part** to change the temporary target, or use the target list or
Structure Browser as a fallback. Temporary target changes never activate a Part.
An ineligible Active Part is explained in the status bar; choose a target explicitly.
Empty space keeps picking pending and shows a prompt. Escape ends picking while
keeping the command open; press Pick target part to resume. Once picking has ended,
Escape cancels the command. Panel Cancel always cancels the whole command.
Extrude Profile still defaults to a new solid and does not merge with Active Part.

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

The older-OCCT boundary fallback can include internal face boundaries and does not
compute curved surface silhouettes. The current flat browser has no nested assemblies to exercise.
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
