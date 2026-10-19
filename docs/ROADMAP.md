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

## v0.20 — cone primitive
- Create Cone adds a parametric GeneralBRep frustum (80/40 mm diameters, 80 mm height).
- Four orange Pull arrows edit absolute bottom/top diameters and height from either end; top diameter zero creates a pointed cone.
- Cone parameters survive transforms, duplication and Pull undo/redo. Equal diameters remain editable through a cylinder B-Rep.
- Transform gizmos use the current B-Rep volume centroid, including after Pull and undo/redo; multipart pivots retain unweighted averaging.
- Regression coverage and manual checks: [Create Cone and Pull](CONE_PULL.md).


## v0.3 — Boolean Operations
- Modeling > Boolean provides Unite, Subtract and Intersect for primitives and GeneralBRep solids in world coordinates.
- Subtract explicitly selects a target and lists the remaining selected parts as tools.
- Validated results replace sources transactionally; cached Boolean commands share the modeling undo/redo history.
- Stable inactive part slots synchronize browser rows and selection across Boolean undo/redo without renumbering IDs.
- Results support transforms, color, visibility, duplication and further Booleans; multi-solid disjoint unions are supported.
- Automated checks and manual smoke steps: [Boolean Operations](BOOLEAN_OPERATIONS.md).
- Follow-up: general face-offset editing, history compaction, persistent IDs and cancellable background operations.

## v0.3.1 — consistent rotation pivot
- Rotate Part and gizmo rotation share B-Rep volume centroid calculation in world coordinates.
- Groups retain the unweighted mean pivot and support menu rotation with grouped Undo/Redo.
- Existing local gizmo axes, world dialog axes and Primitive Pull anchors are preserved.
- Regression coverage and manual checks: [Rotation pivot](ROTATION_PIVOT.md).

## v0.3.2 — command sessions and preview
- Interactive Move and Rotate, including groups, use temporary presentation transforms until OK/Enter.
- Cancel/Esc and tool changes discard the preview and preserve original geometry, transforms and selection.
- Confirmation adds one operation to the existing modeling history; cancellation preserves Undo/Redo.
- Primitive Pull, Booleans and modal transform dialogs retain their current confirmation behavior.
- Architecture, future adapters and verification: [Command sessions](COMMAND_SESSIONS.md).
