# Unite seam regression

Exact OCCT 7.9.3 B-Reps from user-supplied `opendesigner-debug4.zip` (2026-10-20).

| Fixture | Export entry | SHA-256 |
| --- | --- | --- |
| left.brep | shapes/00000.brep | c3e6443bab874a9d8d29f10cfa28fcd792e1dc491f017aa145c0f2ceeefb84cb |
| right.brep | shapes/00001.brep | 87967f24b9d4877d72cb9bab90c414622f2debe5de7ce23f4c6dc585acf3ff00 |
| before-cleanup.brep | shapes/00002.brep | a4187d2d121a9ce2fd4fd4ff218a52d973ebd6dfff013265be3b5ca86836c13c |

Left box: `(0,14,0)..(125,80,46)`; right box: `(125,0,0)..(225,80,60)`.
The old Unite has 1 valid solid, 11 faces, 2 redundant coplanar dividing edges and
volume 859500 mm^3. Cleanup must preserve that material and the height/front
steps, yielding 9 faces and no coplanar dividing edges.
The equal-height variant (left height 60) must have 8 faces and volume 975000 mm^3.
