# General Move Face (v0.3.6)

v0.5.3 adds [direction references](#direction-from-geometric-references-v053);
v0.5.4 adds [Move Face by Points](#move-face-by-points-v054).

## Cylindrical features (v0.4.9, phases 9D–9D.7B)

The existing **3D Modeling → Move Face** command also accepts the cylindrical
wall of one selected complete circular through-hole, supported open cylindrical
notch, or attached boss on
certified support geometry. The rectangular-stock paths allow up to eight
independent parallel cylindrical features. The local interior-boss path below
does not use those pattern/count rules. Unselected features remain unchanged. A supported
cylindrical face also offers Move Face in its Face context
menu. The selected face determines the target, independently of Active Part.
Planar Move Face keeps its normal-offset workflow described below.

Choose an explicit **Part X/Y/Z projected into lateral plane** reference in the
Command Panel. References parallel to the cylinder axis are disabled. Set the
**Direction angle** to rotate that reference in the lateral plane; positive angles
follow the right-hand rule about the displayed cylinder axis (Part coordinates).
The orange arrow shows the resulting positive direction. Drag it or type signed
mm in the distance field. References and angles are relative to the Part; signed
distance is measured in world mm, including for rotated/scaled Parts. No lateral
direction is chosen automatically. Linear snapping and Shift bypass still apply.

Bosses default visibly to **Extend**, which keeps the full round cylinder and
valid overhangs. **Trim** clips it against the original stock side surfaces and
can produce planar truncated walls. The choice stays fixed during the command.
Through-holes and notches hide this selector; they can open into a side notch or leave the
stock completely, healing the original hole. Both policies preserve valid
connected material; completely detached or clipped-away bosses are refused.

Direction, distance and policy changes show the actual rebuilt blue translucent
solid. Invalid geometry restores the source display and disables OK. Correct the
parameters or cancel with Esc/Cancel; Enter/OK confirms through the existing
session. Blind selected holes, blends and interacting/touching edits remain
unsupported. Existing boundary paths have rectangular-stock/parallel-feature
limits; a local fallback supports repeated movement of a trimmed or overhanging
boss across one planar side boundary.
Moving into an unselected feature is refused.
Geometry calculation remains synchronous. See the [phase plan](CYLINDRICAL_MOVE_PLAN.md).
After a boss crosses one planar side boundary, select its current cylindrical
wall to move it farther outward, along the boundary, or back inward to restore a
fully supported boss. Trim clips the cylinder against the current healed planar
support; Extend retains the full cylinder with a certified attachment. Each move
reads the current B-Rep, including split walls, without remembered feature state.
Corners, multiple side boundaries, sloped/curved trims, blends and interacting
features remain outside this fallback. See [repeated boss reconstruction](REPEATED_BOUNDARY_BOSS_MOVE.md).
A separated complete through-hole remains editable: the
actual exterior boss material is preserved without re-recognizing it as an
interior boss. Unaffected stock side planes can also move when a through-hole's
physical rim planes certify the stock slab and the edit stays clear of exterior
material. Interface planes and interacting edits remain conditional/refused.
An unchanged complete boss can also move repeatedly after another hole becomes
an open notch. This local path checks the actual cap, rims and planar support;
it does not require the whole Part to match rectangular stock or a cylinder-count
pattern. The old/new footprints must remain fully supported and clear of feature
trims. Trim and Extend coincide for this interior case. Other boundary cases use
the existing certified policy paths. See the [local reconstruction strategy](GEOMETRY_DRIVEN_MOVE_FACE.md).
An open cylindrical notch through one planar exterior side can be selected and
moved again. Move it farther outward, along the edge, or back inside to restore a
complete circular hole. Each operation infers its axis, partial rims and current
material-side boundary from the B-Rep. A restored hole can reopen on another side;
no original feature position or Trim history is retained. Complete exit heals
the opening and leaves no cylindrical wall to select. The shared planar-rim
certificate also accepts valid corner/multiple-side
transitions when the current planes and material prove them. Curved closures,
blind regions and interacting features still need a valid reconstruction and are
refused atomically when uncertified. See [open-notch certificates and tests](OPEN_CYLINDRICAL_NOTCH_MOVE.md).
A rotated top face does not change cylindrical identity. Hole movement and radial
Pull can reconstruct against oblique planar rims, including an elliptical upper
opening and circular lower opening. Radius and axis direction remain fixed during
lateral movement; actual depth and intersection curves follow the unchanged
surrounding planes. No rectangular-stock, rim-curve-type or cylinder-count rule
is used by this local certificate. See [exact rotated-rim regression](ROTATED_RIM_CYLINDER.md).

Automated session checks cover normal/HiDPI; manual Fedora KDE acceptance and
phase 9E lifecycle/persistence coverage remain pending.

## Planar workflow

Choose **Modeling > Move Face**, then click one planar face. A preselected face
can also be used, including through the viewport Face context menu. The visible
Faces selection mode keeps Boolean/GeneralBRep faces selectable even with part
transform tools enabled; see [Selection/context menus](SELECTION_CONTEXT.md). The **orange** arrow points along the face's outward normal.
Drag it or type a signed displacement in millimeters: positive moves outward,
negative inward. This is a relative displacement, not an absolute primitive
height or diameter. Existing linear snapping and Shift bypass apply.

A translucent solid previews the result. **OK/Enter** confirms once;
**Cancel/Esc**, closing the parameter tool window or switching modeling tools
abandons the candidate. Re-grabbing the orange arrow continues the same session.
An invalid distance restores the original display, disables OK and cannot be
confirmed with Enter. Correct the value or cancel. A zero offset adds no history.
A normal near the camera direction may not be draggable; exact input remains
available, or orbit the camera first.

Move Face changes geometry in the existing part slot. Move Part translates the
whole part; Primitive Pull edits recognized primitive parameters. They remain
separate operations. After a nonzero Move Face, the edited part becomes a
GeneralBRep and loses primitive Pull eligibility. This prevents later Box bounds
rebuilding or primitive regeneration from overwriting the edited topology.
Undo restores the original shape, kind and primitive parameters; Redo restores
the cached direct-edit result. Names, colors, visibility, presentation identity
and Structure Browser row IDs are preserved. Confirmation replaces obsolete face
owners with a whole-part selection; Cancel restores the original face owners.

## Direction from geometric references (v0.5.3)

The Move Face panel of a planar Face has a **Direction** selector:

| Direction | Source | Resolves to |
| --- | --- | --- |
| Face normal (default) | the moved Face | its outward normal (unchanged behaviour) |
| Edge / Sketch line / Axis / Face | a straight Edge (edge orientation), a Sketch Segment (start → end) or Infinite Line, a construction Axis, a planar Face (outward normal) or a Workplane (normal) | the reference direction |
| Point to Point | two of: construction Point, Part Vertex, Sketch point (endpoint, midpoint, center, construction point, intersection) | the unit vector from the first to the second point |

Selecting a non-default source arms picking; **Pick direction reference**
re-arms it (Point to Point restarts with a new first point). Picking uses the
shared reference picker of [Workplane references](WORKPLANE_REFERENCES.md):
Sketch points use the same snap targets and pixel radius as Sketch drawing.
**Reverse direction** flips the resolved direction. The orange arrow shows the
direction; the typed value is the signed distance **along that direction**.

The reference is independent of the moved Face: it may belong to another Part,
a Workplane or a Sketch, and it is not edited. It is resolved in world
coordinates (Part placement and Workplane frames applied) on every preview and
again on OK. A Face/Edge/Vertex whose topology has since been rebuilt is stale
and refused ("… no longer exists in the current geometry …"); it is never
rebound to similar geometry.

Geometry: a planar Face translated by `s·d` produces the same solid as offsetting
its plane by `s·(d·n)` along the outward normal `n` (tangential components do not
change a plane). Move Face therefore uses the existing certified planar path
(`MoveFaceGeometry::move`, or `EdgeBlendFeatures::move` with Keep ON/OFF) with
the projected distance; certification, validation and Keep rules are unchanged.

Refused atomically (no geometry, selection or history change; the session stays
open): a direction with `|d·n| < 1e-9` ("parallel to the Face"), stale, deleted,
hidden or locked references, coincident Point-to-Point points, references with
no unique direction (curved non-circular edges, free-form faces, Vertex/Point as
a direction), and any direction reference on cylindrical hole/notch/boss walls,
which keep their lateral reference controls.

Tests: `workplane-reference-sessions` (+`-hidpi`) checks every source, Reverse
and exact distance against independent `MoveFaceGeometry` oracles, OK/Undo/Redo,
Part identity, real viewport picks, parallel/stale/cylindrical refusal.

## Move Face by Points (v0.5.4)

The Direction selector of a planar Face has a fourth entry, **By Points:
start A → end B**. Instead of a direction plus a typed distance, the user picks
a start point **A** and an end point **B**; the displacement is the full vector
`B − A`. There is no distance input (the value field is read-only and the
orange arrow cannot be dragged) and no Reverse: the pick order defines the
direction.

| Item | Behaviour |
| --- | --- |
| Point sources | Part Vertex, construction Point, Sketch point (endpoint, midpoint, center, construction point, intersection) and the center of a circular Part Edge — the point references the shared resolver already supports. Straight Edges, Axes, Faces and Workplanes are not points and are refused. |
| Picking | Choosing *By Points* arms picking. The first pick sets A, the second B; the preview appears after the second pick. Every further pick **replaces B**. **Pick start point again** discards A and B and restarts with A. Picking uses the shared reference picker (Sketch points use the Sketch snap targets and radius). |
| Panel (read-only) | A and B, the vector length `|AB|` and the resulting offset along the Face normal `(B − A)·n`, both in mm with six decimals; the value field shows `|AB|`. |
| Commit | OK/Enter commits exactly one Undo step; Cancel/Escape commits none. A and B are re-resolved from the current document on every preview and once more on OK. |

Geometry: only the normal component moves a planar Face. The command runs the
existing certified planar path (`MoveFaceGeometry::move`, or
`EdgeBlendFeatures::move` with Keep ON/OFF when certified blends exist) with
the normal offset `(B − A)·n`; tangential components of `B − A` do not change a
plane. Consequently, **if A lies on the moved Face, the Face ends up in the
plane through B** (parallel to its original plane). If A does not lie on the
Face, the Face moves by the same normal offset as the A→B vector, wherever A
and B are. Keep ON/OFF, certification, validation and Part identity rules are
unchanged.

Refused atomically, with a diagnostic and no geometry, selection or history
change (the session stays open so another point can be picked):

- A and B coincide (`|B − A| < 1e-6` mm);
- `|(B − A)·n| < 1e-6` mm: the vector is parallel to the Face;
- a stale point (Vertex/Edge rebuilt by a later edit: "… no longer exists in
  the current geometry …"), a deleted Point or Sketch entity, or a hidden or
  locked source — including Sketch points while **View → Show Workplanes** is
  off (existing hidden-reference diagnostic);
- a straight Edge as a point ("Only circular Edges supply a point");
- Face types outside the existing direction-reference rules: cylindrical
  hole, notch and boss walls keep their lateral reference controls, and By
  Points is not offered for them.

Tests: `move-face-by-points-sessions` (+`-hidpi`): A on the Face lands in the
plane through B (dimensions checked); an oblique vector moves by `(B − A)·n`;
every point source as A and as B against independent `MoveFaceGeometry`
oracles; real viewport picks of A and B, replacing B and Pick start point
again; refusals (coincident, parallel, straight Edge, single point, hidden
Point, Sketch point on hidden Workplanes, stale Vertex, deleted Point,
cylindrical wall) without mutation or history; Keep ON/OFF on a Face adjacent
to a certified Fillet equals `EdgeBlendFeatures::move`; Undo/Redo and Save/Open.

Manual acceptance (Fedora KDE/Wayland, 100 % and `QT_SCALE_FACTOR=2`):

1. Box top Face → Move Face → *By Points*. Pick a top corner Vertex (A), then a
   Sketch circle center on a vertical Workplane (B): the preview top lies in
   the plane through B; the panel shows `|AB|` and the normal offset; OK gives
   one Undo step; Undo/Redo; Save and reopen.
2. Pick a further point: B is replaced and the preview updates. Click *Pick
   start point again*: A and B are cleared.
3. Pick A and B at equal height (vector parallel to the top): refused, no
   preview. Pick the same point twice: refused.
4. On a box with a Fillet next to the top Face, By Points with Keep ON and OFF.
5. 200 %: picking Vertex, Point, Sketch point and circular Edge center hits the
   intended point; the arrow and blue preview render correctly.

## Planar faces containing cylindrical openings (Phase 9F.1B)

A planar face remains recognizable when its boundary contains inner wires,
ellipses or other intersection curves. Selected-plane recognition is independent
of unrelated surface types; reconstruction then proves its actual local supports.
For certified planar-rim openings, Move Face heals the current void internally,
moves the plane using existing planar reconstruction, and re-cuts the stationary
cylinder against the updated rim plane. Its radius/axis stay fixed while the
elliptical opening and adjacent edges are recomputed. Multiple certified openings,
repeated signed normal offsets, preview, Cancel, cached history and Save/Open are
covered. No source pattern, primitive metadata or feature history is required.
See [exact diagnosis, independent tests and limits](SLOPED_PLANAR_OPENING_MOVE.md).

## Supported geometry (v0.3.6)

Planar targets on one closed valid solid/shell, up to 64 faces. The legacy global
offset route certifies plane, cylinder and cone supports; local reconstruction
routes analyze their own actual neighborhoods. Box faces, cylinder/cone caps, simple Subtract
faces and the two-height Unite riser are covered by automated geometry tests.
Both signs of a face offset are supported conditionally on reconstruction.
Cone cap radii follow the existing conical support, rather than primitive Pull.
Located B-Reps and existing part instance transforms remain supported.

The shared [B-Rep reconstruction layer](BREP_RECONSTRUCTION.md) normalizes both
input and output. OCCT same-domain merging and connectivity-checked internal
subshape cleanup remove redundant final trims. Common confirms that cleanup
preserves material; disconnected coplanar patches remain separate when necessary.

The usual intersection-offset path keeps original analytic supports. When it
fails, bounded all-planar material-cell reconstruction can accept controlled
support disappearance and newly exposed patches on existing planes. It uses
OCCT Splitter, halfspace Common and Fuse, not an extruded bridge volume. The
requested target plane must survive (possibly merged with another exterior face)
and volume must change in the requested direction.

On the two-height Unite fixture, Move -60 reaches x=120 with 8 faces; +60 removes
the low terrace and produces 6 faces; +100 reaches x=-40 and exposes the necessary
underside on z=50 with 8 faces. A notched Box preserves two disconnected coplanar
wall patches when deepened, and merges to 6 faces when its notch closes. Tests
check material distribution as well as exact volumes and topology.

The new path requires one target-plane patch, at most 12 distinct planes and 32
source cells. Complex nonconvex partitions, lost targets, multiple shells/solids,
freeform/blend surfaces and unreliable intersections remain refused. Cylinder/
cone cap edits keep the conservative existing construction path; there is no new
general curved-surface topology algorithm. Failed previews leave document geometry
and history intact. See the shared architecture for the exact scope and limits.

## Verification

Run the CMake build with `OPENDESIGNER_BUILD_TESTS=ON`, all CTests and
`git diff --check`. The move-face suite verifies signed Box/cylinder/cone
cap dimensions and volumes, Subtract geometry, source preservation, transformed
parts, numeric/drag/keyboard input, preview/cancel/commit and cached Undo/Redo.
It also runs Move/Rotate/Align transaction checks on the Unite riser while
preserving name, color, presentation, transform and browser row.
The face-transform-geometry suite independently checks Unite material occupancy
and exact volume, including rotated wedge shapes; see the shared layer document.
Manual Fedora KDE verification of v0.3.6 is pending. For repeatable checks, use both signs,
Cancel/Esc, OK/Enter and one Undo/Redo on the documented fixture.

## Exact oblique-face regression: Rotate +30° then Move +49 mm

The supplied Fedora debug B-Rep now moves the selected oblique plane +49 mm
without extending a triangular solid wing through the real front face. The
reconstruction retains neighboring physical trim supports even where source-cell
splitting made them temporarily redundant. The low terrace remains unchanged;
the front/side faces extend to the requested plane. Correct volume is
1091890.5249039864 mm³ (recorded faulty result: 1156871.7334342287 mm³).

Exact fixture provenance, material samples and the independent oracle are in
[the fixture documentation](../tests/fixtures/rotate30-move49/README.md).
Visible preview, Cancel, Enter, Undo/Redo and Part identity are tested on this
same B-Rep. The bounded planar fallback now explicitly refuses concave, holed or
conflicting target-neighbor decompositions; existing native analytic construction
paths retain their conservative support rules. Manual Fedora KDE retest pending.
