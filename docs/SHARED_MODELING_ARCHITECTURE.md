# Shared modeling architecture — v0.4.8, phases 8A–8E

Phase 8A established the reuse contract for existing profile, Boolean,
target-picking and history mechanisms without runtime changes. Phase 8B adds
the shared operation enum and unified Extrude command orchestration described
below; geometry algorithms, history and the document schema remain unchanged.
Phase 8C adds the headless revolution kernel and shares the existing sweep result
and solid certificate; Extrude geometry behavior remains unchanged.
Phase 8D adds [Revolve New Solid](REVOLVE.md) through the shared profile-sweep
command lifecycle. Phase 8E adds the same operation/target controls as Extrude and
reuses its in-place transaction. Revolve's certified tool uses existing Add and
extracted Cut material application; the sweep geometry kernels remain unchanged.
The workflow reference is SolidDesigner / Modeling 2008 v16.0; the contracts below
describe OpenDesigner behavior, without claiming exact original UI parity.

## Existing reusable boundaries

| Concern | Existing interface / owner | Reuse contract |
| --- | --- | --- |
| Profile analysis | `SketchProfileValidation::validate(entities, workplaneId, tolerance)` | Read-only local XY topology; ignores construction and other Workplanes; preserves holes, islands and region membership; refuses unsupported/invalid input. |
| Certified planar faces | `SketchProfileBuilder::build(profile, frame, policy)` | Rechecks input, builds world-space analytic faces and certifies topology, area and vertex tolerances. Reuse for both sweep types. |
| Extrusion tool | `SketchProfileExtrusion::extrude(faces, normal, signedDistance)` | Headless prism generation and certification; no Part, session, presentation or history ownership. |
| Shared sweep result | `SketchProfileSweep::Result` (also aliased as `SketchProfileExtrusion::Result` and `SketchProfileRevolution::Result`) | Success, world-space solid/compound, solid count, volume and diagnostics. Failed results must never be committed. |
| Revolution tool | `SketchProfileRevolution::revolve(faces, worldAxis, signedDegrees)` | Headless OCCT rotational sweep and certification; see [Revolve geometry](REVOLVE_GEOMETRY.md) for supported axes and safe refusals. |
| Add | `SketchProfileExtrusion::addMaterial(tool, target, placement)` | Local target plus positive rigid placement; one connected target/result; rejects disconnected or unchanged additions. |
| Cut | `SketchProfileExtrusion::cut(faces, frame, target, placement, depth, reversed, throughAll)` | Extrusion-specific reach/tool generation followed by subtraction; rejects no change and total removal; permits multiple remaining solids in the same Part. |
| Remove | `SketchProfileExtrusion::removeMaterial(tool, target, placement)` | Shared certified-tool subtraction extracted from Cut Extrude; no change and complete removal are refused, multiple surviving solids allowed. |
| Boolean kernel helpers | `BooleanGeometry::{combine, solidResult, validSolid, normalizeBooleanResult}` | Non-destructive OCCT operations, solid extraction, validation and material-preserving normalization; no command/document ownership. Shared with existing Part Boolean commands. |
| Target resolution | `CadViewport::{partIndexForId, solidTargetError, initialTargetPart, beginTargetPicking, cancelTargetPicking}` | Stable UUID resolution, preliminary eligibility and callback-based temporary picking. Kernel certification remains authoritative. |
| Preview lifecycle | `CommandSession`, `ProfileSweepState`, `refreshProfileSweep`, `finishProfileSweep` | Snapshot sources/original Parts, rebuild preview, revalidate on confirmation, discard on cancellation. |
| New Part transaction | `BooleanChange`, `commitBooleanChange`, `restoreBooleanChange` | Create one GeneralBRep Part, preserve its UUID on Redo and reuse stable history slots. No committed tool Part. |
| In-place transaction | `FaceChange`, `applyFaceState`, `restorePullChange` | Store complete before/after Part snapshots at the original index; preserve target identity, properties and placement. |
| Undo/Redo and storage | `PullChange`, `undoPull`, `redoPull`, native GeneralBRep persistence | One confirmed operation, existing restoration paths and schema; previews never enter persistence. |

`SketchProfileExtrusionTools.cpp` owns shared sweep controls, previews and commits.
`SketchProfileRevolutionTools.cpp` supplies the Revolve axis/angle controls and
selection adapter, reusing `Construction::resolve` for linear references.
`SketchProfileExtrusion.cpp`, `SketchProfileBuilder.cpp` and `BooleanGeometry.h`
own geometry. `BooleanOperations.cpp::calculateBoolean` also owns Part selection,
consumption and history policy: do not invoke it with a temporary sweep Part.
Reuse its kernel helpers instead.

## Three-mode contract

| Operation | Target | Geometry | Commit |
| --- | --- | --- | --- |
| New Solid | None; ignore Active Part | Certified sweep tool | One new Part, possibly containing multiple solids |
| Add Material | Exactly one eligible Part | Fuse target and certified sweep tool | Replace target in place |
| Remove Material | Exactly one eligible Part | Subtract certified sweep tool from target | Replace target in place |

Phase 8B implements a widget-free `ModelingOperation` enum (`NewSolid`,
`AddMaterial`, `RemoveMaterial`) and stores it in `ProfileSweepState` instead
of the former `adding`/`cutting` flags. Operation combo item data maps explicitly
to the enum. One `sketch.extrude` QAction serves the menu and toolbar; the separate
Cut entry point/action is removed. Geometry APIs remain unchanged.

`setExtrusionOperation` restores any hidden Remove target before changing mode,
clears temporary target state, updates relevant controls, then rebuilds preview.
Entering Add or Remove resolves an eligible Active Part with `initialTargetPart`,
otherwise starts the shared picker. Add additionally rejects multi-solid targets
at selection time, consistent with the existing geometry policy. Each mode change
resolves Active Part afresh; explicit target overrides last within the current
mode. New Solid ignores Active Part and ends pending picking. No ordinary
selection fallback is used. Temporary picks never change Active Part.

Remove-only region, Blind/Through All and Reverse controls retain values while
hidden; their values have no effect on New Solid/Add. The existing signed distance
serves New Solid/Add; Remove uses positive Blind depth or computed Through All
reach. Failed previews remove candidates and disable confirmation. Refresh checks
source snapshots, target UUID/slot, visibility, eligibility and original shape.

All sweep results and Boolean tools are world-space. Apply target placement once
for Boolean evaluation, then inverse-transform the result once for local target
storage. Rebuild bounds and GeneralBRep flags through the existing command path;
copy the original Part so identity, presentation properties and placement survive.
Never accumulate previews onto a previous result.

## Implementation boundaries for phases 8B–8E

These are recommended dependency boundaries, not additional phase requirements.

- **8B: unified Extrude orchestration.** Keep prism and Cut depth/reverse/Through
  All algorithms unchanged. If unifying controls, add the enum above and map all
  three modes explicitly; expose one Extrude entry point. Add and Remove both
  resolve Active Part or request a direct pick (implemented in phase 8B). Route through the existing profile, picker, preview and commit paths.
  Establish mode-switch behavior with the existing extrusion session tests.
- **8C: headless Revolve tool generation.** Accept certified builder faces plus
  explicit axis/angle parameters; return certified world-space solids and
  diagnostics. Axis/angle validation and revolution-specific topology checks
  belong here, with numerical tests. Do not assume prism volume certification
  or directional Through All has a Revolve equivalent.
- **8D: Revolve New Solid command integration (implemented).** Share certified
  source snapshots, profile-region filtering, the Command Panel/preview lifecycle
  and New-Part history path with Extrude. Axis selection uses existing AIS owners
  and `Construction::resolve`, without changing ordinary operand selection. The
  kernel remains headless and unchanged. See [Revolve](REVOLVE.md).
- **8E: Revolve Add/Remove and focused acceptance (implemented).** Share Extrude's
  operation/target controls and in-place transaction; delegate certified tools to
  the existing material functions. Cover each mode's preview/invalid
  preview/Cancel/confirm, transformed targets, source preservation, UUID/property
  retention, one-step Undo/Redo and native roundtrip. Retain separate headless
  geometry tests and desktop session tests, including HiDPI when controls change.

Keep existing safe refusals: Add requires one connected solid; Remove can split
the target but cannot remove it entirely; unchanged material operations fail.
No healing, multi-target operation, persistent feature links, Revolve geometry or
UI redesign is introduced in 8A.

## Targeted validation

The existing `sketch-profile-extrusion` test exercises prism/Add/Cut geometry.
`profile-extrusion-sessions` and its HiDPI variant cover preview lifecycle,
Active Part and explicit picking, cancellation, history, identity and persistence.
For this documentation-only phase, reuse the existing build and these regression
checks; no full suite or unrelated rebuild is needed. Desktop automation does
not constitute manual Fedora KDE interaction acceptance.

Validation on 2026-11-01: building only `OpenDesignerProfileExtrusionTests` and
`OpenDesignerSelectionContextTests` in the existing `build` directory succeeded.
The three tests above passed (3/3, 20.62 seconds) against the completed build.
GUI tests initially aborted at sandboxed startup and passed with desktop access.
`git diff --check` passed. No full CTest suite was run.

Phase 8B validation on 2026-11-01: incremental builds of `OpenDesigner`,
`OpenDesignerProfileExtrusionTests` and `OpenDesignerSelectionContextTests` in
the existing Release build succeeded. `sketch-profile-extrusion` passed (3.04 s);
normal and HiDPI `profile-extrusion-sessions` passed (2/2, 22.24 s) with desktop
access. Session coverage now includes the single menu/toolbar action, three-mode
switching, Active Part precedence for Add/Remove, explicit overrides, hidden and
absent targets, stale-preview removal, signed distance after Through All, Cancel,
identity/placement/material, Undo/Redo and native roundtrip. The panel check waits
for native layout and verifies full control reachability through scrolling when
HiDPI constrains the viewport, with no horizontal overflow and the existing
12-pixel anchor. Initial test failures were corrected by reactivating the Sketch
after a visibility change and respecting the existing scroll wrapper's overflow
contract. No geometry checks were relaxed. `git diff --check` passed; no full
CTest suite or stress tests were run. Manual desktop acceptance was pending at phase 8B; phase 8F records the user's
35/35 Fedora KDE acceptance in [Revolve](REVOLVE.md#final-integration-and-manual-fedora-kde-acceptance).
