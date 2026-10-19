# OpenDesigner

OpenDesigner is an open-source cross-platform 3D direct-modeling application inspired by classic CoCreate SolidDesigner.

**Status:** v0.3.1 consistent rotation pivot prototype; not a packaged release.

## Prototype features
- Qt 6 desktop window with dockable Structure Browser
- Open CASCADE B-Rep viewport
- Box, Cylinder, Sphere and Cone primitives with direct Pull and exact dimensions
- Move/Rotate gizmos centered on B-Rep volume centroids, selection and duplication
- Unite, explicit-target Subtract and Intersect with validated results and Undo/Redo
- Structure Browser with names, color, visibility and stable Boolean result selection
- Orbit, pan and zoom

## Build (Linux)
Requires CMake 3.21+, a C++20 compiler, Qt 6.4+ Widgets and Open CASCADE visualization and modeling algorithms development libraries (including TKBO/TKBool).

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
