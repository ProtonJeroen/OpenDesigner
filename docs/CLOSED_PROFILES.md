# Closed Profile validation and construction — phases 6C / 6D.0

`SketchProfileValidation::validate(entities, workplaneId, tolerance)` analyzes local
XY Profile geometry, including hidden/locked entities. Construction geometry and
other Workplanes are ignored. It does not modify entities, documents or history,
and has no widget, presentation or storage dependency. Default tolerance: 1e-7 mm.

Supported boundaries are simple Segment/Arc loops (including all-Arc loops) and
standalone full Circles. Arc sweeps and short/long routes remain analytic; source
order and CW/CCW direction do not affect validity. Endpoint proximity clusters
must fit entirely within tolerance. No endpoint is moved. Existing analytic
Sketch intersections reject duplicates, overlaps, branches and crossings.
Contacts between distinct boundaries, including tangency, are refused. Ellipse
and EllipticalArc profiles remain explicitly Unsupported.

Valid output includes:

- `contours`: ordered curve values, original entity IDs, per-curve `reversed`
  traversal flags, analytic signed area and orientation. Circles use canonical
  CCW traversal. Arcs may be returned through the existing `reversedArc()` helper.
- `parent` and `depth` on each contour: even depth is material, odd depth a hole.
- `regions`: an `outer` contour index and direct `holes` indices. A depth-two
  island is a separate region, rather than a hole attached to the depth-zero outer.

For example, rectangle → circle → smaller circle returns two regions: the
rectangle with its circular hole, and the inner circular island. Two disjoint
rectangles each containing a circle return two regions, each with one hole.
Containment uses analytic circle distance or analytic ray intersections. Rays
through vertices or support-circle tangencies are avoided; unresolved numerical
cases return Unsupported. There is no tessellation or curve sampling.

Invalid/Unsupported results have readable diagnostic codes, source IDs and a
location where available. They expose no partial contours or regions.

## Analysis is not an OCCT wire certificate

`Valid` means a supported, unambiguous topology within the requested Sketch
connection tolerance. It does **not** mean a wire can already be extruded.
`wireReadiness` is always `NotCertified`, accompanied by a `WireNotCertified`
entry in `constructionDiagnostics`, separate from analysis error `diagnostics`.
Each contour reports `maximumEndpointGap`; nonzero gaps additionally produce
`TolerantClosure`, even when they are smaller than the Sketch tolerance. Values
are never silently healed, including small floating-point Arc endpoint gaps.
The separate construction adapter below checks closure and OCCT wire/B-Rep
validity, failing explicitly if construction cannot be certified. No wire,
solid, Boolean, GUI or persistent feature is created by this module.

Regression target: `OpenDesignerProfileValidationTests`, CTest
`sketch-closed-profile-validation` (headless, no application/display required).
Native Fedora KDE GUI acceptance belongs to the later extrusion workflow.
Native `.odc` data/schema and existing modeling tools are unchanged.

## Exact wire and planar face construction (6D.0)

`SketchProfileBuilder::build(validation, frame, policy)` converts a successful
analysis into certified analytic wires and planar faces. `connectionTolerance`
is recorded by successful validation and bounds the construction policy; this is
analysis metadata, not persisted document data. The adapter reuses `validate()`
to recheck the public result's source geometry, contour membership and material
regions, rather than trusting a mutable status flag or duplicating intersections.

Segment supports remain lines; Arc/Circle supports remain circles. Explicit
edge ranges preserve signed traversal, seam crossings and short/long arc routes.
`Sketch::worldPoint()` and the Workplane's local X/Y axes place all geometry in
3D. Right- or left-handed frame axes are respected; the construction plane normal
is X cross Y. No triangulation, approximation, ShapeFix or automatic healing is
used. Each source curve yields one edge, with its original entity ID.

### Bounded closure policy

- `Policy::maximumVertexTolerance` defaults to 1e-7 mm. It must be finite, at
  least OCCT `Precision::Confusion()` and no larger than the recorded Sketch
  connection tolerance. A stricter construction limit can refuse an otherwise
  valid analysis. No tolerance is silently increased.
- Each junction vertex is anchored at the outgoing source curve's evaluated
  start. The incoming curve retains its exact support and endpoint parameter;
  both use the same topological vertex with **exactly** the policy tolerance.
  Source endpoints, curve parameters and Sketch IDs are never rewritten.
- Both local and world endpoint gaps must be within the construction limit.
  Wire output reports the maximum world gap; successful result metadata reports
  the effective vertex tolerance. This is certified **bounded-tolerance OCCT
  closure**, not a claim that all distinct analytic endpoints coincide exactly.
- Within each contour, distinct junction vertices within twice the limit, or a
  full Circle radius within twice the limit, are refused as construction ambiguity. World-coordinate
  ULP must remain below one-sixteenth of the limit; unresolved placement fails.

Certification checks oriented shared vertex identity, endpoint/curve residuals,
analytic support types, edge count, `BRep_Tool::IsClosed`, `BRepCheck_Wire` and
`BRepCheck_Analyzer`. Vertex, edge and face tolerances must stay within policy.

Each material region produces one planar face: an outward CCW wire and its
reversed direct-hole wires relative to local XY. Islands remain separate faces.
The adapter checks face/UV wire closure, nesting and orientation using
`BRepCheck_Face`/`BRepCheck_Wire`, actual plane, boundary identity/count and area
against the independently validated analytic region area. The area error bound
is `8 * tolerance * boundaryLength + 256 * doubleEpsilon * abs(expectedArea)`;
explicit nesting and boundary checks remain mandatory, irrespective of area.

Success returns source-indexed wires/edges and region-indexed faces with outer,
hole and entity references and measured areas. Any failure returns structured
codes/messages and available contour/region/ID context, with **no partial wire
or face output**. No document, selection or Undo/Redo state is accessed. `.odc`
remains unchanged. Elliptical profiles, solids, Extrude/Cut and GUI are excluded.

Headless regression target: `OpenDesignerProfileBuilderTests`, CTest
`sketch-profile-wire-face-construction`. Tests include exact areas, actual hole
classification, CW/CCW routes, transformations, bounded gaps and atomic refusal.
