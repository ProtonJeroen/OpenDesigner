# OpenDesigner

OpenDesigner is an open-source cross-platform 3D direct-modeling application inspired by classic CoCreate SolidDesigner.

**Status:** v0.4.9 Cylindrical Move Face; not a packaged distribution.

Move supported cylindrical holes and bosses from their current B-Rep, with explicit
lateral directions and boss Trim/Extend policies. Supported boundary features also
have Delete Face and diameter/height Pull workflows. No feature history is needed.
The user reports **47/47 manual desktop checks passed** across the recent geometry
and crash-fix phases; these are user-reported acceptance results. Native persistence,
identity and history checks are recorded in [Phase 9E](docs/FACE_HISTORY_PERSISTENCE.md).
Final build/test results and release limitations are recorded in the
[v0.4.9 integration review](docs/V049_INTEGRATION_REVIEW.md).

**Unresolved geometry capability:** Delete Face on the sloped cylindrical opening
in `hole2.odc` remains unsupported before and after serialization. This is not a
persistence failure or completed Delete Face functionality. See the
[rotated-rim scope](docs/ROTATED_RIM_CYLINDER.md#reconstruction-limits) and
[Pull/Resize limits](docs/PULL_RESIZE_GAPS.md#unsupported-versus-ambiguous-versus-invalid).
Cross-screen HiDPI panel placement also remains a platform limitation on the
reviewed Qt/xcb desktop; see the integration review for the tested scope.

Both commands support New Solid, Add Material and Remove Material with shared
Active Part targeting, live previews, Undo/Redo and native Save/Open. The user
reported successful Fedora KDE acceptance: Unified Extrude 7/7, Revolve New Solid
13/13 and Revolve Add/Remove 15/15 (**35/35 total**). See
[Extrude](docs/EXTRUDE_PROFILE.md), [Revolve](docs/REVOLVE.md) and the
[v0.4.8 scope and limitations](docs/ROADMAP.md#v048--unified-extrude--revolve).

The supplied Delete Face and Unite seam cases were manually confirmed by the user
on 2026-10-20. Native Save/Open and the cone/hole Pull fixes were tested and
confirmed by the user on Fedora KDE on 2026-10-21. Automated regression coverage
and remaining scope limits are documented below.

Construction geometry adds independent Workplanes, Axes and Points with shared
command sessions, Undo/Redo and native storage. Its Fedora KDE workflow
was manually tested by the user; see [construction geometry](docs/CONSTRUCTION_GEOMETRY.md).

2D Modeling adds exact local geometry and schema-v3 storage, Workplane editing,
interactive/numeric lines, circles and arcs, analytic snapping, browser management
and shared history. Start with **Edit Sketch** or the Workplane chooser in the
2D Modeling toolbar. Overdraw converts explicitly chosen bounded Construction
portions into independent Profile geometry. Construction Axes creates real local
X/Y infinite lines for snapping and Overdraw; XY/XZ/YZ Workplane presets support
normal offsets, including in an empty document. Arcs support explicit CW/CCW;
Offset and Common Tangent Construction Line support Infinite/Finite output.
See [Sketch Editor](docs/SKETCH_EDITOR.md).
Nonmodal 2D/3D command panels share viewport-relative upper-right placement,
12-logical-pixel margins, drag positioning and scrollable content for small
viewports. See [command panels](docs/COMMAND_SESSIONS.md#command-panel-positioning).
The user confirmed 18/18 GUI regressions on Fedora KDE on 2026-10-23:
the earlier nine and all nine additional checks, including axes and presets.

Native writes use `.odc` schema v5 for Ellipse/EllipticalArc values, v4 when only
local 2D Construction Points require it, and v3 otherwise; readers accept v1–v5.
Phase 6A.2 adds independent Ellipse/Elliptical Arc drawing with exact numeric
input, live preview, CW/CCW, both roles and shared history. Basic elliptic snaps
and line/ellipse intersections are available; conic/conic and elliptic tangent
snaps remain incomplete. Oblique 3D projection is deferred to 6A.3. See the
[implementation scope](docs/ELLIPSE_IMPLEMENTATION_PLAN.md#11-approved-6a2-independent-drawing-requirements).
Points have exact local coordinates,
fixed-screen-size markers, typed selection, snapping and shared history. This
foundation has no separate interactive point-drawing tool. **Project 3D Geometry**
is available in Modeling → 2D Modeling and the 2D toolbar after **Edit Sketch**.
Choose Vertex, Edge, Face or Mixed; select visible 3D subshapes, inspect the blue
preview, then OK or Cancel. The same exact vertex/straight-edge, parallel
circle/arc and actual face-boundary core supplies the independent 2D results.
See [projection workflow and limits](docs/SKETCH_PROJECTION.md#phase-5-interactive-project-3d-geometry)
and the pending [Fedora KDE GUI checklist](docs/GUI_TEST_PLAN.md). The existing
Point → Project onto workplane creates an independent 3D construction Point;
it does not populate a Workplane’s 2D collection. Overdraw currently requires
bounded pieces, excludes unbounded tails and 0/1-cut circles, refuses overlapping
Construction sources, and does not validate closed/extrudable profiles.

## Prototype features
- Qt 6 desktop window with dockable Structure Browser
- Open CASCADE B-Rep viewport
- Box, Cylinder, Sphere and Cone primitives with direct Pull and exact dimensions
- Move/Rotate gizmos centered on B-Rep volume centroids, selection and duplication
- Interactive Move/Rotate preview with OK/Enter, Cancel/Esc and one shared Undo/Redo step
- Move Face for supported planar faces and cylindrical holes/bosses, with validated B-Rep preview and Undo/Redo
- Rotate Face about a straight target edge or projected world axis
- Align Face as Coplanar or Parallel to a selected planar reference face
- Delete Face for certified analytic holes, bosses, convex planar chamfers and
  bounded planar transitions (including successive and inclined L-shaped steps),
  with multi-face/multi-Part preview, atomic confirmation and cached Undo/Redo
- Passive Face/Alt-Part selection and capability-aware viewport context menus
- Unite, explicit-target Subtract and Intersect with validated results and Undo/Redo
- Material-preserving Boolean seam cleanup and uniform geometry failure popups
- Structure Browser with names, color, visibility and stable Boolean result selection
- Independent Workplanes, Axes and Points with analytic creation methods, typed
  selection, properties, Move/Rotate previews and shared Undo/Redo
- Orbit, pan and zoom
- Native `.odc` documents with File → New/Open/Save/Save As, exact B-Rep
  round-trips, Part metadata, placements and saved camera state

## Build (Linux)
Requires CMake 3.21+, a C++20 compiler, Qt 6.4+ Widgets and Open CASCADE visualization, modeling, OCAF and data-exchange development libraries (including TKBO/TKBool/TKFeat and TKLCAF/TKXCAF/TKBinXCAF), plus libarchive development headers.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Windows and macOS are planned but not yet CI-validated. See docs/ROADMAP.md for planned direct modeling, workplanes, assemblies, STEP, GLB and FBX.

## License
GNU General Public License v3.0 (GPL-3.0-only). See LICENSE.


## Tests and Boolean workflow

See [Boolean Operations](docs/BOOLEAN_OPERATIONS.md) for selection, target/tool
choice, error handling, Undo/Redo and known limits. Enable the optional
integration tests with `-DOPENDESIGNER_BUILD_TESTS=ON`, build, then run
`ctest --test-dir build --output-on-failure` with an X11/OpenGL display.
GitHub Actions enables the test targets for its Release build and runs all CTests
under Xvfb with software OpenGL before uploading the Linux executable.

Rotation behavior, regression coverage and manual checks: [Consistent rotation pivot](docs/ROTATION_PIVOT.md).
The repository also includes an original [SolidDesigner 16 workflow analysis](docs/SOLIDDESIGNER16_WORKFLOWS.md), without proprietary manual assets.

Interactive transformation workflow and architecture: [Command sessions and preview](docs/COMMAND_SESSIONS.md). Booleans now share the preview lifecycle with explicit Target/Tools and optional Keep Tools. Primitive Pull / Resize now also requires explicit activation and OK/Enter; Cancel/Esc restores the original.

Supported surfaces, direct-edit behavior and limitations: [General Move Face](docs/MOVE_FACE.md).

Planar face rotation and alignment behavior, validation and limitations:
[Rotate Face](docs/ROTATE_FACE.md) and [Align Face](docs/ALIGN_FACE.md).

Selection shortcuts, explicit context actions and eligibility: [Selection and context menus](docs/SELECTION_CONTEXT.md).

Face removal, conservative reconstruction limits and shared geometry error
popups: [Delete Face](docs/DELETE_FACE.md).

Native document storage, `.odc` schema v3/v4/v5 with v1–v5 read compatibility, transaction-safe Save/Open and
round-trip tests: [Native documents](docs/NATIVE_DOCUMENTS.md).

The v0.3.6 interaction workflow retains passive selection: click Faces, Alt+click Parts,
Ctrl to add/remove. Choose a context or Modeling action to show its manipulators.
OK/Enter and Cancel/Esc completely close the function and remove all temporary UI;
selection remains available for another explicit command. There is no selection
mode dropdown or combined Move/Rotate gizmo. Boolean Target/Tools and Keep Tools
retain their explicit preview workflow.

Shared analytic face reconstruction, verified Boolean Unite riser behavior,
controlled topology changes and remaining limits:
[B-Rep reconstruction](docs/BREP_RECONSTRUCTION.md).

Advanced Face Topology adds final material-preserving normalization and bounded
planar reconstruction across support-disappearance events. The tested Unite riser
now rotates to +75° past the former cap boundary; Move can remove its low terrace
or expose a necessary underside without leaving redundant coplanar seams. Curved
and unrestricted topology events remain limited; see the reconstruction document.

### Diagnostic model export (v0.3.6)

Use **File → Export Debug Model...** to capture all Parts (including hidden ones),
exact OCCT B-Reps, selection, available operation parameters and diagnostics in a ZIP.
`build/OpenDesignerDebugModel model.zip --view` reloads an isolated read-only model
and compares stored meshes, fresh triangulation and B-Rep wireframe. See
[debug export instructions](docs/DEBUG_MODEL.md). This is not `.odc` Save/Open;
that remains a separate diagnostic format from v0.3.7 `.odc` documents. Building requires libarchive development headers
(`libarchive-devel` on Fedora).

The exact supplied **Rotate Face +30° → Move Face +49 mm** case now retains
physical neighboring trim planes during cell reconstruction, removing the real
triangular protrusion. Regression tests use the exported B-Reps and an independent
material oracle. That earlier reconstruction fixture retains its automated coverage;
the subsequent Delete Face and Unite reports have been confirmed manually. See the
[diagnosis](docs/BREP_RECONSTRUCTION.md).
