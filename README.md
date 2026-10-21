# OpenDesigner

OpenDesigner is an open-source cross-platform 3D direct-modeling application inspired by classic CoCreate SolidDesigner.

**Status:** v0.4.0 construction geometry prototype in development; not a packaged release.
The supplied Delete Face and Unite seam cases were manually confirmed by the user
on 2026-10-20. Native Save/Open and the cone/hole Pull fixes were tested and
confirmed by the user on Fedora KDE on 2026-10-21. Automated regression coverage
and remaining scope limits are documented below.

Construction geometry adds independent Workplanes, Axes and Points with shared
command sessions, Undo/Redo and native storage. Its new Fedora KDE workflow
validation is pending; see [construction geometry](docs/CONSTRUCTION_GEOMETRY.md).

## Prototype features
- Qt 6 desktop window with dockable Structure Browser
- Open CASCADE B-Rep viewport
- Box, Cylinder, Sphere and Cone primitives with direct Pull and exact dimensions
- Move/Rotate gizmos centered on B-Rep volume centroids, selection and duplication
- Interactive Move/Rotate preview with OK/Enter, Cancel/Esc and one shared Undo/Redo step
- General Move Face for supported planar faces, with validated B-Rep preview and Undo/Redo
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

Rotation behavior, regression coverage and manual checks: [Consistent rotation pivot](docs/ROTATION_PIVOT.md).
The repository also includes an original [SolidDesigner 16 workflow analysis](docs/SOLIDDESIGNER16_WORKFLOWS.md), without proprietary manual assets.

Interactive transformation workflow and architecture: [Command sessions and preview](docs/COMMAND_SESSIONS.md). Booleans now share the preview lifecycle with explicit Target/Tools and optional Keep Tools. Primitive Pull / Resize now also requires explicit activation and OK/Enter; Cancel/Esc restores the original.

Supported surfaces, direct-edit behavior and limitations: [General Move Face](docs/MOVE_FACE.md).

Planar face rotation and alignment behavior, validation and limitations:
[Rotate Face](docs/ROTATE_FACE.md) and [Align Face](docs/ALIGN_FACE.md).

Selection shortcuts, explicit context actions and eligibility: [Selection and context menus](docs/SELECTION_CONTEXT.md).

Face removal, conservative reconstruction limits and shared geometry error
popups: [Delete Face](docs/DELETE_FACE.md).

Native document storage, `.odc` schemas v1/v2, transaction-safe Save/Open and
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
