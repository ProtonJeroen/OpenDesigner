# Shared modeling architecture — v0.4.8, phase 8A

Phase 8A is an architecture audit and interface contract. The current code already
shares the required profile, Boolean, target-picking and history mechanisms.
No runtime abstraction, geometry, UI or document schema change is necessary yet.
The workflow reference is SolidDesigner / Modeling 2008 v16.0; the contracts below
describe OpenDesigner behavior, without claiming exact original UI parity.

## Existing reusable boundaries

| Concern | Existing interface / owner | Reuse contract |
| --- | --- | --- |
| Profile analysis | `SketchProfileValidation::validate(entities, workplaneId, tolerance)` | Read-only local XY topology; ignores construction and other Workplanes; preserves holes, islands and region membership; refuses unsupported/invalid input. |
| Certified planar faces | `SketchProfileBuilder::build(profile, frame, policy)` | Rechecks input, builds world-space analytic faces and certifies topology, area and vertex tolerances. Reuse for both sweep types. |
| Extrusion tool | `SketchProfileExtrusion::extrude(faces, normal, signedDistance)` | Headless prism generation and certification; no Part, session, presentation or history ownership. |
| Existing extrusion result | `SketchProfileExtrusion::Result` | Success, world-space solid/compound, solid count, volume and diagnostics. Failed results must never be committed. |
| Add | `SketchProfileExtrusion::addMaterial(tool, target, placement)` | Local target plus positive rigid placement; one connected target/result; rejects disconnected or unchanged additions. |
| Cut | `SketchProfileExtrusion::cut(faces, frame, target, placement, depth, reversed, throughAll)` | Extrusion-specific reach/tool generation followed by subtraction; rejects no change and total removal; permits multiple remaining solids in the same Part. |
| Boolean kernel helpers | `BooleanGeometry::{combine, solidResult, validSolid, normalizeBooleanResult}` | Non-destructive OCCT operations, solid extraction, validation and material-preserving normalization; no command/document ownership. Shared with existing Part Boolean commands. |
| Target resolution | `CadViewport::{partIndexForId, solidTargetError, initialTargetPart, beginTargetPicking, cancelTargetPicking}` | Stable UUID resolution, preliminary eligibility and callback-based temporary picking. Kernel certification remains authoritative. |
| Preview lifecycle | `CommandSession`, `ProfileExtrusionState`, `refreshProfileExtrusion`, `finishProfileExtrusion` | Snapshot sources/original Parts, rebuild preview, revalidate on confirmation, discard on cancellation. |
| New Part transaction | `BooleanChange`, `commitBooleanChange`, `restoreBooleanChange` | Create one GeneralBRep Part, preserve its UUID on Redo and reuse stable history slots. No committed tool Part. |
| In-place transaction | `FaceChange`, `applyFaceState`, `restorePullChange` | Store complete before/after Part snapshots at the original index; preserve target identity, properties and placement. |
| Undo/Redo and storage | `PullChange`, `undoPull`, `redoPull`, native GeneralBRep persistence | One confirmed operation, existing restoration paths and schema; previews never enter persistence. |

`SketchProfileExtrusionTools.cpp` owns the current controls and command orchestration.
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

Today `ProfileExtrusionState::adding` means a target-modifying operation, including
Cut; `cutting` selects Cut-specific controls and geometry. Valid refreshed pairs
`(adding, cutting)` are `(false, false)`, `(true, false)` and `(true, true)`.
These are internal legacy flags, not a reusable public mode API. The operation
combo's index 1 means Add in Extrude and Cut in the separate Cut command.
Do not infer a future shared mode from that widget index.

When a second command needs a common mode value, introduce only a small
`ModelingOperation` enum (`NewSolid`, `AddMaterial`, `RemoveMaterial`) in a
widget-free header. Map UI choices explicitly in the command layer and use
`mode != NewSolid` for target requirements. Replace the legacy flags atomically
with their consumers; do not retain competing sources of truth. A command base
class, generic feature graph or new history stack is unnecessary.

Preserve the existing targeting distinction: Extrude starts in New Solid and
Add requires explicit target choice. Cut initializes from an eligible Active Part
using `initialTargetPart`, otherwise requests a pick. Ordinary selection is not
an Active Part fallback. Dropdown, browser and geometry picks remain temporary
and never activate a Part. Refresh verifies the selected UUID still resolves to
the captured slot, target visibility/eligibility and unchanged original shape;
source Workplane and Sketch snapshots are also checked before committing.

All sweep results and Boolean tools are world-space. Apply target placement once
for Boolean evaluation, then inverse-transform the result once for local target
storage. Rebuild bounds and GeneralBRep flags through the existing command path;
copy the original Part so identity, presentation properties and placement survive.
Never accumulate previews onto a previous result.

## Implementation boundaries for phases 8B–8E

These are recommended dependency boundaries, not additional phase requirements.

- **8B: unified Extrude orchestration.** Keep prism and Cut depth/reverse/Through
  All algorithms unchanged. If unifying controls, add the enum above and map all
  three modes explicitly; retain existing entry points and their initialization
  policies. Route through the existing profile, picker, preview and commit paths.
  Establish mode-switch behavior with the existing extrusion session tests.
- **8C: headless Revolve tool generation.** Accept certified builder faces plus
  explicit axis/angle parameters; return certified world-space solids and
  diagnostics. Axis/angle validation and revolution-specific topology checks
  belong here, with numerical tests. Do not assume prism volume certification
  or directional Through All has a Revolve equivalent.
- **8D: shared material application and Revolve command integration.** When
  Revolve needs material application, extract the existing Add and Remove code
  into a headless helper accepting mode, certified world tool, local target and
  placement. Keep extrusion reach calculation in `cut`, delegating only its
  subtraction/result checks. Share certification/result types only as required;
  retain existing extrusion API wrappers and diagnostic behavior. Do not copy
  Fuse/Cut, normalization, no-change or connectedness checks into Revolve.
  Reuse target resolution and Part transaction paths; extract a small command
  helper only if integration actually needs it.
- **8E: focused integration acceptance.** Cover each mode's preview/invalid
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
