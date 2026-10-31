# PR #65: Ubuntu CI B-Rep evidence mutation

## Evidence and environment

The failed [PR run 41213676062](https://github.com/ProtonJeroen/OpenDesigner/actions/runs/41213676062)
checked merge commit `ccac194` (head `df7cafe`, base `d7d2c51`). Its full job log
contains 58 passes and these failures:

| Test | Actual terminating assertion |
| --- | --- |
| move-face | preview modified document geometry |
| delete-face-session | failure changed document/history |
| debug-model-export | Undo after export failed |

The earlier Move Face null-offset/status-0 diagnostics are not the terminating
failure. The geometry phase completes, including its primitive cap and Boolean
material assertions, before the test fails in the UI preview phase. OCCT 7.6 can
return no native per-face offset result; the existing certified reconstruction
fallback handles this. No Move Face kernel algorithm change is needed here.

CI is Ubuntu 24.04, OCCT `7.6.3+dfsg1-7.1build1`, Qt `6.4.2+dfsg-21.1build5`,
Xvfb/xcb with software OpenGL. Local Fedora has OCCT 7.9.3 and Qt 6.11.2.
[develop run 41181101802](https://github.com/ProtonJeroen/OpenDesigner/actions/runs/41181101802)
passed 60/60 at `d7d2c51`, including all three tests, with the identical Ubuntu
OCCT/Qt package builds. The failing tests and Move Face implementation are
unchanged from develop. This is a PR rendering regression exposed by older OCCT,
not an already-failing develop test or a newly broken Move Face kernel path.

## Root cause and fix

PR #65 adds sharp feature edges to PartPresentation. On OCCT 7.6,
`StdPrs_WFShape::Add` stores Polygon3D display caches on the edges it receives.
The feature compound referenced canonical document edges, so selecting/rendering
a part also modified B-Rep evidence shared by document and history snapshots.

An isolated Ubuntu probe found classification itself byte-preserving. Rendering
changed `Polygon3D 0` to `Polygon3D 12` for a box, adding corresponding edge-cache
references. Serialization without display data remained identical. The ordinary
full BRepTools::Write snapshots used by the tests correctly detected this side
effect, even though exact geometry and the undo stack were not the cause.

The renderer now detaches only the display-edge topology/caches using
`BRepBuilderAPI_Copy(features, false, false)`. Exact curves remain shared; there
is no solid geometry copy, extra AIS object, or change to picking. OCCT can build
its line caches on these temporary display edges without altering the document.
Colors, widths, silhouettes, continuity filtering and depth handling are unchanged.

All original geometry, serialization and history assertions remain intact. A new
Active Part regression checks full serialized B-Rep evidence, including existing
meshes, before and after rendering active/selected feature colors and clearing
the feature highlight.

## Validation

An isolated Ubuntu 24.04 container with the exact CI OCCT/Qt package versions
reproduced all three failures before the fix. Only the affected test executables
and the directly affected Active Part regression were built and run afterward.

| Targeted test | Fedora OCCT 7.9.3 / Qt 6.11.2 | Ubuntu OCCT 7.6.3 / Qt 6.4.2, Xvfb |
| --- | --- | --- |
| move-face | Passed | Passed |
| delete-face-session | Passed | Passed |
| debug-model-export | Passed | Passed |
| active-part-sessions (new cache regression) | Passed | Passed |

Targeted CTest elapsed time: Fedora 84.95 s; Ubuntu 67.73 s. The Fedora application
also built successfully. The Ubuntu runs used a read-only source mount and an
isolated build tree; no host dependency packages were changed. Original evidence,
geometry, preview, history and export assertions all passed unchanged.

No full suite, stress tests, remote workflow dispatch, push or new PR was run.
The next GitHub Actions run on the actual hosted runner must verify all three
formerly failing tests plus the rendering regression; the local Ubuntu replica
is evidence of compatibility, not a claim that the remote check has passed.
