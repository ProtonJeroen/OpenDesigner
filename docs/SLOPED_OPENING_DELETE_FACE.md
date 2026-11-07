# Phase 9F.1: Delete Face on sloped cylindrical openings

Geometry first, history never. Delete Face now consumes the same current planar-rim
material certificate as cylindrical Move Face and radial Pull.

## Exact native reproduction and root cause

The current private `hole2.odc` has SHA-256
`80d421dd8a5370f446cfd1a2478ceb73380db0b686b58b616dcc5d14260ea5ee`.
Its selected inward R24 wall has circular and elliptical rim segments. The Part
also contains an unrelated outward R14.77 side boss. The test selects the current
inward cylindrical face from its elliptical rims; it does not assume the older
R21 fixture or read primitive creation metadata.

Before the change, the exact native deletion returned:

> Cylindrical Delete Face: Sloped notch boundaries cannot be reconstructed safely.
> Sloped boss trims are unsupported. The selected region has no unambiguous closure
> on adjacent surfaces.

The cylindrical Delete adapter tried only the one-sided notch fill and the
boundary-boss removal certificate. The former requires axial-normal notch ends;
the latter is an outward-boss certificate. Neither could heal this sloped inward
opening. The legacy defeaturing/planar fallback also refused it. Move Face and
Pull already had `reconstructPlanarRimHole`, but Delete never called it. This was
missing reconstruction dispatch, not an unrecognizable cylinder or persistence
failure.

## Shared reconstruction improvement

Add an optional removal result to `reconstructPlanarRimHole`. It still isolates
the source, resolves the connected cylindrical patch and infers material normals
and actual oriented planar rim halfspaces. Its existing certificate requires:

- finite opposing axial bounds, with no retained computational cylinder end caps;
- an empty inferred void and complete coverage of the selected cylindrical wall;
- a valid healed solid, independently verified added material and re-cut source;
- every closing planar patch covered by an actual healed exterior support.

For deletion, return this already certified healed body before constructing a
new destination cutter. Reuse `finishCylindricalRemoval` to check outside material,
protected trimmed faces, manifold topology, removal of the selected wall and
source-to-result correspondence. Every final face must map to a retained analytic
support. The existing Move/Pull path remains unchanged when no removal result is
requested.

The Delete adapter uses this path after its existing notch/boss certificates.
Failures include the planar-rim certificate's diagnostic and return no result.
There is no file-name, Part-origin, primitive-metadata or rectangular-stock branch.
No history, serialization or preview implementation was changed.

## Focused regression

```
cmake --build build --target OpenDesigner OpenDesignerDeleteFaceTests OpenDesignerMoveFaceTests -j2
build/OpenDesignerDeleteFaceTests --sloped-opening-geometry /absolute/path/hole2.odc
build/OpenDesignerDeleteFaceTests --sloped-opening /absolute/path/hole2.odc
ctest --test-dir build -C Release -R '^delete-sloped-opening$' --output-on-failure
```

`delete-sloped-opening` uses a portable generated native-storage fixture. The
optional path replays the exact private file without modifying it. A test-only
oracle intersects all physical main-body supports, independently retains the
analytic side boss where present, and first verifies the original native material
against a long cylindrical cutter. It does not call the production local fill or
use its clipped-cylinder tool. Verification includes:

- valid closed solids; exact solid/shell/face/wire/edge/vertex counts against the
  independent oracle; two uses per edge;
- independent expected volume and Boolean material equivalence; the portable
  complete opening also has an analytic disk-area × affine axial-height check;
- sampled occupancy over the entire model, including exterior space;
- the actual side boss, an additional unrelated hole and rotated B-Rep coordinates;
- selected-face removal correspondence and immutable input geometry;
- atomic refusal with diagnostics for intersecting cylindrical supports;
- native Save/Open before deletion, Save during preview, saved result comparison,
  a second fresh-source Open/delete, preview cleanup, Cancel and one cached
  Undo/Redo transaction;
- Part ID, name, color, visibility, placement, presentation/browser identity,
  hidden witness and Active Part, with deliberately stale primitive metadata.

The Phase 9E history regression now requires sloped-rim deletion to succeed before
and after storage; it no longer accepts the old known refusal. The old corner-hole
refusal expectation is also replaced with a healed-solid oracle: multiple planar
side rims can determine a unique valid fill. Blind/unselected-bottom, incomplete
boss and foreign-face refusals remain covered.

## Validation and limits

Validation completed with incremental Release builds of the application and both
affected test executables; build logs contain no warnings/errors.

- Exact native headless reproduction failed with the refusal above before the fix.
- Exact native geometry and GUI regressions passed after the fix, including all
  three oracle variants, safe intersecting-support refusal and the saved-source
  deletion/preview/history/identity checks.
- `delete-sloped-opening` passed (7.18 seconds).
- Existing boundary deletion sessions (24.38 seconds) and legacy Delete Face
  geometry (6.49 seconds) passed. The boundary geometry run exposed the obsolete
  corner-refusal assertion; after replacing it with the independent healed-body
  oracle, its `--boundary-geometry` rerun passed. Other refusal assertions passed.
- Tightened `face-history-persistence` passed (43.00 seconds), and the shared
  Move/Pull `cylindrical-rotated-rim` regression passed (13.31 seconds).
- The exact native `hole2.odc` history/persistence case passed after consecutive
  Move/Resize operations; deletion now succeeds rather than allowing old refusal.

Logs are retained under `build/phase9f1-*`, including `reproduction.log`,
`native-geometry.log`, `native-session.log`, `delete-tests.log`,
`boundary-rerun.log`, `shared-tests.log` and `native-history.log`.
All output ODCs use temporary directories. All four supplied private source ODCs
were verified unchanged by SHA-256 and remain untracked.

This extends deletion only where the shared current-material certificate applies.
Nonplanar rim supports, interfering/ambiguous cylindrical patches, unproved
nonconvex closures, blends, missing exterior termination or exceeded numerical/
topology budgets still fail safely. Blind-hole deletion is not broadened by this
certificate; existing explicit legacy selections retain their previous scope.
Outward sloped boss trims remain outside this change. An analytic cylinder can be
recognized even when a reconstruction is unsupported; those cases are not claimed
geometrically impossible.

The cross-screen HiDPI platform limitation documented in
[Phase 9F](V049_INTEGRATION_REVIEW.md) is unchanged. No full CTest suite was repeated.
