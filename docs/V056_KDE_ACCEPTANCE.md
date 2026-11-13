# v0.5.6 candidate — Fedora KDE/Wayland acceptance

Status: **manual Fedora KDE acceptance PASS (user-reported)**. The user reports
that all requested v0.5.6 acceptance checks passed, including **200% HiDPI
picking**. This record describes reported results; it does not claim that the
agent independently performed desktop acceptance. Preserve all **nine** private,
untracked `.odc` documents; use synthetic documents or Save As copies.

## Reported manual results

| Scenario | Result / evidence |
|---|---|
| Requested v0.5.6 checks: commands, Trim, Extend, exact projection/Overdraw, profiles, lifecycle and protection | PASS — user's final overall acceptance report |
| 200% HiDPI picking | PASS — explicitly confirmed by the user |
| Extend to the first circular crossing | PASS — explicitly confirmed by the user |
| Second sequential Extend crossing on the same boundary | PASS — explicitly confirmed by the user |
| Elliptical arc Extend crossing | PASS — explicitly confirmed by the user |
| Interactive spline mouse placement, including five or more interpolation points | PASS — explicitly confirmed by the user |
| Spline live preview, Back and numeric XY entry | PASS — explicitly confirmed by the user |
| Spline Enter/OK, Cancel/Escape and Undo/Redo | PASS — explicitly confirmed by the user |
| Editing retained spline interpolation points | PASS — explicitly confirmed by the user |
| Spline Save/Open and duplication without modifying the source | PASS — explicitly confirmed by the user |
| Unconfirmed spline preview excluded from saved documents | PASS — explicitly confirmed by the user |

### Evidence boundaries and untested scenarios

The user did not supply an individual 100%/200% result matrix for every detailed
procedure below, exact Fedora/KDE versions, GPU/driver, resolution or Qt scaling
variables. Do not infer those details from the overall PASS or automated tests.
Native Qt Wayland embedding, physical cross-monitor scaling and a 10,000-point
interactive spline stress test are **untested in this reported acceptance**.
Native Wayland embedding remains unsupported: the OCCT viewport uses Qt/xcb
through XWayland on KDE/Wayland.

The sections below retain the reproducible acceptance procedures, including
edge-case exercises; they are not additional per-scenario execution records.
For future reruns, record build, hardware and scale settings. Prefer KDE display
scaling with `QT_SCALE_FACTOR` unset, or record KDE 100% plus
`QT_SCALE_FACTOR=2` separately. Avoid combining multipliers and restart after
changing scaling. Do not force `QT_QPA_PLATFORM=wayland`.

## Commands and interaction

1. With no active Workplane, verify Trim, Extend and Overdraw are disabled in
   **Modeling → 2D Modeling** and the 2D toolbar. Activate one visible, unlocked
   Workplane with Edit Sketch. Verify each menu/toolbar pair invokes the same
   command, without duplicate actions. No preselection is required.
2. Hide or lock the active Workplane, then use Finish 2D. Each operation must
   cancel pending candidates and disable these actions. Showing/unlocking a
   Workplane must not edit it; explicitly reactivate it before continuing.
3. During each command, orbit/pan/zoom without confirming geometry. Check clear
   hover feedback, readable prompts, full OK/Cancel buttons and usable panel
   placement. A click at overlapping entities must request a clearer choice.

## Trim

Use a fresh Sketch, or hide unrelated coincident curves between exercises.

1. Draw `(-60,0)→(60,0)` and Construction boundaries at X=-20 and X=20. Activate
   Trim and click `(0,0)`. Blue must retain the two outside portions. Cancel,
   repeat and confirm. Check one Undo/Redo restores the exact original/result.
2. Remove a tail with one crossing. Try no interior crossing, a tangent-only
   contact, overlap and a click exactly at a cut. Expect feedback and no partial
   mutation or enabled OK for an invalid candidate.
3. Draw a radius-40 circle cut by X=20. Click the rightmost portion. Confirm the
   correct remaining major arc, not its complement. Repeat on an ellipse with
   radii 50 and 30, rotated and unrotated, including across its parameter seam.
4. Draw an interpolating spline through `(-60,0),(0,40),(60,0)` with boundaries
   X=-20 and X=20. Remove the middle portion. Retained fragments must follow the
   original curve exactly and become coefficient B-splines; there is no new
   interpolation through fewer points. Repeat with a supported periodic spline.

## Extend

1. Draw `(-60,0)→(0,0)`, with finite boundaries at X=-80 and X=20. Choose Extend,
   click near the right endpoint, then the X=20 boundary away from its crossing.
   Verify the marker identifies the selected endpoint and the blue candidate
   ends at X=20. Repeat from the left endpoint to X=-80.
2. Include a nearer unselected boundary: the explicitly selected boundary must
   control the result. Clicking the middle of the source must not choose a side.
3. Extend circular/elliptical arcs in both directions and with reversed traversal.
   Include a parameter-seam crossing. The opposite endpoint, support, orientation,
   entity ID and metadata must stay intact. Full turns are not invented.
4. Select a boundary giving two forward crossings. Extend must select the first
   strictly forward certified intersection using directed curve-parameter progress,
   not Euclidean distance. Confirm, then repeat from the new endpoint with the
   same boundary to reach the next crossing; stop when the supported continuation
   is exhausted. An existing endpoint contact must not create a zero-length
   extension. Indistinguishable roots, tangencies, coincident supports and
   uncertifiable geometry remain safe refusals; multiple distinct certified
   crossings alone are not a refusal. No additional full revolutions are invented.
   Also try a finite boundary missing the support, an interpolating spline and a
   coefficient fragment beyond its knot-domain limit: refuse.
5. Extend a trimmed coefficient fragment within its existing support domain.
   Check unchanged poles, weights and knots, and distinguish a valid open Sketch
   result from a closed Profile that still needs certification.

## Interactive multipoint spline

1. Activate a visible, unlocked Workplane and choose **2D Modeling → Interpolating
   Spline**. Click point 1, then point 2: show a provisional straight segment and
   keep OK disabled. Point 3 enables OK only when exact interpolation is valid.
2. Continue to five or more points. Each click appends one interpolation point;
   no fixed click count automatically finishes drawing. Move the mouse to update
   the live exact candidate without accepting another point. Back removes the
   last accepted point; **Use XY point** explicitly appends numeric coordinates.
3. Press Enter/OK with at least three valid accepted points. Confirm only those
   accepted points, excluding the temporary cursor and unaccepted numeric fields.
   The result is one exact interpolating spline, not a polyline. Escape/Cancel
   discards the pending spline; confirmation is one Undo/Redo transaction.
4. Edit the retained interpolation points using **Edit Curve / Trim Parameters**.
   Check Save/Open, duplication with an unchanged source, and saving during
   preview: unconfirmed geometry must be excluded from the saved document.

The existing 10,000-point interpolation validation limit and coefficient B-spline
limits remain unchanged. The reported manual test covered five or more points,
not stress testing at the limit.

## Exact oblique projection and Overdraw

1. Create a cylinder of radius 40 (diameter 80) and height 10. Create a Workplane
   through its cap center, initially parallel to that cap, then rotate the
   Workplane 60 degrees about its local X axis. Project the circular cap rim with
   Project 3D Geometry. The resulting Construction ellipse has major radius 40
   and minor radius 20. Inspect its exact values with Edit Curve, then Cancel.
2. Look at the Workplane. Add a Construction diameter through the ellipse center
   along its major axis, extending beyond both ends. Overdraw from one meeting
   point along the upper ellipse portion, then the lower portion. Green denotes
   the proposed piece and blue the chosen path. Reverse the walk and repeat with
   a projected elliptical arc. Neither route may become its complementary arc.
3. Confirm, Undo/Redo and repeat Overdraw over the same confirmed Profile portions.
   Reuse must preserve existing entity IDs/metadata and add no duplicate entities
   or Undo step when the collection is unchanged. Construction sources remain.
4. Repeat with supported interpolating and standalone projected B-splines.
   Inspect exact coefficient fragments and reversed traversal. Ambiguous or
   differently parameterized overlaps without a proof must fail atomically.
5. Move/rotate the Workplane and repeat the picks. Move or delete the original
   cylinder after confirming the projection: independent projected/Overdraw
   Sketch values must remain unchanged. Sketch operations must not modify the
   cylinder's geometry, placement or metadata.

## Mixed profiles, holes and the reuse limitation

1. Build an elliptical contour with a smaller circular/elliptical hole. Use New
   start for the inner contour, preserving both walks. Include a disjoint region
   and a supported mixed line/spline contour. Existing Profile Builder must retain
   holes and regions; open, crossing or unsupported contours must be refused.
2. For partial reuse, keep an existing Profile line `(-40,0)→(40,0)`. Build a
   Construction rectangle with corners `(-10,0),(10,0),(10,20),(-10,20)` and
   Overdraw it. The covered base reuses the **whole** existing Profile line.
   Its outside tails are retained, and the panel warns that they may prevent
   profile construction. Check that an unrelated closed contour is untouched.
3. Do not expect Overdraw to delete tails or change their role. Splitting an
   existing Profile while retaining every piece leaves those tails in the same
   Workplane-wide Profile set. Adding an overlapping bounded copy duplicates
   geometry; removing/reclassifying tails could damage another contour. A safe
   resolution needs explicit contour ownership or user-directed edits and is
   deferred beyond Phase 5. Manually edit a copy if a different topology is needed.

## Lifecycle, persistence, protection and cleanup

1. For every command, inspect blue Preview before confirming. Use both OK and
   Enter, both Cancel and Escape. Confirmed changes use one Undo step; Cancel
   preserves both stacks. Trim/Extend permit one operation per confirmation.
2. Switch Trim → Extend → Overdraw → drawing tools during preview. No old blue
   objects, green hover, markers, panels or manipulators may remain. Changing
   sources or the Workplane cancels stale evaluated commands.
3. Save during preview, then Open a copy: only confirmed geometry is stored.
   Confirm, Save/Open again and inspect domains, orientation, roles and IDs.
   Duplicate a selected curve using the existing command; check independent IDs,
   exact values, Cancel and one-step confirmation. Coincident Profile copies may
   be invalid for solid construction until explicitly edited.
4. Hide/lock sources and boundaries, including on another Workplane. They cannot
   be edited or used as picked operands. Existing hidden/locked Profiles may
   prevent duplicate Overdraw geometry, but their values and flags stay unchanged.
5. Finish 2D after an active preview. Select a Part face, an edge and a whole Part
   with Alt; verify normal picking returns with no stale Sketch owner or tool.
6. Record failures with the exact workflow, build and scaling settings. Desktop
   results above are user-reported; automated xcb/HiDPI sessions do not certify
   native compositor behavior or physical cross-monitor scaling.

Known limits: full closed Overdraw sources need two distinct boundaries;
uncertifiable geometry and self-crossing spline supports are refused; spline
extrapolation is unsupported; rational/higher-degree coefficient B-spline
Profiles retain their existing construction refusals. No tolerance is loosened.

## Automated validation record for the candidate

The earlier version-only incremental application build passed:

```sh
cmake --build build --target OpenDesigner -j4
```

After the earlier shared intersection adapter changes, the focused check passed
3/3 tests (Trim 0.19 s, Extend 0.16 s, Profile Validation 0.09 s):

```sh
cmake --build build --target OpenDesignerSketchTrimTests OpenDesignerSketchExtendTests OpenDesignerProfileValidationTests -j4
ctest --test-dir build -R '^(sketch-trim-geometry|sketch-extend-geometry|sketch-closed-profile-validation)$' --output-on-failure -j2
```

The following are historical passing results from the earlier implementation
phases; they do not imply that every shared session file remained unchanged:

```sh
# Exact geometry and directly affected existing Overdraw coverage (Phase 4)
ctest --test-dir build -R '^sketch-advanced-overdraw-geometry$' --output-on-failure -j2
ctest --test-dir build -R '^(sketch-overdraw-geometry|sketch-curve-operations)$' --output-on-failure -j2
ctest --test-dir build -R '^(sketch-overdraw-sessions|sketch-overdraw-traversal)$' --output-on-failure -j1
# Final shared UI integration, normal and 200% Qt scaling (Phase 5)
ctest --test-dir build -R '^sketch-curve-workflows(-hidpi)?$' --output-on-failure -j1
```

These passed 1/1, 2/2, 2/2 and 2/2 respectively; the workflow sessions took
16.80 s and 16.99 s. Direct Trim, Extend and Overdraw session checks also passed
in Phase 5. Desktop sessions used the existing xcb/X11 display, with desktop
sandbox access. No full local CTest suite was run. GitHub Actions supplies the
broader regression check; its current status is reported on the pull request.
Subsequent focused validation (already run before this documentation update):

- Sequential Extend correction: `sketch-extend-geometry`,
  `sketch-extend-sessions`, `sketch-curve-operations`: **3/3 PASS**, 14.34 s.
- Interactive spline improvement: `sketch-drawing`,
  `sketch-advanced-curve-sessions`, `sketch-advanced-curve-sessions-hidpi`:
  **3/3 PASS**, 55.49 s. Incremental application/session-test builds passed.

Manual Fedora KDE acceptance is **PASS (user-reported)** as scoped above.
No local builds or tests were run for this documentation-only update.
