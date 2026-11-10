# Fillet and Chamfer — v0.5.0 initial implementation

Choose **3D Modeling > Fillet** or **3D Modeling > Chamfer**, then click one
edge on a visible Part. Enter a positive radius or equal distance in millimeters
in the existing floating command panel. Fractional values retain double precision;
there is no snapping or rounding of the requested treatment. The inherited numeric
editor range is 100000 mm. For scaled instances the world distance is converted
to local distance once; placement is preserved.

The complete candidate solid appears in blue with 25% transparency. Numeric
changes rebuild from the original B-Rep. Invalid geometry or incomplete input
removes the candidate, restores the original presentation, disables OK and reports
the reason in the status bar. Preview never writes canonical document geometry.
OK/Enter confirms one cached FaceChange; Cancel/Escape or a tool switch removes
all preview objects and leaves history, including Redo, intact. Picking returns
to normal Face selection after termination. Selecting another object after the
edge is chosen follows the existing command cancellation convention.

## Geometry and safety

`FilletGeometry::apply` uses `BRepFilletAPI_MakeFillet` with one selected edge and
a positive constant radius. `ChamferGeometry::apply` uses
`BRepFilletAPI_MakeChamfer` with two identical distances and a face obtained from
actual edge-to-face adjacency. Equal distances make the chosen face side
immaterial. Both operate on one deep copy and map the selected topology into it.
No feature history, primitive recognition, mesh conversion or healing is used.

`EdgeBlendValidation` checks finite positive input, membership, non-degenerate
edges, two distinct adjacent faces and absence of seam topology. Both paths use
the existing `BRepReconstruction::validSolid` policy before and after construction:
one solid, one closed shell, no stray face/edge/vertex topology, BRepCheck validity,
positive finite volume and isolated self-interference analysis. OCCT must complete
successfully. Exceptions become actionable errors; failures return a null shape.

Candidates reuse `presentFacePreview`, including tessellation and nonselectable
blue AIS display. Confirmation replaces the B-Rep of the same Part slot, marks
it GeneralBRep and records complete before/after snapshots. UUID, appearance,
visibility, placement and browser order use the existing FaceChange lifecycle.
Native `.odc` storage already saves the exact resulting B-Rep; no schema change
or persistent blend feature is introduced. Save during preview saves confirmed
geometry. Undo/Redo restores cached shapes and clears selection.

## Scope and limitations

One selected edge on one Part per command. Ordinary convex box edges and concave
prismatic notch edges are tested. OCCT may extend a blend along a tangent contour;
there is no separate multi-edge/junction control. Oversized treatments, seam,
non-manifold, foreign and degenerate edges are refused. The shared validator
conservatively rejects solids with multiple shells, including enclosed cavities.
Arbitrary imported/freeform geometry is accepted only when the kernel and shared
validation succeed; broad freeform coverage is not claimed. Variable radii,
asymmetric/angle chamfers, setback corners, face-to-face blends and multi-edge
selection remain future work. Geometry computation is synchronous, like existing
face commands; expensive arbitrary models have no background cancellation.

## Automated verification

`Fillet-geometry` and `Chamfer-geometry` check analytic convex material volume,
concave material addition, invalid sizes/selections, valid closed solid topology
and byte-identical serialized source geometry across success and failure.
`edge-blend-sessions` checks OCCT edge picking on a translated/rotated Part,
fractional input, blue-preview ownership, malformed input cleanup, Escape/Enter,
Cancel/Redo preservation, one-step history, stable identity/metadata/Part slots,
unrelated Part preservation and native Save/Open including confirmed-only Save.

Validation on Fedora 44, Release build, 2026-11-10:

- `cmake --build build -j 4`: PASS.
- Targeted `Fillet-geometry` and `Chamfer-geometry`: 2/2 PASS.
- Targeted `edge-blend-sessions`, `selection-context`,
  `native-document-sessions`, `command-panel-sessions`: 4/4 PASS (65.00 s).
- `ctest --test-dir build --output-on-failure -j 2 --timeout 300`:
  92/92 PASS, 0 failed, 0 skipped (759.78 s), including all 89 existing tests.
- `git diff --check`: PASS.

Viewport tests required execution outside the filesystem/network sandbox to access
X11/OpenGL. Initial sandboxed viewport attempts aborted before test output; all
four passed with display access. No tests were disabled or assertions weakened.

Native Fedora KDE visual acceptance remains manual; automated viewport checks do
not establish behavior on every compositor, screen or display scale.

## Fedora KDE acceptance checklist

1. Launch `build/OpenDesigner`, create a Box, choose **3D Modeling > Fillet**,
   then click a visible convex edge. Enter `2.345` mm (use the locale decimal
   separator); verify a complete translucent blue round and unchanged browser row.
2. Change the radius repeatedly. Enter zero, a negative number, incomplete input
   and an oversized radius; verify the original returns and OK is unavailable.
   Correct the value, then Cancel. Repeat and press Escape while editing the field.
3. Confirm using OK; verify one Undo restores the box and one Redo restores the
   fillet. Verify name, color, visibility, Active Part and placement remain correct.
4. Repeat with **Chamfer**, using an equal distance of `2.345` mm; inspect both
   adjacent faces. Repeat cancellation, invalid input and one-step Undo/Redo.
5. Rotate/translate a Part and repeat both operations. Check placement and exact
   world-space treatment size. Test a concave notch edge when available.
6. Save during preview and reopen a copy: only confirmed geometry should exist.
   Save each confirmed treatment, reopen and inspect the solid and Part identity.
7. Switch tools during edge picking and preview; invoke Undo/Redo during preview;
   verify no blue objects, stale edge highlights, gizmos or panels remain.
8. Repeat at normal and 200% display scale, checking numeric focus, panel placement,
   typing and OK/Cancel visibility. Camera orbit/pan/zoom must preserve the session.
