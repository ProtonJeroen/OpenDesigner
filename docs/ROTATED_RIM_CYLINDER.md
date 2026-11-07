# Geometry-driven cylinder regression — rotated top

## Exact document and root cause

`hole2.odc` is read without writing or saving it. Its original SHA-256 is
`4d3e27583bcf43e0592b50d12c3e57621bd4bc00a8ac685009d60a3f15b4681b`.
The Part is `slot-4`; the selected surface is an inward R21 cylinder on the
Z-axis line through (84,36). Its carrier location is (84,36,-60), with trimmed
V range [60,183.911…]. One opening is circular and the other elliptical.
The top's outward normal is (sin 15°,0,cos 15°); the right side is also oblique.

The old rectangular-stock certificates could not certify these retained planes.
The subsequent local hole path required both rim planes normal to the cylinder
axis, circular boundary edges and a constant cylindrical span. Its notch fallback
reported “Sloped notch boundaries cannot be reconstructed safely.” Pull/Resize
similarly reported “Sloped cylindrical trims cannot be resized safely.” The
analytic surface was still a cylinder: those were reconstruction restrictions,
incorrectly surfaced as an unsupported-cylinder diagnosis.

## Shared current-material correction

`reconstructPlanarRimHole` is a shared fallback for Move Face and radial Pull.
Use the existing isolated neighborhood and inward material normals, gather actual
oriented planar rim supports, and bound a cylinder axially from their analytic
intersection extrema. Clip against those planes. Any computational cylinder end
cap left in the result rejects the construction; all bounds must be physical.
There is no rectangular stock, creation metadata, feature history, cylinder-count
rule, world-axis test or boundary-curve classification.

Certify an empty current void with complete mantle coverage, fill only that
volume, and independently check added material plus re-cut to the original.
Every closing plane patch must cover an actual healed exterior surface.
Subtract the destination cylinder clipped by the same current supports. Actual
intersections with the retained material determine its new openings; planar
regions actually intersected by the cutter may change trim. Require physical
exterior terminations, exact removed material, one normalized manifold solid,
unchanged outside material and preserved unrelated trimmed faces using the
existing shared utilities. No OCCT offset reconstruction is invoked.

Lateral movement preserves radius and the translated axis line/direction.
Physical axial extent follows the fixed surrounding planes and may change.
Radial Pull uses the same certificate with a new radius and unchanged axis and
planes. Valid planar open-notch resize and corner transitions also pass this
certificate; earlier “unsupported by class” assertions now verify exact material.
Failures identify the missing geometric/material certificate and do not claim
that the cylindrical support is unrecognizable.

## Focused regression

`cylindrical-rotated-rim` runs a portable native-storage equivalent. Exact private
replay uses:

```
build/OpenDesignerMoveFaceTests --rotated-hole /absolute/path/hole2.odc
```

The oracle is independent: intersect ALL retained planes into a bounded
polyhedron, verify that no seed support survives, then use a long cylindrical
cutter. It is test geometry, not a production stock reconstruction. Check exact
material equivalence, analytic disk-area × axis-height void volume, independent
halfspace/radial occupancy, radius/axis preservation, manifold edge uses and
unaffected trimmed face coverage. Exercise repeated movement, rotated B-Rep
coordinates, radial resize and restoration, and preservation of another hole and atomic axial/interacting-feature
refusal. Sessions cover blue non-selectable preview, localized numeric input,
Escape, preview/commit agreement, exact cached Undo/Redo, fresh selection owners,
Part UUID/placement/appearance/browser identity and an unrelated Part. Actual
Pull/Resize context activation and radial preview/Cancel are checked as well.
Pure geometry operations preserve source serialization. After rendering, analytic
material is checked independently because triangulation caches may be populated.
The original native file must remain byte-for-byte unchanged.

## Reconstruction limits

This certificate requires a connected inward cylindrical patch whose current
void is exactly bounded by the material halfspaces of its adjacent planar rims,
including opposing axial bounds. Split/coplanar patches are allowed. A nonconvex
closure not proved by that intersection, curved neighbor supports, blends, blind
regions, interfering features, empty/degenerate clipped intersections or exceeded numerical/
topology budgets get specific atomic refusals. These are reconstruction limits,
not limits on recognizing an analytic cylinder. Existing boss and Delete Face
certificates retain their scope; this change does not add general freeform
reconstruction to those operations.

## Validation result

Incremental application, cylindrical geometry and Move Face session targets
built successfully. The focused exact `hole2.odc` replay and portable
`cylindrical-rotated-rim` regression both passed all geometry and UI assertions.
The directly affected `--radius` and `--notch` geometry regressions also passed,
including independent trim material checks for the newly certified notch resize
and corner return move. No full CTest suite or unrelated passing suite was run.
The original `hole2.odc` hash is unchanged and the file remains untracked.


## Phase 9F.1: Delete Face follow-up

[Sloped-opening Delete Face](SLOPED_OPENING_DELETE_FACE.md) now consumes this same
planar-rim fill through the existing cylindrical-removal validator. The current
private document's R24 opening and unrelated side boss are tested independently,
including Save/Open, Cancel, cached history and Part identity. Curved/interacting
or otherwise unproved closures remain diagnostic refusals.


## Phase 9F.1B: moving the planar rim support

[Planar opening Move Face](SLOPED_PLANAR_OPENING_MOVE.md) now moves the selected
planar support while keeping cylindrical holes stationary. It updates the actual
rim plane and recomputes elliptical intersections using the same current-material
certificate. This is a planar normal-offset command, distinct from moving or
resizing the cylindrical wall.
