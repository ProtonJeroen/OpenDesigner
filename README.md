# OpenDesigner

OpenDesigner is an open-source cross-platform 3D direct-modeling application inspired by classic CoCreate SolidDesigner.

**Status:** v0.3.5 Advanced B-Rep Reconstruction prototype; not a packaged release.
Manual Fedora KDE validation for v0.3.5 passed; the user confirmed all tested functionality, including live previews and context menus.

## Prototype features
- Qt 6 desktop window with dockable Structure Browser
- Open CASCADE B-Rep viewport
- Box, Cylinder, Sphere and Cone primitives with direct Pull and exact dimensions
- Move/Rotate gizmos centered on B-Rep volume centroids, selection and duplication
- Interactive Move/Rotate preview with OK/Enter, Cancel/Esc and one shared Undo/Redo step
- General Move Face for supported planar faces, with validated B-Rep preview and Undo/Redo
- Rotate Face about a straight target edge or projected world axis
- Align Face as Coplanar or Parallel to a selected planar reference face
- Passive Face/Alt-Part selection and capability-aware viewport context menus
- Unite, explicit-target Subtract and Intersect with validated results and Undo/Redo
- Structure Browser with names, color, visibility and stable Boolean result selection
- Orbit, pan and zoom

## Build (Linux)
Requires CMake 3.21+, a C++20 compiler, Qt 6.4+ Widgets and Open CASCADE visualization and modeling algorithms development libraries (including TKBO/TKBool/TKFeat).

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

The v0.3.5 interaction workflow retains passive selection: click Faces, Alt+click Parts,
Ctrl to add/remove. Choose a context or Modeling action to show its manipulators.
OK/Enter and Cancel/Esc completely close the function and remove all temporary UI;
selection remains available for another explicit command. There is no selection
mode dropdown or combined Move/Rotate gizmo. Boolean Target/Tools and Keep Tools
retain their explicit preview workflow.

Shared analytic face reconstruction, verified Boolean Unite riser behavior,
controlled topology changes and remaining limits:
[B-Rep reconstruction](docs/BREP_RECONSTRUCTION.md).
