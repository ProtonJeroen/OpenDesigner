# Interactive Trim — v0.5.6

Activate a visible, unlocked Sketch Workplane and choose **2D Modeling → Trim**
(or Trim on the 2D toolbar). Click the unwanted portion of a curve, away from
intersections. The blue preview contains the remaining geometry. OK/Enter
commits one SketchChange; Cancel/Escape restores the original geometry and
preserves history. Each confirmation performs one Trim. Another click during a
valid preview does not chain another operation.

Visible, unlocked curves of either role on the active Workplane supply crossing
boundaries. Hidden/locked curves, points and other Workplanes do not. Open
curves need an interior crossing; their original endpoint can bound a tail.
Closed curves need two distinct crossings. Tangencies alone do not bound a
removal. Overlaps, unresolved contacts, boundary picks, self-intersecting splines
and multiple curves under the pointer are refused with feedback. Infinite
Construction lines can supply boundaries but are not Trim targets.

Segments, circular/elliptical arcs, complete circles/ellipses and certified
splines retain their exact geometry and directed traversal. The first retained
fragment keeps the original entity ID; additional fragments get new IDs. Names,
roles and visibility/selection metadata are copied. Source/boundary values are
never changed before confirmation. Workplane coordinates remain local.

Interpolating spline fragments become exact coefficient B-splines, retaining
poles, weights, knots, multiplicities and parameter intervals. There is no
re-interpolation. A retained periodic path that crosses the existing seam uses
two bounded coefficient fragments on the same support. Through-point editing
continues to apply to original interpolating splines, not coefficient fragments.
Native storage already supports these values; no schema change is needed.

Profile Validation and Builder accept finite, nonrational coefficient B-spline
fragments of degree at most 3. Self-intersection checks use the trimmed domain;
area uses exact polynomial knot-span integration. Existing shared-vertex,
closure, source-edge-count, area and B-Rep checks remain mandatory, with the
existing 1e-7 Sketch/vertex policy and unchanged downstream certification limits.
Rational, higher-degree and whole closed coefficient B-spline profiles remain
unsupported. Their exact Sketch Trim may be available, but solid profile
construction explicitly refuses them. No associative source links are created.

Focused coverage lives in `sketch-trim-geometry`, `sketch-trim-sessions` and
`sketch-trim-sessions-hidpi`. Native Fedora KDE acceptance remains manual.

## Fedora KDE acceptance

Repeat at normal and 200% scaling:

1. Draw a line crossed by two Construction lines. Trim its middle, inspect both
   blue remainders, Cancel, then repeat and confirm. Check one Undo/Redo.
2. Trim a line tail with one crossing. Try no crossing, a tangent, an overlap and
   a click at an intersection: expect feedback and no partial modification.
3. Trim circles and rotated ellipses on each side of their parameter seam.
   Repeat with clockwise/major arcs; check the retained route and endpoints.
4. Trim open and periodic interpolating splines. Inspect exact coefficient
   fragments, including a retained path across the seam. Close a supported
   cubic fragment with a line and use the existing Profile Builder workflow.
5. Move/rotate a Workplane, look normal to it and repeat. Hide/lock targets and
   boundaries; verify they are unavailable. Cancel by hiding the Workplane or
   switching tools. Check hover and picking at both scales.
6. Save during preview and reopen a copy: only confirmed geometry is stored.
   Confirm, save/reopen, then inspect exact values, roles and IDs. Verify OK,
   Enter, Cancel and Escape leave no preview or command state behind.

The consolidated [Fedora KDE/Wayland acceptance checklist](V056_KDE_ACCEPTANCE.md)
covers 100% and 200% scaling. Manual acceptance is **pending**; automated
xcb/HiDPI sessions do not certify native desktop behavior.
