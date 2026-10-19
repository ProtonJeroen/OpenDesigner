# OpenDesigner roadmap

Aspirational backlog based on previous ChatGPT discussions. **Unchecked items are not necessarily absent from local code**: verify first.

## Foundation
- [ ] Verify local source tree, existing CMake/build workflow and dependencies.
- [ ] Verify existing box primitive and selected-face manipulator behavior.
- [ ] Preserve previously reported lower-face manipulation bug fix.
- [ ] Add repeatable geometry checks and build smoke tests.

## Modeling
- [ ] **Sphere primitive** — next requested development task.
- [ ] More primitive creation and editing workflows, compatible with current modeling kernel.
- [ ] Selection, transforms, view controls, snapping, undo/redo and direct-modeling operations.

## Classic workflows
- [ ] Re-create practical interface and modeling workflows of **CoCreate Modeling 2008 v16.0** as faithfully as useful.
- [ ] Modern enhancements while retaining recognizable classic selection and command behavior.
- [ ] Use lawful v16 reference material only for observation, never copying proprietary implementation.

## Exchange
- [ ] Native model/document persistence and appropriate CAD format support.
- [ ] glTF / GLB import and export.
- [ ] FBX import and export (after checking viable GPL-compatible implementation).

## Platforms and interaction
- [ ] Linux, Windows and macOS.
- [ ] Optional cross-platform touch mode in Settings.
- [ ] Touch-friendly UI and manipulators without replacing mouse/keyboard support.
- [ ] iPadOS version.
- [ ] Android tablet version.

Project: **OpenDesigner** (without 'CAD' in the name). License: **GPL-3.0**.
