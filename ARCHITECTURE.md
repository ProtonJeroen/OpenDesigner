# OpenDesigner architecture — verification-first notes

The selected modeling framework, CAD/mesh kernel, rendering API, application UI toolkit, source layout and test framework **have not been verified from the GitHub main branch**. Do not invent them.

## Reported local development history
- Fedora KDE local build was reported to run with `cmake --build build -j$(nproc)` and `./build/OpenDesigner`.
- A bug was fixed locally: interacting with an arrow on the lower face previously moved the upper face instead.
- Next planned primitive: sphere.
These are earlier conversation reports, not independently verified commits.

## Inspect in code
1. App/windowing toolkit and rendering backend.
2. Modeling representation and kernel, meshes/B-rep/topological identity.
3. Scene/document organization and serialization.
4. Picking, face selection, manipulators and transform command semantics.
5. Primitive creation and display.
6. Build toolchain and automated checks.

## Design guardrails
- Prefer separation of geometry, commands, interaction and rendering when the existing architecture supports it.
- Maintain stable selected-face targeting.
- Keep touch interaction optional and avoid duplicating geometry logic per input mode.
- Validate GPL-compatible licensing before adding third-party libraries.
