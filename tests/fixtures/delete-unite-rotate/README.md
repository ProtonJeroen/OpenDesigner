# Delete the transition triangle and front strip after Unite / Rotate

Exact OCCT B-Reps from the user's `opendesigner-debug.zip` (2026-10-20,
OpenDesigner 0.3.6 / OCCT 7.9.3). The archive contains two active, separate Parts,
and its last applied operation is Rotate Face +30 degrees on Part 2. Selection
is empty in the archive. The targets below match the two visible patches in the
accompanying screenshot; their support planes identify them geometrically.

| Fixture | Archive payload | SHA-256 |
| --- | --- | --- |
| low.brep | shapes/00000.brep, Part 1 | f294c8d16dadf38360881c68b64b520126a988a18ff75cb5d236c0c30e663665 |
| rotated-high.brep | shapes/00001.brep, Part 2 | 1a7f2fbe9be33d140a6afc2e29209074141f8b0c2dd50b85477b9e304b48312e |

The user explicitly chose to Unite these Parts before deleting Faces. A separate
Box cannot delete its end cap using another Part's surface, and must still fail
without any partial cross-Part edit.

After Unite the targets are:

- The small +X transition triangle on x=125. In the low Part before Unite this
  is only the exposed portion of its entire end face, not a separate triangle.
- The high Part's narrow -Y front face on y=0.

The low section is x=[0,125], y=[16,80], z=[0,39]. The high section is
x=[125,225], y=[0,80], z=[0,h(y)], where h(y)=60+(y-80)/sqrt(3).
Removing the triangle lets the inclined support trim the low top and front;
removing the strip extends the high inclined top until it meets z=0.

The independently constructed expected material is:

- Low: 0<=x<=125, 16<=y<=80, 0<=z<=min(39,h(y)).
- High: 125<=x<=225, 80-60*sqrt(3)<=y<=80, 0<=z<=h(y).

The low and high volumes are 284458.6807038386 and 311769.1453623979 mm³;
total volume is **596227.8260662365 mm³**. Tests construct separate polygonal
prisms from these equations, compare Common material equivalence and independent
occupancy samples, and exercise rotated coordinates, reversed selection, the
triangle alone and an unbounded negative control. The session regression covers
actual Unite, Delete preview/Cancel/OK, stable united Part identity and cached
Undo/Redo with complete selection cleanup.

## Sequential deletion regression

The follow-up exports `opendesigner-debug1.zip` and `opendesigner-debug2.zip`
contain a related variant with low y=[22,80], z=[0,43]. Both fixture payloads are
the active document Part 2 at `shapes/00002.brep` (zero-based Part ID).

| Fixture | Source | SHA-256 |
| --- | --- | --- |
| sequential-before.brep | opendesigner-debug1.zip | d5b4344d952699d8e515841f07885f20304f0f1b47bdae14e18b62eca8782819 |
| sequential-after-first.brep | opendesigner-debug2.zip | d4171919e85d393ab766a8ffae58d4e5778811b914be7acedb10ef179b60c9ea |

The first deletion removes the +X triangle (index 8 before removal). The second
archive records that successful operation, with volume 577574.852050488 mm³;
its remaining -Y strip is index 2. Deleting this strip previously failed because
the vanished triangle was needed to infer an opposite-facing step axis.

The final independently expected material uses the same high prism as above and
the low profile 0<=x<=125, 22<=y<=80, 0<=z<=min(43,h(y)). Its low volume is
282326.9381911683 mm³; total volume is **594096.0835535661 mm³**.
Tests cover this exported intermediate solid without command history, both
deletion orders from the original, a transformed intermediate, refusal of an
unbounded deletion, and two distinct cached Undo/Redo steps through the UI.

## Inclined L-shaped transition

`inclined-l-step.brep` is the active Part 3 (`shapes/00003.brep`) from
`opendesigner-debug5.zip`, supplied 2026-10-20. SHA-256:
`3f20a7171bff754b5e0f9667f50389f8161a0414be5fd6619d4c3120bdec61e5`.
The selected face is index 6: the -X L-shaped transition at x=125.

With t=tan(15 degrees), the low section occupies 36<=x<=125,
36-(x-36)t<=y<=80, 0<=z<=45+(x-125)t. The high section is the box
(125,0,0)..(225,80,60). Deletion extends the low front/top supports through the
high section, retaining its front and top after x=36+36/t=170.35382907248 and
x=125+15/t=180.98076211353 respectively. Thus the independent expected material is:

- 36<=x<=225;
- max(0,36-(x-36)t)<=y<=80;
- 0<=z<=min(60,45+(x-125)t).

The result has 8 faces and volume **601739.4919736391 mm^3**, obtained by
integrating the width times height over the three intervals split at the above
crossings. Tests build a separate footprint prism and ramp prism, intersect them,
compare material, and probe both sides of each crossing. Rigid geometry and Part
instance transforms, reversed selection orientation, unbounded and ambiguous-axis
negative controls, refusal when a shallow top would erase an unselected high top,
preview/Cancel/OK and repeated cached Undo/Redo are covered.
