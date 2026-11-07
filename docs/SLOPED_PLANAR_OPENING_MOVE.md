# Phase 9F.1B: move a planar face containing cylindrical openings

Geometry first, history never. The selected entity is a plane; its elliptical
boundary does not turn this into a cylindrical-wall movement or a resize command.

## Exact reproduction and root cause

Review baseline: `4821b66`. The current supplied `hole2.odc` has SHA-256
`cd4f5c5790d2e70000b34c973e24a53e67256118fccc0ab725ff21985e2a76c6`.
This is a later private capture than the file recorded in Phase 9F.1. It contains
an inward R24 cylinder on the Z-axis line through (50,36), an unrelated R14.77 side
boss, and the selected planar top with outward normal approximately
(0.258819,0,0.965926).

The native +5 mm reproduction confirmed that planar recognition succeeds, then
reconstruction returns:

> Move Face refused at a transverse cylindrical intersection. Local planar
> reconstruction refused: an adjacent support changes cross section along the
> requested direction. No certified unaffected stock slab with exterior features
> was found.

Neither inner wires nor elliptical curves were rejected by recognition. The
constant-section planar sweep cannot translate the old trimmed face unchanged:
the stationary cylinder intersects the new plane in a different ellipse. The
existing exterior-feature routes did not cover this case. The transverse-cylinder
guard then refused before OCCT's previously crashing global offset intersection.
Removing that guard would not be a reconstruction fix.

## Reused current-B-Rep reconstruction

Extend the existing planar-rim material utility with a planar-support edit:

1. Resolve the current plane patch, its adjacent inward cylindrical opening and
   physical planar rim supports. Use the existing fill/removal certificate to
   prove the void is empty, has complete wall coverage, terminates on actual
   exterior supports and heals to one valid manifold solid.
2. Move that healed planar support using the existing planar engine. If another
   certified opening remains adjacent, the same route resolves it from the new
   current B-Rep; no feature count, primitive data or creation recipe is required.
3. Translate only the selected physical rim plane. Rebuild the clipped void using
   the **unchanged cylinder axis and radius**, and cut it from the moved healed
   body. Boolean intersections regenerate the ellipse and adjacent trimming edges.
4. Require monotonic signed growth/shrink of the healed body and complete
   containment of the new void in the certified surrounding material. Certify the
   resulting material outside the edit region, retained trimmed faces, manifold
   topology, survival of the moved plane and stationary cylinder, and permitted
   analytic supports. A failed certificate returns a diagnostic and no candidate.

Healing and re-cutting are internal computations on isolated B-Reps, not document
commands or stored feature history. The user still commits one planar Move Face
transaction. The existing transverse-cylinder guard remains in place if this
route cannot certify a reconstruction.

Planar recognition now checks the selected plane's membership, solid validity and
complexity budget independently of unrelated surface types. The global offset
route retains its existing supporting-surface budget as a reconstruction guard;
that restriction no longer makes the selected plane itself unrecognizable.
A regression distinguishes recognizing a planar cap next to a sphere from the
unsupported spherical-neighbor reconstruction.

## Focused regression

```
cmake --build build --target OpenDesigner OpenDesignerMoveFaceTests OpenDesignerDeleteFaceTests -j2
build/OpenDesignerMoveFaceTests --sloped-plane /absolute/path/hole2.odc
ctest --test-dir build -C Release -R '^sloped-planar-opening-move$' --output-on-failure
```

The default CTest creates a portable model. The optional path opens the exact
private native document read-only. Tests independently reconstruct expected whole
material using all physical outer planes, fixed analytic boss volumes and long
fixed-axis cutters. Computational seed supports must disappear. Sampled occupancy
uses independent plane halfspaces and cylinder inequalities, not production healing
or face sweeps. The original native material must match this oracle before editing.

Coverage includes:

- Positive/negative and repeated offsets; valid closed solids, independent volume
  and Boolean material equality; two edge uses around the final manifold.
- Recomputed ellipse samples on both the moved plane and unchanged cylinder;
  preserved radii/axis lines for selected-hole and unrelated cylindrical supports.
- An additional independent opening and arbitrary rigidly transformed B-Reps.
- Source geometry immutability, normal-offset planar command classification,
  localized exact numeric input, actual preview material and nonselectable previews.
- Cancel and preview removal; preview/commit material agreement; exact cached
  Undo/Redo states; stable Part ID/name/color/visibility/placement, AIS/browser row
  identity, Active Part and hidden witness.
- Confirmed-only Save during preview, native result Open, history/tool/selection
  reset, and a follow-up negative planar edit on the reopened B-Rep.
- Deliberately stale primitive metadata, which cannot govern this face operation.

## Validation and remaining limits

Incremental Release builds of `OpenDesigner`, `OpenDesignerMoveFaceTests` and
`OpenDesignerDeleteFaceTests` passed. The exact native workflow passed the geometry
and session checks above. The focused CTest run passed **5/5 in 78.49 seconds**:

| Test | Result | Seconds |
| --- | --- | --- |
| `move-face` | Passed | 35.24 |
| `sloped-planar-opening-move` | Passed | 15.75 |
| `cylindrical-rotated-rim` | Passed | 13.55 |
| `move-face-crash03112026` | Passed | 9.38 |
| `delete-sloped-opening` | Passed | 4.56 |

Logs are retained under `build/phase9f1b-*`: `reproduction.log` contains the original
native refusal, `native.log` the complete native pass, `affected-build.log` the
incremental builds, and `tests.log` the five-case CTest results. All output ODCs
are temporary. The native source was checked byte-for-byte, and all four private
source SHA-256 values match this phase's initial baseline. They remain untracked.
No full CTest suite was run.

This supports normal translation of a selected planar rim support where the
existing material certificates and healed-plane reconstruction apply. It does not
add a new arbitrary-direction planar tool. Nonplanar/uncertified interacting rim
supports, blind or unproved closures, topology/contact changes the healed-plane
engine cannot certify, loss of the plane or opening, interference with another
void, and exceeded numerical/topology budgets remain diagnostic refusals. General
freeform reconstruction is not claimed. Recognition and certified edit capability
remain distinct; these limits do not imply that the selected surface is not planar
or that valid geometry is inherently impossible.

The unrelated cross-screen HiDPI platform qualification from Phase 9F is unchanged.
