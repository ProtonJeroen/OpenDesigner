# Closed Profile validation — phases 6C.1 / 6C.2

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
A future construction adapter must check exact closure and OCCT wire/B-Rep
validity, and fail explicitly if construction cannot be certified. No wire,
solid, Boolean, GUI or persistent feature is created by this module.

Regression target: `OpenDesignerProfileValidationTests`, CTest
`sketch-closed-profile-validation` (headless, no application/display required).
Native Fedora KDE GUI acceptance belongs to the later extrusion workflow.
Native `.odc` data/schema and existing modeling tools are unchanged.
