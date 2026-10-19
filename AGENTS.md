# OpenDesigner — Codex instructions

OpenDesigner is a GPL-3.0 open-source 3D direct-modeling application. Use **CoCreate SolidDesigner / Modeling 2008 v16.0** (not v15.x) as the primary behavioral and workflow reference, without copying proprietary code or assets.

Confirmed targets: Linux, Windows, macOS; iPadOS and Android tablet versions on the roadmap. Touch mode should be optional in Settings and usable on all applicable platforms. Preserve desktop controls. glTF/GLB and FBX import/export belong on the roadmap along with CAD formats.

## Workflow
1. Read ROADMAP.md, ARCHITECTURE.md, DECISIONS.md and CODEX_HANDOFF.md.
2. Inspect the *actual checkout* and its git status before editing; do not assume source code is already pushed to main.
3. Keep changes minimal, incremental, testable and compatible with GPL-3.0. Confirm third-party library licenses.
4. Preserve the reported fix for face manipulation: a manipulator arrow on the lower face must operate on the intended lower face, not the top.
5. Do not mark roadmap tasks complete without verifying source and behavior.
6. Update roadmap/decisions when project plans or architecture change.

## Next feature
The last requested implementation task in ChatGPT was a **sphere (bol) primitive**. First find the existing primitives, picking and rendering architecture. Implement a sphere consistently with the current code and run build/tests. Report any missing source code before planning a replacement architecture.

Previous local user-reported build commands: `cmake --build build -j$(nproc)` and `./build/OpenDesigner` (not yet verified for GitHub main).
