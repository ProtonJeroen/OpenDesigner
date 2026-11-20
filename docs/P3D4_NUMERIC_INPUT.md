# P3D.4 — Move Face numeric input scheduling

Typed numeric input in a planar Move Face session with an exact P2 source snapshot uses a parent-owned, precise, single-shot Qt timer (300 ms). Each edit immediately clears the certified receipt, candidate and pending FaceChange, restores the source presentation, and restarts the timer. Invalid input is handled immediately without reconstruction. The displayed text changes immediately.

The timer captures the exact source snapshot, source-state object, selected Face, selected owners and Part selection, text, and a session-local generation. Before evaluation these must still match. Unknown or changed context refuses evaluation. Cancellation, restart, source-revision events, explicit preview and direction/reference updates stop pending work. A source or selection mismatch cannot commit a stale result.

OK/Enter cancels and consumes the pending request, then runs the existing synchronous certified confirmation path for the current value. A receipt is reusable only through the unchanged P2 matching checks. Pending OK is enabled to request certification; the pending state itself has no committable geometry. There are no provisional previews, worker threads or overlapping OCCT operations. A generation/source-state guard precedes publication of a reconstruction result. Geometry reconstruction and material certification are unchanged.

Manipulator and explicit reference updates remain immediate. Pull and other commands, cylindrical/blend sessions without the required exact source snapshot, and By Points input do not use this timer. Programmatic formatted echoes of a certified manipulator value do not schedule another preview; a changed value invalidates its receipt and confirmation retains its full-validation fallback.

## Focused validation

`move-face-numeric-input` covers rapid `15` (one evaluation), a pause after `1` (two evaluations), `-15`, correction, receipt invalidation, immediate Enter and OK, Cancel/Escape, selection/source/placement changes, restart, changed source Part, explicit preview and reference updates, identity, source immutability and Undo/Redo. Existing `move-face-preview-reuse` and `spline-move-face-session` passed, including their geometry and persistence checks. The ordinary `move-face` regression passed (56.53 s), including actual arrow dragging and reference/rotation interaction. Its old synchronous typed-preview assertions were updated to await the timer without removing their geometry/presentation checks. Focused results: `move-face-numeric-input` 14.28 s, `move-face-preview-reuse` 27.05 s, `spline-move-face-session` 9.44 s; all passed. Builds used only the Move Face test target with `-j4`; CTest used `-j2`.

## Read-only private-model timing

Three trials on the current Part 2, height 60 mm, displacement +15 mm; accepted height 75 mm. No private geometry is included here. The source document hash was unchanged. The first trial additionally verified topology/tolerances, exact spline coefficients/knots/multiplicities, Face correspondence, directional material differences, unchanged bottom and protected material. Each trial checked certified confirmation, identity/placement and Undo/Redo.

| Measurement | P3D.3 median | P3D.4 median |
| --- | ---: | ---: |
| Rapid `1` then `5`, certified requests | 2 | 1 |
| Rapid typing through final preview | 9.829 s | 5.413 s |
| One atomic `15` input through preview | 5.566 s | 5.152 s |
| Confirmation after certified preview | 103.845 ms | 109.767 ms |

Rapid typing trials: 5.473, 5.406, 5.413 s. Atomic-input trials: 5.152, 5.126, 5.163 s. Confirmation: 112.794, 109.767, 98.786 ms; no reconstruction and one P2 reuse each. Individual rapid key handlers had medians 132.261 ms and 56.649 ms, dominated by existing presentation/event work, with no certified reconstruction until the timer. Stable input intentionally waits 300 ms; explicit OK/Enter and manipulator updates do not wait.

The saved P3D.3 baseline was reused rather than rebenchmarked. No improvement in the unchanged geometry kernel or confirmation is claimed. One focused regression process overlapped the beginning of the optional timing run; these are automated GUI-scenario measurements, not a guarantee of manual responsiveness on every machine. Removing the unnecessary intermediate certified request explains the principal benefit. A user pause exceeding the debounce still requests an intermediate certified preview. Once synchronous certification starts, the UI remains blocked for approximately five seconds; queued newer input cannot interrupt it.

The next smallest kernel optimization remains the separately identified repeated root-solid validation in `BooleanGeometry::certifiedSolid`, subject to its own approval and compatibility checks. It was not changed in P3D.4.
