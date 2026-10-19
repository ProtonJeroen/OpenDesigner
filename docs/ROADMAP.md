# OpenDesigner roadmap

## v0.1 — foundation
- Qt 6 native desktop application
- Open CASCADE 3D viewport
- Dockable structure browser
- Box creation and basic navigation
- Linux CI build

## Next milestones
- CAD document ownership, persistent save/load and robust selection
- Direct face modeling and workplanes
- Assemblies and 3D copilot
- STEP, IGES, STL
- GLB/glTF and FBX import/export

OpenDesigner targets Windows, Linux and macOS. This roadmap describes planned work, not finished features.

## v0.20 — primitive Pull
- Sphere creation and interactive Pull with absolute diameters are implemented on the sphere feature branch.
- Sphere and cylinder Pull show and accept absolute diameters or cylinder heights, preserving local centers, opposite caps, instance transforms and undo/redo.
- Optional viewport regression tests and manual verification steps: [Sphere Pull](SPHERE_PULL.md).
