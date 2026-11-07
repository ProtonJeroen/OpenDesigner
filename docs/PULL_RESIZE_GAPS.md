# Phase 9D.7D — remaining Pull/Resize gaps

Geometry first, history never. All capabilities below come from the current
B-Rep's analytic supports, topology, adjacency and material. Primitive fields
are derived UI caches, not evidence that an operation is available.

## Gap review

| Requested edit | Previous gap | Current result |
| --- | --- | --- |
| Extend boss diameter | Radius analysis counted the support and underside as separate axial ends, then required a fully covered base. Recognition/certificate restrictions, not geometric ambiguity. | Reuse the boundary-boss reconstruction: full cylinder, positive connected attachment, exact feature material and protected surrounding surfaces. |
| Trim/Extend boss height | General B-Rep planar caps had no Pull height handle. The local planar sweep already existed; repeated growth/shrink also exposed wall seams and split caps. | Derive the current cap, axis, attachment and height; expose an absolute-height handle and reuse the existing local planar sweep. Sweep connected cap patches and include only swept regions on invariant neighboring supports across seams. |
| Open-notch diameter | Already working at `a249519`, including oblique/corner planar rims. | No replacement algorithm. Retain the planar-rim certificate; add repeated confirmed numeric/UI coverage. |
| Repeated Pull after confirmation | No persistent-history mechanism was needed. Missing current-geometry height references and a seam-sensitive sweep caused the observed gaps. | Reanalyze every current wall/cap. Repeated radius/height edits, split-face joint selection, Cancel and cached Undo/Redo are covered. |

None of the ordinary cases above is inherently ambiguous. A partial attachment,
a split face or an earlier modeling operation is not a reason to reject analytic
cylindrical geometry.

## Small shared corrections

The existing boundary-boss reconstruction accepts an optional requested radius
and can report its independently certified attachment axis. The existing
full/trim material comparison determines current behavior: a full cylinder stays
full; an actual planar-trimmed cylinder retains its current cut. Shrinking a Trim
boss until its cut disappears leaves a full cylinder. Later growth therefore
uses that current full geometry and may create a valid overhang; no historical
Trim plane or mode is invented.

The cap reference grows its connected planar patch, finds a unique adjacent
cylindrical support, verifies its material/attachment using the same boss
certificate, and requires the selected cap opposite that attachment. A support
plane or exposed underside cannot silently become a height handle. Actual cap
coverage must lie inside the certified cylinder. Radius and attachment remain
fixed during height adjustment.

Height changes call `BRepReconstruction::movePlanarLocal` directly. The utility
now sweeps all connected coplanar cap patches, verifies the exact swept material,
and permits a sweep to cross seams on the same invariant analytic wall. Only
wall/plane regions actually intersecting the sweep become affected. Outside
material, unrelated trimmed patches, complete translated cap coverage, valid
solid/manifold checks and input isolation remain mandatory. There is no offset
fallback.

Existing Pull sessions supply drag, localized absolute diameter/height input,
preview rollback, OK/Cancel, cached history and stable Part identity. Temporary
height-handle state is derived on activation and cleared with the existing tools.
Jointly selected fragments of one connected cap or cylindrical support patch use
the same whole-patch certificate.

## Unsupported versus ambiguous versus invalid

- **Unsupported reconstruction:** curved/annular or multiple disconnected attachment patches,
  multiple side cuts outside the boss certificate, oblique/nonconstant-section
  boss caps, blends, or interactions not certified by the local material rules.
  Some such shapes may admit valid edits; this implementation does not claim
  they are geometrically impossible or unrecognizable.
- **Unsupported joint tool operation:** separate independent cylindrical
  features, mixed cap/wall parameter selection, and general multi-Part
  cylindrical Pull are not yet composed into one batch reconstruction. Connected
  split fragments of one support are handled. Existing grouped primitive-box
  Pull is retained. Missing grouping support is not geometric ambiguity.
- **Ambiguous reference:** a selection that does not determine a unique boss
  axis/attachment or combines incompatible absolute-height references needs a
  more specific selection or an explicit joint parameter definition. Multiple
  adjacent supports alone are not proof of inherent geometric ambiguity.
- **Invalid/degenerate request:** nonpositive height/radius, loss of positive-area
  attachment, tangent-only connection, self-interference or loss of protected
  material are refused atomically with diagnostics. A rejected preview retains
  the last validated result; Cancel restores the immutable source.

Existing numerical/topology budgets remain safety limits. No stock shape,
creation method, cylinder count or primitive provenance is used as recognition.

## Focused validation

New modes:

```
build/OpenDesignerCylindricalMoveTests --pull-gaps
build/OpenDesignerPullTests --boundary-gaps
```

Geometry checks use independent box/cylinder material oracles, analytic volumes,
sampled occupancy, rotated B-Reps, input serialization and invalid/disconnected
requests. UI cases cover Extend/Trim height, Extend diameter, repeated notch
diameter, joint split caps/walls, drag, localized numeric confirmation, invalid
preview recovery, Cancel, exact cached Undo/Redo, stale metadata independent
recognition, Part identity/appearance/placement and an unaffected Part.

Validation completed: incremental application and affected test targets built.
Both new focused modes passed, along with the directly affected `--radius`,
`--local` and `--trimmed-boss-pull` regressions. The existing post-Trim test now
uses an interacting annular support for its unsupported-activation check;
overhanging full bosses are supported. Previously passing notch/oblique-rim
geometry results were reused; their algorithms were not reimplemented. No full
CTest suite was run. Private ODC files were neither modified nor committed.
