# OpenDesigner

OpenDesigner is an open-source cross-platform 3D direct-modeling application inspired by classic CoCreate SolidDesigner.

**Status:** v0.1 prototype, not yet verified as a release.

## Prototype features
- Qt 6 desktop window with dockable Structure Browser
- Open CASCADE B-Rep viewport
- Create box (100 × 80 × 60 mm)
- Basic part tree, selection, orbit, pan and zoom

## Build (Linux)
Requires CMake 3.21+, a C++20 compiler, Qt 6.4+ Widgets and Open CASCADE visualization development libraries.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Windows and macOS are planned but not yet CI-validated. See docs/ROADMAP.md for planned direct modeling, workplanes, assemblies, STEP, GLB and FBX.

## License
GNU General Public License v3.0 (GPL-3.0-only). See LICENSE.
