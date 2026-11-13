# Workplanes and geometric references (v0.5.3)

This document defines the coordinate frames, reference types, reference
stability rules, persistence and Undo/Redo behaviour that Workplanes, Axes,
Points, Sketch geometry and solid subshapes share from v0.5.3 onwards. It is the
contract that later exact curves (v0.5.4), Sweep and Loft build on.

The geometry-first, history-independent principle of the
[roadmap](ROADMAP.md) applies throughout: a reference is evaluated against the
**current** B-Rep, construction and Sketch values at the moment an operation
uses it. Nothing stores a parametric dependency on a source object.

## 1. Investigation findings (state at v0.5.2, `131a80f`)

| Area | Location | Finding |
| --- | --- | --- |
| Workplane/Axis/Point values | `src/ConstructionGeometry.h/.cpp` (`Construction::Object`, `Method`, `resolve`, `build`) | All three kinds exist since v0.4.0 with UUID, name, `gp_Ax3` frame, visibility, selection lock, display extent and offset. Creation methods include two points, straight edge, cylindrical face, point+direction, normal to Workplane (Axis) and XYZ, vertex, edge midpoint, circle center, axis/plane and two-axis intersections, offset, projection (Point). |
| Construction commands, picking, Undo | `src/ConstructionTools.cpp`, `ConstructionPanel` | `CommandSession::Kind::Construction`; role picking resolves picked Part subshapes through `Construction::resolve`. Sketch entities cannot be picked as construction operands. One `ConstructionChange` per OK. |
| Workplane activation / Edit Sketch | `CadViewport::activateSketchWorkplane(id, alignView=true)` in `src/SketchDrawing.cpp`; menu `sketch.edit`, toolbar Workplane combo, context menu `context.edit_sketch`, browser | Every caller except a few tests aligned the camera (`lookNormalToSketch`) on activation. The context-menu "Look Normal to Workplane" item also *activated* the Workplane. |
| Camera | `CadViewport::lookNormalToSketch`, `fitConstructions`, `stabilizeViewCamera` | Look Normal sets eye along the Workplane normal, up = Workplane +Y, then fits the Workplane. Camera state is saved in `.odc` but never dirties the document. |
| Workplane frame and active ring | `src/SketchDisplayBounds.*` (`displayRectangle`, `WorkplaneBorderWidth = 15`), `CadViewport::constructionPresentation`, `workplaneColor` | Opening = content bounds + 10 % per side, minimum span 20 mm; empty Workplanes use `extent` (default half 100 mm, Face-derived minimum half 10 mm). A 15 mm open ring is drawn outside the opening; active = green, inactive = light grey, 50 % transparency. |
| Move/Rotate Workplane preview | `CadViewport::previewConstruction` | The target presentation is erased and replaced by the candidate presentation. Its colour is forced back to the active/inactive Workplane colour and it is displayed with display mode `-1`, so the frame is not reliably drawn as a shaded ring while previewing. |
| Sketch geometry and snapping | `src/SketchGeometry.h`, `src/SketchSnapping.*` (`snapTargets`, `closestSnap`) | Snap targets carry a 2D point, kind and label only. They do not identify which entity/feature produced the point, so a snap cannot be re-evaluated or compared with a picked reference. |
| Picking / `SelectionContext` | `src/SelectionContext.cpp` | Kinds: Parts, Faces, Edges, Constructions, Sketches. Automatic Edge/Face selection, delayed Part promotion and Alt/Ctrl semantics from v0.5.1 are implemented here and are not changed. |
| Move Face | `src/MoveFace.cpp`, `MoveFaceGeometry` | Planar faces: signed offset along the outward normal only (`MoveFaceGeometry::move`, or `EdgeBlendFeatures::move` when certified blends exist). Cylindrical holes/bosses: lateral direction from a Part axis + angle. No external direction reference. |
| View modes / visibility | `CadViewport::setPartVisible`, `CadPart::visible` | Only per-Part visibility exists. There is no view-level wireframe/shaded or "hide all solids" state. |
| Persistence | `src/NativeDocument.*`, [Native documents](NATIVE_DOCUMENTS.md) | Schema v5. Constructions (v2+), Sketch entities (v3+), construction points (v4), ellipses (v5). |

## 2. Coordinate frames

All lengths are model millimetres, angles degrees in the UI and radians in code.

| Frame | Definition | Owner moves → |
| --- | --- | --- |
| **World** | Right-handed global XYZ. Every resolved reference is expressed here. | — |
| **Part placement** | `CadPart::orientation` (`gp_Trsf`) maps the canonical local B-Rep to world. Faces/Edges/Vertices are stored in Part-local coordinates. | Move/Rotate Part changes only the placement; the local B-Rep and its subshapes stay identical, so subshape references stay valid and resolve to the moved location. |
| **Workplane local** | `Construction::Object::frame` (`gp_Ax3`): origin, +X (`XDirection`), +Y (`YDirection`), normal +Z (`Direction`). Right-handed and orthonormal. | Move/Rotate Workplane replaces the frame. Sketch entities owned by the Workplane keep their local values and therefore follow it. |
| **Sketch local 2D** | `Sketch::Point{x,y}` in the owner Workplane's XY. `Sketch::worldPoint(p, frame) = origin + x·X + y·Y`. | Follows its Workplane exactly (no stored world coordinates). |
| **Axis** | `frame.Location()` (origin) and `frame.Direction()` (signed unit direction). | Move/Rotate Axis replaces the frame. |
| **Point** | `frame.Location()`. | Edit Coordinates / Offset replaces it. |

Display extents, display offsets and the Workplane ring are presentation-only
and never participate in reference resolution.

## 3. Reference model

A new pure module, `src/ReferenceGeometry.h/.cpp` (namespace `References`, part
of the `OpenDesignerConstruction` library, no widgets or AIS), defines:

- `References::Target` — *what* was picked: a kind, an owner identity and, for
  subshapes, the picked `TopoDS_Shape` of the Part's canonical B-Rep; for Sketch
  points a `Sketch::PointReference` (entity, feature, partner, index).
- `References::Model` — *the current document values* to resolve against:
  Parts (persistent id, canonical shape, placement, visibility),
  construction objects and Sketch entities.
- `References::Resolved` — the geometric result in world coordinates: an
  optional point, an optional line (`gp_Ax1`) and an optional plane (`gp_Ax3`).
- `References::resolve(model, target, resolved, error)` — the single resolution
  path used by picking, snapping and every command that consumes references.
- `References::direction(...)` and `References::pointToPoint(...)` — derived
  direction rules shared by commands.

| Kind | Identified by | Resolves to | Direction (if requested) |
| --- | --- | --- | --- |
| Part | Part persistent id | point = placement origin; plane = placement XY frame | — (refused: a Part has no unique direction) |
| Face | Part id + picked Face | planar: plane (oriented outward normal) and area centroid; cylindrical/conical: axis line and centroid | planar: outward normal; cylindrical/conical: axis; other surfaces refused |
| Edge | Part id + picked Edge | straight: line in edge orientation, midpoint; circular: circle axis line, center | straight: edge direction; circular: circle axis; other curves refused |
| Vertex | Part id + picked Vertex | point | refused |
| Axis | construction id | line (origin, direction) | the Axis direction |
| Workplane | construction id | plane frame, origin | the Workplane normal |
| Point | construction id | point | refused |
| Sketch line | Sketch entity id (Segment or Infinite Line) | line in world (Segment start→end; Infinite Line origin/direction), Segment midpoint | line direction |
| Sketch point | entity id + feature: start, end, midpoint, center, construction point position, or intersection (partner id + index) | point in world | refused |

"Center" and "intersection" are Sketch point features of the same kind and use
the same evaluation functions as snapping. Solid circle centers are available
through a circular Edge (its center point), consistent with the existing
**Point → Center of circle** construction method.

Face/Edge/Vertex geometry is evaluated through `Construction::resolve`, the
resolver construction commands already use, so plane orientation, edge
orientation and placement handling are identical everywhere.

### Picking and snapping

`CadViewport::pickReference(pixel, accepted kinds)` is the one viewport adapter
that converts a click into a `References::Target`:

1. Sketch points on visible Workplanes, through `Sketch::snapTargets` +
   `Sketch::closestSnap` — exactly the targets and pixel radius the Sketch
   drawing snap uses — but only targets that carry a `PointReference`
   (cursor-dependent *Nearest* / *Tangent* snaps are not references).
2. Construction Points (screen distance to their position).
3. Sketch lines and Axes (screen distance to their displayed segment).
4. Part Vertices, Edges and Faces through the OCCT detection already used for
   selection (owner shape + Part presentation), including Part placement.

Snap targets now carry the `PointReference` that produced them, and
`References::resolve` re-evaluates that reference from the current entities.
Snapping and picking therefore resolve to the same reference and the same
point. Existing automatic Edge/Face selection, delayed Part promotion and the
Alt/Ctrl selection semantics are not changed: reference picking is only active
while a command explicitly asks for a reference.

## 4. Reference stability

- References are resolved when used (preview, OK). A Move Face preview
  re-resolves its direction reference on every update and once more before
  committing.
- A Face/Edge/Vertex reference is valid only while its picked subshape is still
  a member (`IsSame`) of the owning Part's current canonical shape. After an
  edit that rebuilds topology, the reference is **stale** and is refused with
  "*… no longer exists in the current geometry of <Part>; pick it again*". It is
  never rebound to a geometrically similar Face/Edge.
- Part placement changes (Move/Rotate Part) do not invalidate subshape
  references; they resolve to the transformed location.
- Construction and Sketch references are identified by UUID. A deleted object
  or entity, a deleted owner Workplane, or a hidden/locked object is refused
  (hidden and locked objects cannot supply operands, as in v0.4.0).
- Intersections are re-evaluated with `Sketch::intersect`; if the indexed
  intersection no longer exists the reference is stale.
- Ambiguous geometry is refused, never guessed: non-straight/non-circular edge
  directions, free-form face normals, zero-length Point-to-Point vectors and
  directions with no component along a planar Face's normal.
- Refusal is atomic: no model, selection or history change; the command stays
  open so another reference can be picked.

## 5. Functional design

### 5.1 Camera decoupling

`activateSketchWorkplane(id)` (Edit Sketch, Workplane combo, browser, context
menu) only changes the active Workplane, picking and labels; it never changes
the camera. The new action **View → Look at Workplane** (`view.look_at_workplane`,
also on the 2D toolbar and in the Workplane context menu) aligns the camera to
the active Workplane, or to the single selected Workplane when none is active:
view direction = −normal (eye on the +normal side), up = Workplane +Y, then fit
to the Workplane frame. The existing 2D **Look Normal** entry triggers the same
function. Camera changes remain navigation: no Undo step, no dirty document.

### 5.2 Workplane frame presentation

- Ring width `Sketch::WorkplaneBorderWidth` is 10 mm (was 15 mm).
- The free opening is at least 100 × 100 mm in Workplane-local coordinates,
  centered on the content-derived rectangle; larger content still enlarges it.
  This applies to empty and Face-derived Workplanes too.
- During Move/Rotate Workplane previews the candidate frame is displayed as a
  shaded, translucent ring in the preview colour (blue) at the transformed
  location, so the Workplane remains visible; the original is restored on Cancel.
- Presentation only: no schema, picking or analytic change.

### 5.3 Axes and Points

Existing v0.4.0 entities are reused unchanged (browser group, `.odc` storage,
rename, show/hide, lock, Move/Rotate, delete, Undo/Redo). Construction role
picking now also accepts Sketch points and Sketch lines through `pickReference`,
which adds **Point from Sketch point / center / intersection** and **Axis from
Sketch line** without new methods or schema. **Axis from cylindrical face**
also accepts conical faces (cone axis).

### 5.4 Move Face direction

The Move Face panel gains **Direction**: *Face normal* (default, unchanged),
*Edge / Sketch line / Axis*, or *Point to Point*, a **Pick** button, **Reverse**
and the existing exact distance field. The reference is independent of the
moved Face and may belong to another Part or a Workplane.

For a planar Face, translating its support plane by `s·d` is geometrically the
same as offsetting it by `s·(d·n)` along its outward normal `n`; tangential
components do not change a plane. The command therefore keeps the existing,
certified planar Move Face path (`MoveFaceGeometry::move` or
`EdgeBlendFeatures::move` with Keep ON/OFF) with the projected distance. A
direction with `|d·n| < 1e-9` is refused ("parallel to the Face"). The orange
arrow shows `d`; the typed value is the distance along `d`.

Cylindrical hole/boss walls keep their existing lateral-reference controls;
external direction references are refused for them ("not certified"), as are
all other unsupported combinations.

### 5.5 View modes

**View → Wireframe / Shaded** and **View → Show 3D Geometry** are view-level
settings stored in `CadViewport`, not in Parts. Hiding 3D geometry erases Part
presentations without touching `CadPart::visible`; showing restores exactly the
Parts whose own flag is visible. Wireframe/Shaded changes the Part display mode
only. Neither is saved in `.odc`, neither is undoable, and New/Open keep the
current view settings.

## 6. Persistence

No `.odc` schema change: schema v5 remains current.

- Reference targets are transient command state and are never saved; only the
  confirmed result geometry is saved.
- Axis/Point creation from Sketch references stores ordinary construction
  frames (schema v2 records).
- Ring width, minimum frame size and view modes are presentation-only.
- Older documents (v1–v5) open unchanged.

## 7. Undo/Redo

| Action | History |
| --- | --- |
| Activate Workplane / Edit Sketch | none (unchanged) |
| Look at Workplane | none (camera navigation) |
| Workplane Move/Rotate (frame preview) | one `ConstructionChange` on OK (unchanged) |
| Axis/Point creation (any source) | one `ConstructionChange` on OK |
| Move Face with a direction reference | one `FaceChange` on OK; Cancel/refusal none |
| Wireframe/Shaded, Show/Hide 3D Geometry | none (view state) |

## 8. Interfaces for v0.5.4 (not implemented)

- **Ellipses / elliptical arcs:** already exact `Sketch::Ellipse`/`EllipticalArc`
  values. As references they add Sketch point features *major/minor vertex*
  and keep *center*, *start*, *end* through `Sketch::PointReference`; their
  axis direction becomes a Sketch line-like direction feature. Profiles use the
  existing `SketchProfileBuilder` edge conversion.
- **Exact projection:** `SketchProjection` results become Sketch entities of
  exact type (line, circle, arc, ellipse, elliptical arc, spline). Projected
  entities are ordinary Sketch entities: no link to the source edge is stored,
  so they resolve like any other Sketch entity.
- **2D interpolating splines:** a new `Sketch::Geometry` alternative (appended,
  new persisted tag) storing through-points, closure and end tangents. Point
  features: each through-point (index), start, end. Direction features: end
  tangents. Profile Builder converts it to an exact `Geom2d_BSplineCurve` on
  the Workplane.
- All of these use `References::resolve` and `Sketch::PointReference`; no new
  resolver or picking path is introduced.

## 9. Tests

Covered by `reference-geometry` (pure resolution, stale refusal, snapping
identity) and `workplane-reference-sessions` (camera, frame presentation,
picking, Move Face direction, Undo/Redo, Save/Open, view modes); see the
roadmap entry for exact commands and results.

## 10. Manual acceptance (Fedora KDE/Wayland, 100 % and `QT_SCALE_FACTOR=2`)

1. Rotate the camera to an oblique view. Edit Sketch on a Workplane via the
   menu, toolbar combo, browser and context menu: the camera must not move.
   Then View → Look at Workplane: view normal to the plane, +Y up.
2. Create an empty Workplane and one from a small Face: each frame opening is
   at least 100 × 100 mm; the green active ring is visibly narrower (10 mm).
   Draw geometry beyond 100 mm: the frame grows.
3. Move and Rotate a Workplane: the blue frame stays visible during dragging
   and numeric input; Cancel restores the original, OK commits one Undo step.
4. Move Face on a box top with Direction = Edge (a vertical box edge), a
   Sketch line, an Axis and Point to Point; check arrow, preview, Reverse,
   exact distance, OK, Undo/Redo. Pick a direction parallel to the face: refused.
5. Move the reference Part, then use its Edge again: the direction follows.
   Edit the reference Part's Face so the Edge is rebuilt: stale refusal.
6. Create Point from a Sketch circle center and an Axis from a Sketch line.
7. View → Wireframe/Shaded and Show/Hide 3D Geometry: individual Part
   visibility in the browser is unchanged; toggling back restores exactly.
