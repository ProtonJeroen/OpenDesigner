# Command panel positioning on Fedora KDE / XWayland

Investigation: 2026-10-28, branch `fix/command-panel-viewport-positioning`,
created from `develop` at `993f4d9`. No push or pull request.

## Environment and reproduction

- Fedora KDE 44; KWin 6.7.5; Qt 6.11.2; XWayland 24.1.14.
- XWayland screen: 1920x1080, primary eDP-1, origin (0,0).
- `QT_QPA_PLATFORM=xcb`; HiDPI CTests set `QT_SCALE_FACTOR=2`.
- Before changes, all four ellipse-drawing / projection-session variants failed
  in the targeted run on this machine (including normal projection).

## Root cause and measured evidence

The application placed native `Qt::Tool` dialogs immediately after `show()`.
KWin's initial configure and frame decorations arrive asynchronously. The
positioner measured an undecorated frame and could subsequently treat the WM's
initial placement as a manual move, disabling the default anchor. Even after
initial exposure, delayed configure notifications from the positioner's own
resize/move requests could restore an older client origin and disable anchoring.
This second race was reproduced by repeating the decorated-dialog test.

Measured normal-scale ellipse sequence, in logical global coordinates:

```
viewport:                   QRect(577,248 1023x660)
computed and moved frame:   QRect(1224,260 364x470)
frame at assertion:         QRect(1224,232 364x498)
client global origin:       QPoint(1224,260)
devicePixelRatio:           1
```

The late 28-pixel title bar moved the frame top above the viewport even though
its client origin remained at the requested coordinate. This is not a physical
versus logical pixel arithmetic error.

Measured HiDPI projection sequence:

```
viewport:                   QRect(257,131 703x363)
requested initial frame:    QRect(565,143 383x339)
late initial WM frame:      QRect(289,31 362x353)
anchor after late Move:     false
clamped manual frame:      QRect(289,131 383x339)
devicePixelRatio:           2
```

This explains why explicit scaling settings do not fix the problem and why
results depend on the WM/event timing on a particular machine. Qt's XCB backend
updates cached frame margins on `_NET_FRAME_EXTENTS`; the widget Show event does
not establish that native mapping and decorations are complete. See the
[Qt XCB implementation](https://github.com/qt/qtbase/blob/dev/src/plugins/platforms/xcb/qxcbwindow.cpp).

There were additional test assumptions exposed after fixing application startup:

- Ellipse checked native geometry after only one `processEvents()` call.
- Projection assumed a requested 1250x950 main window always provided enough
  logical viewport height for the natural form. At DPR 2, KWin caps it to the
  physical screen. Scrolling is then necessary and correct.
- Projection's manual test point at local (25,25) extended a 339-pixel-high frame
  one pixel outside a 363-pixel viewport. Correct clamping produced (25,24).
- The generic positioner fixture expected content over 700 logical pixels high
  to fit after restoring a top-level window on a 540-logical-pixel-high screen.

## Changes

- `src/CommandPanelPositioner.cpp`: defer initial sizing/placement until native
  exposure, then synchronize pending window-system events before measuring frame
  decorations. Initial configure events cannot become manual placement. A guarded,
  single opening callback handles deletion during event processing. An explicit
  reopen anchors again; preview updates of visible panels preserve manual positions.
  Record client origins of issued resize/move operations and consume matching
  spontaneous configure confirmations, without reclassifying them as manual moves.
  Non-spontaneous manual QWidget moves and new native positions continue to disable
  anchoring; their local positions survive previews and repeated show calls.
- `tests/SketchEllipseDrawingTest.cpp`: settle native events before geometry checks,
  using the same synchronization approach as the other GUI tests.
- `tests/SketchProjectionSessionTest.cpp`: check scrollbar necessity against actual
  logical capacity and choose an in-bounds manual test position. Frame containment
  and the original 12-pixel anchor assertion remain intact.
- `tests/CommandPanelPositionerTest.cpp`: retain two-axis overflow and restore tests
  with content that fits the actual DPR-2 screen. Add repeated exact-anchor checks
  for decorated dialogs, preservation of manual positions and reanchoring on reopen.

The placement formula, 12-logical-pixel margin, containment rules and existing
anchor assertion tolerances are unchanged. No blanket reanchoring of visible
panels is introduced.

Opt-in diagnostics remain available with `OPENDESIGNER_PANEL_TRACE=1`. They log
viewport bounds, frame geometry, widget position, global client origin, saved
local position, requested frame, DPR, anchor state and external Move events to
stderr. They are silent by default. Investigation logs are in `/tmp` and are not
repository artifacts.

## Validation

Build: `cmake --build build -j 4` succeeded.

The first full run after the initial exposure fix was 56/57 (385.36 seconds):
all original ellipse/projection failures passed, but normal command-panel
positioning revealed the delayed resize/move confirmation race. After correcting
that race, `command-panel-positioning` and `command-panel-positioning-hidpi` each
passed 10 consecutive runs (20 executions, 43.31 seconds).

Final full suite:

```sh
QT_QPA_PLATFORM=xcb ctest --test-dir build --output-on-failure
```

**57/57 passed, 0 failures, 386.24 seconds.** Normal and HiDPI ellipse drawing,
projection sessions and command-panel positioning all passed. The HiDPI entries
used their existing CTest environment `QT_SCALE_FACTOR=2`.

An additional temporary harness, `/tmp/opendesigner-native-panel-check.cpp`, was
compiled against the rebuilt `OpenDesignerCommandPanels` library. It checked the
exact 12-pixel anchor, moved a decorated tool window via
`QWindow::setFramePosition()` (rather than `QWidget::move()`), and checked that five
repeated show/update calls retained local position (25,35). The trace confirmed
that the positioner received a spontaneous Move event. Both executions passed:

- `QT_QPA_PLATFORM=xcb /tmp/opendesigner-native-panel-check`: DPR 1, exit 0.
- `QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=2 /tmp/opendesigner-native-panel-check`:
  DPR 2, exit 0.

This is an automated check of the native movement path; no human pointer drag
was performed. The existing regression also exercises the panel's drag handle
with mouse events, numeric edits, growth/shrink, parent movement and reopen.

Final logs: `/tmp/opendesigner-ctest-all-final.log`, `/tmp/panel-repeat-v2.log`,
`/tmp/opendesigner-native-panel-dpr1.log`, `/tmp/opendesigner-native-panel-dpr2.log`.
`git diff --check` passed. No push or pull request was made.
