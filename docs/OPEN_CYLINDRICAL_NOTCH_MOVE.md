# Phase 9D.7A — repeated open cylindrical notch movement

## Recognition and material reconstruction

Earlier through-hole recognition requires two complete circular rims. A hole
crossing an edge produces partial arcs and opening generatrices, so that old
certificate correctly refuses it on the next edit. The new local fallback in
`CylindricalMoveGeometry::moveThroughHole` recognizes the current geometry:

- Grow the connected cylindrical support patch, allowing split mantles. Read its
  axis/radius and certify inward material normals and actual radial occupancy.
- Identify two axial rim planes and one exterior side parallel to the cylinder
  axis. Multiple coplanar side patches are one support, with consistent material
  orientation. Circular rim lengths must match; opening lines total two axial
  spans; mantle area must equal arc length times that span.
- Infer the missing volume as the finite cylinder intersected with the material
  halfspace of that actual exterior plane. This is a physical support, never a
  bounding-box crop or a remembered original Part/feature position.
- Fill only that volume. Certify a single valid healed solid, exact added
  material, and independent recutting back to the original source. All planar
  boundaries of the fill must close on actual exterior surfaces, including the
  old opening plane and both axial ends. Wall area must match the inferred trim.

Translate the unchanged finite cylinder perpendicular to its axis, then subtract
it from the healed shape. The destination cut must terminate on the same two
axial exterior planes, refusing blind cuts or ceilings beneath an unrelated boss.
`finishLocal` independently preserves material outside the old/new cylinder
region and every unaffected trimmed surface. Only planar side regions actually
intersected by the new tool join the affected set; unrelated cylindrical features
remain protected. Final acceptance checks exact removed material, allowed
analytic supports, a valid closed manifold solid and a supported new neighborhood.

This certificate also accepts a complete current hole when it needs to reopen
against a side of a nonrectangular support. It does not require the whole Part
to match rectangular stock or a global cylinder count. Ordinary certified fast
paths remain in place. Every operation reanalyzes the new B-Rep; no feature IDs,
creation flags, former axes or historical boundary policies are stored.

## Supported results and safe refusal

The wall can move farther outside, along an edge in either direction, or inward
into a complete circular through-hole. A hole can then reopen on another edge
provided the final opening has one exterior support. Splits, merges and healed
boundary segments are legitimate. If the cylinder leaves completely, return
`HoleExited` and the healed material; no hidden feature remains to select.

Corner/multiple-side notches, sloped or curved rim boundaries, blends, blind or
depth-varying notches, inconsistent material sides, interacting features and
uncertifiable/tangent degeneracies are refused atomically with diagnostics.
Existing numerical/complexity budgets and manifold/material checks stay strict.
Already unsupported lateral movement of a truncated/overhanging selected boss
is separate; its supported planar cap editing remains available.

## Interaction and focused regressions

Reuse the existing Move Face session, typed selection, current AIS face owners,
explicit lateral reference/angle, world-mm numeric input and blue candidate
preview. The panel identifies the through-hole/open-notch class and hides boss
Trim/Extend. No new command, persistence format or history mechanism is added.
No notch reconstruction invokes OCCT offset; the 9D.6/9D.6B safeguards remain.

`cylindrical-open-notch-geometry` checks complete → notch → outward → along edge
→ complete → reopen → another edge, imported/current geometry without prior
operations, split mantles, complete exit, unchanged boss/hole/planar material,
independent analytic volume and occupancy, strict solid/manifold validity and
input immutability. Variants include a complete boss, a trimmed boss, an
overhanging boss and a rotated nonrectangular support. Corners, intersections
with another hole/boss support and axial motion are safe refusal cases.

`cylindrical-open-notch-sessions` picks the actual notch in the viewport and
activates its context action, drags a reconstructed preview, cancels with Escape,
uses exact localized numeric input along both lateral references, restores a full
hole, reopens it, previews the unaffected boss, then undoes/redoes all confirmed
steps using cached shapes. It checks Part UUID, placement, appearance, browser
row, other Parts, current selection owners and original source serialization.
The portable fixture includes rotated/scaled Part placement. Native reader replay
accepts each supplied file without modifying it:

```
build/OpenDesignerMoveFaceTests --open-notch /absolute/path/document.odc
```

## Verification

The November 3 focused CTest log records all ten selected regressions passing:
the new notch geometry/session tests, cylindrical move geometry, independent
features, boundary follow-up, local neighborhoods, post-Trim Pull, the isolated
planar crash regression, cylindrical sessions and native-document recognition.
Their binaries postdate the affected sources; resuming this task did not change
implementation or repeat those passed tests.

On November 4, focused `--open-notch` native-reader replays of `moveCylinder.odc`
and `moveCylinderBug.odc` passed repeated previews/confirmations, viewport
selection, drag/Cancel, cached Undo/Redo and source-file immutability. The GUI
replays require access to an X11/OpenGL display. Private ODC files remain
untracked. No full CTest suite was run. Manual Fedora KDE acceptance and the
broader phase 9E persistence work remain pending.

## Later planar-rim correction

The [rotated-top correction](ROTATED_RIM_CYLINDER.md) reuses current planar
material boundaries more generally. Move Face and radial Pull now accept valid
multi-plane/corner trims when their fill/re-cut and protected-material
certificates succeed. The original phase 9D.7A fallback remains available, but
its one-side limitation is not an analytic cylinder-recognition rule.
