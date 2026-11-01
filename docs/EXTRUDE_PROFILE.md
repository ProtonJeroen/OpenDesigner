# Extrude — v0.4.8

For the v0.4.8 Extrude/Revolve reuse contract and phase boundaries, see
[Shared modeling architecture](SHARED_MODELING_ARCHITECTURE.md).

Activate a visible, unlocked Workplane with **Edit Sketch**, then choose
**3D Modeling → Extrude** (also on the 3D Modeling toolbar). All Profile entities
on that Workplane participate, including hidden/locked Profiles. Construction
entities never close a profile. Sources are independent and are not consumed.
There is one menu/toolbar action and one Command Panel; the former Extrude Profile
and separate Cut Extrude workflows are consolidated here. Revolve uses the same
three operation modes and target policy; see [Revolve](REVOLVE.md).

The approved workflow is inspired by SolidDesigner v16's closed-profile
modeling. The controls and policies below are OpenDesigner implementation choices;
no undocumented original v16 interaction is claimed.

- **New Solid** (default): create one GeneralBRep Part. Disconnected material
  regions and islands remain separate solids in a compound within that Part.
- **Add Material**: use the eligible Active Part automatically; otherwise pick
  one visible target directly in the viewport. **Pick target part**, the dropdown
  and Structure Browser allow explicit overrides without changing Active Part.
  The preview is the actual fused target. No temporary extrusion-tool Part is
  committed. Target identity, name, color, visibility, browser slot and instance
  transform are retained. Other Parts are
  untouched. Targets must currently contain one connected solid.
- **Remove Material**: use the same target policy as Add. Select certified regions,
  **Blind** or **Through All**, and optionally **Reverse direction**. See
  [material removal details](CUT_EXTRUDE.md).
- Changing Operation resets the temporary target to an eligible Active Part or
  starts picking. New Solid clears the target and stops picking. Remove-only
  controls keep their values while hidden; they do not affect New Solid or Add.
  Signed distance remains available for New Solid/Add; Remove requires positive
  Blind depth and ignores the distance field in Through All.
- Signed **Distance (mm)** defaults to +20. Positive follows the Workplane normal;
  negative goes the other way. Numeric input uses the desktop locale.
- Blue preview is nonselectable, temporary and independent of document/history.
  Parameter changes rebuild from the original source snapshot. Invalid values,
  disconnected additions and unchanged fusions clear preview and disable OK.
- **OK/Enter** confirms one history operation; panel **Cancel** discards it and
  restores selection. **Escape** first ends target picking if it is active;
  otherwise it cancels the command. Cancelling preserves existing Redo. New/Open,
  source/model changes or switching commands cancel the session. Save captures confirmed
  geometry and source Sketches, never the preview.

## Geometry and ownership

`SketchProfileExtrusion` is headless. It consumes the certified planar faces from
`SketchProfileBuilder`; `BRepPrimAPI_MakePrism` preserves line/circular supports.
Every prism must be one valid, closed, outward-oriented solid with positive
volume. Measured volume and solid count must match source face area × depth.
Multiple source regions produce only a solid compound, never filled holes.
Distances must be finite, greater than twice the certified vertex tolerance and
at most 1e9 mm; world coordinates must resolve OCCT Confusion within 1/16 ULP
budget, so large numerically unsafe placements/distances are refused.

`BooleanGeometry` shares the existing Unite implementation's non-destructive
Fuse, solid extraction and safe same-domain normalization unchanged. Fusion must
produce exactly one valid closed solid and add measurable target volume.
Normalization retains the existing material-equivalence certificate. Tool/target
geometry is copied. Target instance transforms are applied once in world space;
confirmed geometry is inverse-transformed once into the existing local Part.

New Parts reuse `BooleanChange` creation/restoration; additions reuse `FaceChange`
at the original Part index; removal uses the same in-place path. Both use the
existing Undo/Redo stacks and native GeneralBRep storage. UUIDs survive Undo/Redo.
No new `.odc` schema or persistent feature/history link is introduced. Source Sketch/WP values remain unchanged.

Unsupported: Profile Ellipse/EllipticalArc, automatic healing, multi-solid Add
Material targets, multiple targets, tapered/symmetric/path extrusion and
new manipulators. Through All is available only for Remove Material. Explicit
safe refusal is intentional. Remove can leave multiple solids in the target Part,
but complete target removal and operations with no measurable material change
are refused.

## Validation and Fedora KDE acceptance

Automated tests: headless `sketch-profile-extrusion`, plus
`profile-extrusion-sessions` and `profile-extrusion-sessions-hidpi` using the
existing viewport/session test executable. Phase 8E recorded all six targeted
Extrude/Revolve geometry and normal/HiDPI session checks passing; these results
remain applicable because phase 8F changes no modeling behavior.

User-reported manual Fedora KDE acceptance, recorded 2026-11-01: **Unified
Extrude 7/7 passed**. Together with Revolve New Solid (13/13) and Revolve
Add/Remove (15/15), v0.4.8 manual acceptance is **35/35 passed**. The feature is
ready for publication within the documented limits. The reference checks below
remain available for future regressions; they are not a new pending gate:

1. Empty document: create XY/XZ/YZ Workplanes and closed profiles (rectangle,
   Circle, mixed Arc contour, hole/island/disjoint regions). Check +20, a changed
   depth and negative depth; blue result, correct direction and no consumed sources.
2. Check OK/Enter versus Cancel/Escape, invalid/zero/near-zero input and preview
   removal. Confirm Cancel restores selection and retains Redo; one Undo/Redo
   restores the same Part identity/geometry. Preview must not mark the file dirty.
3. Add a rectangular/circular boss to a Box and a GeneralBRep. Repeat on a moved
   and rotated target; choose target via dropdown, viewport and browser. Verify
   target properties and other Parts remain unchanged. Refuse disconnected and
   wholly contained/unchanged additions without stale previews.
4. Save/Open confirmed geometry with source Workplanes/Sketches, holes and
   compounds. New/Open and changing commands during preview must remove the panel
   and temporary geometry. Check New Solid Move/Rotate, visibility, color,
   duplication and existing Boolean commands.
5. At normal/200% scaling: check full panel/OK/Cancel, numeric focus, 12-pixel
   viewport margin, manual dragging and resize/docking without covering the model
   or jumping the panel back during parameter edits.

Material removal is available through Extrude → Remove Material, which shares
the certified Profile Builder, prism generation and in-place target history paths.
