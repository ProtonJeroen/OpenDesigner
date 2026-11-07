# v0.4.9 Phase 9F.2 — command panels across DPI scales

Baseline: `e67e7d4`, branch `feature/v0.4.9-move-cylindrical-face`.
Environment: Lenovo, Fedora KDE/Wayland, one physical monitor, Qt 6.11.2.
OpenDesigner uses Qt/xcb through XWayland because its OCCT viewport embeds an
X11 drawable. This phase changes UI placement only, with no geometry changes.

## Root cause and production correction

The old positioner combined a `mapToGlobal()` origin with QWidget sizes and
cached frame geometry, then applied a relative QWidget move. Those values are
not necessarily one consistent snapshot: Qt's global point mapping can choose a
different screen around a logical screen gap, and native geometry, screen/DPR,
frame margins and QWidget configure delivery can change in separate event-loop
turns. A delayed configure could also be mistaken for a manual drag and disable
the default anchor. The previous 100%/200% single-screen tests did not exercise
native geometry advancing ahead of QWidget's cache.

`CommandPanelPositioner` now:

- Builds viewport bounds from native window positions along the parent chain,
  plus any non-native widget offset. Reads the native viewport size when present.
- Uses QWindow frame geometry and client size consistently for decoration sizing,
  fitting and drag clamping. Selects the owner's screen before measuring/moving
  the Tool window and submits an absolute native frame position.
- Checks both QWidget and QWindow sizes. Repairs a native size even when
  `QWidget::resize()` does nothing because its cached size already matches.
- Watches native windows' screen changes and Move/Resize events, screen geometry
  and logical-DPI changes, as well as the existing widget/layout events.
- Coalesces immediate work and re-reads current geometry every 50 ms for up to
  eight reconciliation passes after opening or a viewport/screen change. New
  geometry events initiate further work; no cached target is replayed and no
  permanent polling timer runs. Native configure moves during a screen/DPR
  transition or decoration change do not replace the saved anchor/offset. Ordinary
  native placement after opening and explicit dragging can still take over.
- Compares native move acknowledgements in client coordinates and allows one
  logical pixel of native fractional-position rounding when identifying a move.

All placement distances remain Qt logical pixels, including the 12-pixel margin.
There is no multiplication or division by screen/window DPR and no physical-pixel
screen-origin conversion in application code. Qt owns those conversions. See
[Qt's coordinate model and screen gaps](https://doc.qt.io/qt-6/highdpi.html#device-independent-screen-geometry)
and [QWindow frame geometry](https://doc.qt.io/qt-6/qwindow.html#frameGeometry).
The existing content wrapper, field pointers, callbacks, focus and command-session
lifecycle remain in use. No position is persisted in native documents.

## Synthetic validation

`command-panel-cross-screen` links the same `OpenDesignerCommandPanels` library
as the application. It uses Qt 6.11's configurable offscreen platform; it does
not replace the positioner or add a production test override/private Qt API.
Older supported Qt builds retain the existing positioning tests; this additional
test is registered only with Qt 6.11 or newer.

The fixture's native geometries and `QT_SCREEN_SCALE_FACTORS` create actual
synthetic QScreen/QWindow instances with these reported logical geometries:

| Screen | Scale | Logical geometry (x, y, width, height) |
| --- | --- | --- |
| s100 | 100% | 0, 0, 1920, 1080 |
| s125 | 125% | 1920, 160, 2048, 1152 |
| s150 | 150% | -2560, -200, 1707, 960 |
| s200 | 200% | 0, -2160, 1920, 1080 |

Coverage includes all 12 directed screen pairs in screen-first and automatic
geometry-first order, native and non-native viewport widgets, negative origins,
vertical offsets and logical screen gaps, anchored decorated panels, frameless
handle dragging, title-bar drag events, window movement, docking, shrink/restore,
scroll recovery, delayed sizes and screen-before-geometry delivery.

A configure-event gate in the test delays QWidget Move/Resize delivery while
native geometry advances. Assertions inspect native panel geometry and actual
native viewport bounds. They cover a stale panel position/size and a stale native
viewport position/size. The baseline positioner fails the stale native panel
case: native frame `(464,-1989,122,82)` while the cached QWidget frame remains
`(539,-2046,340,301)`. The corrected production
positioner converges while configure delivery is still held. Queued work also
covers command destruction and hide/reopen anchoring.

## Validation results

The Release builds of `OpenDesigner`, `OpenDesignerCommandPanelTests`,
`OpenDesignerCommandPanelCrossScreenTests`, `OpenDesignerSelectionContextTests`
and `OpenDesignerMoveFaceTests` succeeded, including their dependencies. No
compiler/linker errors or warnings were reported; Ninja printed a build-log
recovery warning. No full CTest suite was run.

All **8 focused cases passed** on the final production code across three final
batches (65.79 seconds total):

| Test | Result | Seconds |
| --- | --- | ---: |
| command-panel-positioning | PASS | 1.48 |
| command-panel-positioning-hidpi | PASS | 1.56 |
| command-panel-cross-screen | PASS, synthetic | 27.67 |
| cylindrical-move-face-sessions | PASS | 2.96 |
| cylindrical-move-face-sessions-hidpi | PASS | 2.94 |
| sketch-projection-sessions | PASS | 11.54 |
| sketch-projection-sessions-hidpi | PASS | 11.85 |
| command-panel-sessions | PASS | 5.78 |

The initial focused run found an overbroad settling guard that rejected the
projection test's explicit native manual placement. The production guard was
narrowed to screen/DPR transitions and decoration changes; the original
projection assertions were retained. The synthetic test also checks native
manual placement immediately after opening.

Single-monitor GUI sessions ran on the real KDE/xcb desktop, reporting one
`HDMI-A-1` Qt screen at `(0,0,2560,1080)`, DPR 1. Existing HiDPI cases use
`QT_SCALE_FACTOR=2`; the KDE display setting was not changed. The Move Face
numeric field, drag handle and full OK/Cancel controls were visually inspected
in Qt-rendered captures. Native XWayland screen readback was black, so those
captures use QWidget rendering and **do not capture OCCT's native drawable** or
certify compositor decoration placement. Session assertions separately check
live frame geometry, focus, preview/confirm/cancel and docking/resize behavior.
This is automated single-monitor GUI verification, not human or multi-monitor
acceptance.

Local ignored evidence: `build/phase9f2-build-final.log`,
`build/phase9f2-cross-screen-build.log`, `build/phase9f2-cross-screen-final.log`,
`build/phase9f2-native-final.log`, `build/phase9f2-positioning-final.log`,
`build/phase9f2-baseline.log`, and `build/phase9f2-gui-{window,panel}.png`.

Both supplied private files stayed byte-identical and untracked:

| File | SHA-256 |
| --- | --- |
| hole.odc | d68a40d0b0ab4f22bf4097383c84979a8144d736ef73b7cadb56dfde12f9a05d |
| hole.zip | f0b6303f099f2853e15c5cb208fe60e25f7ba1284058681fe8b9a01c5cc6b323 |

No push, merge or pull request is part of this phase.

## Remaining hardware verification

Physical multi-monitor acceptance is **pending**. On Fedora KDE/KWin with two or
more real displays, repeat transitions in both directions between 100%, 125%,
150% and 200% where supported, including left/above arrangements and different
native resolutions. Exercise anchored and manually dragged native/frameless
panels while dragging the application across the boundary, spanning displays,
maximizing/restoring, resizing and toggling docks. Confirm complete frames stay
inside the visible CAD viewport, scrollbars recover, native decorations settle,
input focus and Enter/Escape remain correct, and no screen-change loop occurs.
Also check scale changes while a command is open and monitor unplug/reconnect.

The offscreen backend does not reproduce KWin's decoration timing, fractional
Wayland compositor protocol, interactive cross-boundary dragging or hotplug.
Native Wayland window positioning is not certified; the current application
continues to use xcb/XWayland. Windows/macOS remain unverified by this phase.
