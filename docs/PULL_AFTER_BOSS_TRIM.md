# Hotfix 9D.6B — radial Pull after boss Trim

## Coredump diagnosis

The newest Fedora coredump is PID 17958, November 3, 2026, 21:43:24 CET.
GDB confirms SIGSEGV in OCCT 7.9 during a mouse-drag preview, through:

```
Qt mouse event delivery
CadViewport::mouseMoveEvent
CadViewport::previewPrimitivePull
CadViewport::applyCylindricalFacePull
BRepOffset_MakeOffset::MakeOffsetShape / BuildOffsetByInter / IntersectEdges
BRepOffset_Inter2d::ConnexIntByInt / RefEdgeInter
BRepAdaptor_Curve::Initialize
BRep_Tool::CurveOnSurface  [SIGSEGV]
```

This is a separate application entry from the planar Move Face crash fixed in
`f03ddd5`. Pull still invoked the global offset builder directly. The crash is
in preview, after successful activation and before confirmation. It is an unsafe
OCCT intersection operation, not an exception that the existing catch can handle.

The old capability check accepted any cylindrical face in a valid solid. It did
not certify its mantle, connected patches, rims, cap, side cuts or material side.
After Trim, the boss has a partial outward-facing cylindrical mantle, two axial
planes, a planar side cut and partial circular rims joined by straight edges.
That is valid geometry, but it cannot be treated as an unrestricted cylinder
offset. Replay of the post-Trim geometry verifies valid source topology and
finite original edge p-curves; the failing kernel call is in generated offset
edge intersections, not tool activation or a stored invalid input p-curve.

## Geometry-driven correction

Add geometry-only `CylindricalMoveGeometry::resizeRadius`. It uses the existing
neighborhood copy/mapping, material Booleans, normalization and local acceptance:

- Grow the actual connected cylindrical support patch. Classify material using
  oriented surface normals. Require two axial rim faces and at most one radial
  planar cut for a boss; complete through-holes have no radial cut.
- Construct finite cylinders from the current axis, radius and axial supports.
  Retain the existing cut plane when present. Certify the old feature's exact
  material and cylindrical wall coverage independently against the source.
- Remove/fill only that feature and certify recovery of the original material.
  Boss resizing requires one fully supported base footprint on the actual healed
  support face, including its trims. Holes must close on two exterior rim planes.
- Add/subtract the requested radius, with axial/cut supports fixed. Certify
  outside-region material, unaffected trimmed surfaces, allowed analytic supports,
  positive closed-solid validity and manifold connectivity. Preserve input on
  every refusal; allow legitimate mantle/cap/edge splits and merges.

Pull activation certifies the selected B-Rep at its current radius and explains
unsupported neighborhoods before arming a drag. Every preview reconstructs from
the immutable pre-drag snapshot. `applyCylindricalFacePull` no longer calls OCCT
offset or promotes offset shells. There is no unsafe kernel fallback and no
primitive-creation or feature-history dependency. Existing Part presentation,
preview rollback, Cancel and cached Undo/Redo are reused.

## Scope and regression

Supported radial edits: complete through-holes, complete fully supported bosses,
and fully supported bosses trimmed by one planar side surface. Radius/diameter
changes keep the current height, axis and side-cut plane fixed. Shrinking can
remove the side cut and leave a complete cylinder. Once that cut is absent,
later growth uses the current B-Rep: it cannot infer a former Trim policy to
invent a missing cut or extend an unsupported footprint. General height Pull,
overhang/support extensions, blind/open holes, multiple radial cuts, blends and
sloped caps require additional certificates and are refused with diagnostics.
Height edits through already supported planar Move Face remain separate.

`cylindrical-radius-geometry` checks growth/shrinkage, complete/partial topology
transitions, repeated edits, transformed inputs, independent hole preservation,
analytic volume and occupancy, manifold validity, input immutability and safe
refusals. `trimmed-boss-pull` runs in a dedicated process and performs an actual
Move Face → Trim → confirm → Pull activation → arrow drag sequence, plus preview
recovery, Cancel, commit, Undo/Redo, repeated editing and unsupported activation.

An optional private native-file replay accepts the existing post-Trim geometry:

```
build/OpenDesignerPullTests --trimmed-boss-pull /absolute/path/crash03112026.odc
```

The actual native file replay passed in a separate GDB process with an assertion
that `BRepOffset_MakeOffset::MakeOffsetShape` was never entered. The file is read
only, retains its original bytes and is not committed as a fixture.

## Verification

Incremental application, cylindrical geometry, Pull and Move Face test targets
built. All seven focused regressions passed (55.96 seconds): radial geometry,
primitive Pull, cylindrical-face Pull, trimmed-boss Pull, native-document Pull,
cone Pull and the earlier planar crash regression. The final localized numeric
diameter confirmation check passed again in the isolated trimmed-boss test.

The final native-file GDB replay exited normally, including drag, numeric input,
unsupported activation, Cancel and Undo/Redo. A breakpoint configured to fail on
entry to `BRepOffset_MakeOffset::MakeOffsetShape` was not hit. Original document
hashes and source serialization remain unchanged. `git diff --check` passes.
No full CTest suite was run.

## Later scope update — phase 9D.7D

The [remaining-gap analysis](PULL_RESIZE_GAPS.md) resolves the former full-overhang
diameter restriction and adds geometry-derived height Pull on Trim/Extend caps,
including repeated edits across wall seams and joint selection of split support
patches. Current full cylinders remain full when resized; a vanished historical
Trim plane is not restored. Open-notch and oblique-hole radial reconstruction
was already added by the rotated-rim correction at `a249519`. The original
coredump safeguards and absence of an offset fallback remain in force.
