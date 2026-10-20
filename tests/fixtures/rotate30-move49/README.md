# Exact Fedora KDE Rotate +30° / Move +49 mm regression

These are exact OCCT B-Rep payloads exported by the user from OpenDesigner 0.3.6
(OCCT 7.9.3), not proprietary SolidDesigner reference assets. Existing display
triangulations are retained. The scoped `.gitattributes` entry prevents line-ending
conversion and suppresses whitespace checks on OCCT serializer data so payload
hashes remain exact. The ZIPs themselves are not test dependencies.

| Fixture | Source payload | SHA-256 |
| --- | --- | --- |
| `unite.brep` | `preview-debug.zip`, `last-operation-before`, Part 2 | `a881896e1d2ec1e35257207e4d2189ec55ff88396c1af334237551ec97ed5a1e` |
| `rotated30.brep` | `result-debug.zip`, `last-operation-before`, Part 2 | `ba9ee6ee8878dea1487496c82dd3851f6653775e5da95e7f08fd8978c07d8c68` |
| `recorded-bad49.brep` | `result-debug.zip`, `document-part`, Part 2 | `361739231451a1ee00e02768634be9a0771c29321cbf356a46dfc7c9bead80a5` |

Part IDs above are zero-based document slots. Face indices are one-based
`TopExp::MapShapes(shape, TopAbs_FACE)` indices, as in the debug manifests.
`preview-debug.zip` contains the same rotated source and an equivalent faulty
preview; the defect is already present before OK.

1. In `unite.brep`, select face 9 (high top, z=60).
2. Rotate +30° about the +X axis through (125,80,60). This must remain
   material-equivalent to `rotated30.brep` (volume 639247.9138593196 mm³).
3. In `rotated30.brep`, select face 5, outward normal (0,-0.5,sqrt(3)/2).
4. Move +49 mm along that normal, with identity instance transform.

**`recorded-bad49.brep` is a negative control, not the expected correct result.**
It is a valid closed solid of volume 1156871.7334342287 mm³, with unwanted material
at (175,-20,50). The same material persists after fresh triangulation.

The independent oracle in `tests/RotateMoveFixture.h` keeps the low region
x=[0,125], y=[16,80], z=[0,43], and bounds the high region by x=[125,225],
y=[0,80], z>=0 and z<=60+(y-80)/sqrt(3)+98/sqrt(3).
It constructs a separate polygon/prism and Boolean union for comparison;
production reconstruction does not use this primitive/history description.
Correct volume is **1091890.5249039864 mm³**. The recorded wing adds
**64981.2085302423 mm³**, extending to y=-47.44486372867095.

The correct normalized result has 9 faces, 21 unique edges and 14 vertices.
Tests also compare actual material, requested plane, dense samples around the
wing/front/low terrace, preserved necessary faces, final mapping, Align after
the edit, a rigidly transformed copy, and visible preview/Cancel/Enter/Undo/Redo
with stable Part identity, properties and browser row. Face counts alone do not
establish correctness.
