# Revolve geometry — v0.4.8, phase 8C

`SketchProfileRevolution::revolve(certifiedFaces, worldAxis, signedDegrees)` is
the headless rotational sweep boundary. It accepts a `SketchProfileBuilder::Result`
and `gp_Ax1`, and returns `SketchProfileSweep::Result`: success, world-space solid
or solid compound, solid count, volume and structured diagnostics (with an optional
input face index). Failure returns no shape or partial result. Inputs are read-only;
there is no Part, selection, command panel, history or document mutation here.

## Supported geometry

- Reuse `SketchProfileValidation::validate` and `SketchProfileBuilder::build` for
  closed line/circular-arc/circle profiles, holes, islands and separate regions.
  Their existing unsupported-curve and numerical/tolerance policies still apply.
- Arbitrarily oriented/translated Workplanes and world axes are supported. Supply
  the axis in the same world coordinates as builder faces; do not transform the
  builder result a second time. The axis must lie in the common profile plane.
- Signed angles satisfy `1 <= abs(degrees) <= 360`, including fractional degrees.
  Positive follows the right-hand rule; negative reverses the axis direction.
  Degrees are converted directly to radians without application-level rounding.
- Partial sweeps retain end caps. Full sweeps close around the axis without caps.
  Valid axis-boundary edges and poles are supported (cylinders, cones, spheres).
  Off-axis circular profiles produce tori; profile holes remain voids, including
  closed cavity shells on full revolutions.
- Multiple regions on the same side of the axis produce separate solids in one
  compound, preserving islands. No implicit Unite or temporary Part is created.

## Certification and safe refusal

The kernel uses OCCT's copying
[`BRepPrimAPI_MakeRevol`](https://www.occt3d.com/dev/doc/refman/html/class_b_rep_prim_a_p_i___make_revol.html)
with a nonnegative radian angle. It does not construct another profile builder or
Boolean engine. Shared `BooleanGeometry::certifiedSolid` is the unchanged former
extrusion certificate: B-Rep validity, closed shells, positive volume per solid,
and an outward infinite-point classification. Extrusion keeps its existing API
through aliases to the shared sweep result/diagnostic types.

Analytic meridian bounds check entire curves rather than sampling vertices.
Crossing-axis profiles and isolated interior tangencies to the axis are refused;
in particular, pinched horn tori are explicitly rejected even when OCCT accepts
their topology. Axis configurations outside the profile plane and regions on
opposite sides are conservatively unsupported, even if a particular partial
sweep might not overlap. There is no clipping, healing, profile splitting or
automatic union to repair such inputs.

OCCT `BOPAlgo_ArgumentAnalyzer` checks self-interference of the complete result,
including interference between regions. The aggregate must contain exactly one
certified solid per input face, with volume matching
`sum(face area * absolute centroid radius * absolute angle in radians)` within
`max(1e-7 mm³, expected volume * 1e-9)`. This relation applies to the supported
one-sided meridian profiles, including holes and boundary contact. Axis and result
world coordinates must resolve `Precision::Confusion()/16`, following the
existing extrusion numerical policy. Numerically unsafe cases and kernel failures
return diagnostics rather than changing any source geometry.

The API uses the existing core geometry/result conventions (`QString` diagnostics);
it requires no widgets or event loop. Zero-length axes cannot be represented by
`gp_Ax1`/`gp_Dir` and must be refused when a future caller constructs the axis.

## Later integration boundary

Phase 8D uses this certified world-space tool for New Solid. Future Add/Remove
integration must wait for manual phase 8D acceptance. Keep Add/Remove target selection, placement inversion,
Part identity/properties, preview and Undo/Redo in the command layer. Extract the
existing material application as planned in
[Shared modeling architecture](SHARED_MODELING_ARCHITECTURE.md); do not copy it
into this sweep kernel. Phase 8C introduces no Revolve UI or document feature.

## Validation — 2026-11-01

Only `OpenDesignerProfileRevolutionTests`, `OpenDesignerProfileExtrusionTests`,
`OpenDesignerProfileBuilderTests` and `OpenDesignerProfileValidationTests` were
built incrementally in the existing Release `build` directory. Their four CTests
passed (4/4, 1.89 s). New coverage includes ±1°/±360°, fractional and partial
angles, signed direction via centroid, full/partial cap topology, moved/rotated
axes and Workplanes, negative-side profiles, cylinder/cone/sphere axis contact,
tori, holes, islands, multiple regions, overlapping sweeps, invalid/nonplanar
faces, unsupported/crossing/tangent axes, numerical refusal and unchanged source
serialization. The first rejection run exposed OCCT accepting a pinched horn
torus; the explicit interior-tangency guard resolves that case without rejecting
ordinary axis poles. `git diff --check` passed. No UI, stress or full-suite tests
were run.
