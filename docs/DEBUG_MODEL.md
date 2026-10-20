# Diagnostic model export (v0.3.6)

Use **File → Export Debug Model...** to capture the current model in a ZIP,
for example `opendesigner-debug.zip`. This is diagnostic evidence, **not** the
v0.3.7 `.odc` Save/Open format. It does not regenerate features, open a document,
change selection, confirm/cancel a command, or add an Undo entry.

## Fedora KDE reproduction

1. Start the newly built application (`build/OpenDesigner`). Reproduce the
   triangular protrusion on the Boolean Unite model.
2. Before changing the model further, choose **File → Export Debug Model...**
   and save a ZIP. All parts are included, including hidden and inactive slots;
   there is no need to show the original Boolean operands.
3. If the defect occurs in a live face preview, export before OK/Cancel as well.
   The snapshot includes canonical document parts, the pre-command solid and a
   valid candidate when available. Export again after OK with a different name.
4. Check that the preview remains active after export. Cancel must still leave
   the document unchanged; OK must still add one Undo entry. Cancel in the file
   chooser simply returns without exporting.
5. Send the ZIP together with the screenshot/recording and the face-edit steps.
   The archive contains model geometry, part names and metadata, not just an image.

The ZIP is written atomically with `QSaveFile` and libarchive. A failed write does
not replace an existing file. Export is synchronous; large models can take time.
It never heals, unifies, trims or remeshes document geometry during capture.

## Contents and coordinate conventions

- `manifest.json`: schema version 1, application/OCCT versions, creation time,
  document Undo/Redo depths, viewport selection and Structure Browser selection.
- `shapes/NNNNN.brep`: OCCT ASCII B-Rep with exact surfaces, curves, topology,
  tolerances, locations, and any existing triangulations/normals. A mesh is never
  substituted for B-Rep geometry. Missing display meshes are not generated.
- Every object records role, stable decimal-string Part ID, name, RGBA color,
  visibility, active status, geometry kind, and a 3×4 instance transformation.
  Part IDs are the current application's zero-based document slots, not persistent
  UUIDs. Boolean-consumed/undone slots are retained with `active: false`.
- Geometry and diagnostic volume/bounds are in the canonical B-Rep frame, including
  any intrinsic OCCT shape location. Apply the separate instance transform exactly
  once for world coordinates. Viewer and probe commands do this automatically.
- Selected faces use **1-based `TopExp::MapShapes(shape, TopAbs_FACE)` indices** in
  the corresponding document-part payload. They are snapshot references, not
  persistent face IDs after editing. Target indices refer to the captured **before**
  shape when recorded in an operation.
- Diagnostics include BRepCheck validity, shell closure, solid count, self-interference
  check status, volume, exact-geometry bounding box, unique face/edge/vertex counts,
  per-face areas/support types, and coplanar dividing-edge counts. Coplanar edges or
  small areas are observations, not instructions to delete necessary faces.
- Camera and AIS presentation status are captured when available. These are useful
  evidence; GPU buffers, driver state, highlighted outlines and a screenshot are
  not serialized.

`lastAppliedOperation` describes the latest **currently applied undoable history
entry**, not a complete chronological command log. Face sessions record distance
or angle, target index, transforms, local target plane, rotation axis or Align
reference/mode as applicable. Move records world and local distances separately.
Boolean entries record operation, operands, target and Keep Tools. A previous face
solid is included as `last-operation-before`. `activeCommand` separately describes
an ongoing session; candidate geometry has role `active-preview`.

Legacy commands without reliable recorded parameters explicitly report unavailable
metadata; values are not guessed from volume or face counts. Creation/rename/color
changes and the full Undo/Redo history are not replayable commands in this format.

## Reload and inspect without opening a document

The build also produces `OpenDesignerDebugModel`, an independent diagnostic tool.
No existing OpenDesigner document is accessed or replaced.

```sh
# JSON analysis on stdout; no display required.
build/OpenDesignerDebugModel opendesigner-debug.zip

# Isolated read-only OCCT viewer; no modeling commands or document history.
build/OpenDesignerDebugModel opendesigner-debug.zip --view

# A NEW directory receives report.json and fresh-N.brep copies.
build/OpenDesignerDebugModel opendesigner-debug.zip --output /tmp/od-inspection

# Optional PNG comparison (stored/fresh/wire), using an X11/OpenGL display.
build/OpenDesignerDebugModel opendesigner-debug.zip --render --output /tmp/od-images

# Classify a known suspect point in WORLD coordinates for document Part ID 2.
# Repeat --probe for more points. Use decimal points in CLI coordinates.
build/OpenDesignerDebugModel opendesigner-debug.zip --probe '2,120.5,30,76'
```

`--render` also exercises the same isolated viewer and writes `stored-N.png`,
`fresh-N.png` and `wire-N.png` using a common camera per object. It requires a
new output directory and an OpenGL display; the JSON-only command does not.

The viewer lets you choose any captured object, including hidden operands and
before/preview evidence. Switch between **Stored AIS mesh**, **Fresh triangulation**
and **Exact B-Rep wireframe**. Left drag rotates, middle drag pans, wheel zooms.
Switching representation of the same object keeps the camera pose. The stored-mesh
view disables automatic triangulation; a face without a stored mesh may be blank.
The wireframe uses mesh-free copies of the actual B-Rep boundaries. The inspector
never changes the ZIP or the application document. Fresh B-Reps are for diagnosis,
not an automatic repair/import into the document.

Reload verifies the payload SHA-256 checksums, schema, topology/validity counts,
volume and bounding box. Export also reloads its serialized payload before writing
and compares solid material using OCCT Common when reliable; the manifest reports
whether that check succeeded. Invalid/inconclusive geometry remains exportable for
investigation, with verification status explicit. Hashes detect accidental payload
changes; they are not cryptographic authentication of the sender.

Readers load into a separate `DebugModel::Snapshot` atomically, never a partial
snapshot or a `CadViewport` document. ZIP paths are not extracted. Unexpected paths,
links, duplicate payloads, missing entries and checksum failures are refused. Limits
are 256 MiB per uncompressed entry, 512 MiB total and 1022 geometry objects.

## Distinguish material, topology and display

1. **Real unwanted material:** choose samples inside the visible spike but away
   from boundaries. `--probe` returns `IN`, `OUT`, `ON` or `UNKNOWN` using the exact
   solid and the part transform, independent of display triangulation. `IN` confirms
   actual material at that point; deciding whether it is intended still requires
   the requested edit and before solid. `ON` needs nearby samples on both sides.
2. **Redundant face/seam:** inspect the exact wireframe, face areas and coplanar
   dividing-edge count. A seam with unchanged material differs from an added
   volume. Do not remove thin or triangular faces solely because of their shape;
   some are necessary boundaries.
3. **Display mesh defect:** compare stored and fresh meshes at the same pose.
   Reports sample planar triangle interiors against exact trimmed faces and list
   suspect face indices. If the spike disappears after remeshing, the exact solid
   remains material-equivalent, and suspect material points classify `OUT`, that
   is evidence of a triangulation defect. If neither representation reproduces it,
   AIS/driver presentation state may be involved; preserve the recording too.

Remeshing operates on deep copies with `BRepTools::Clean` and
`BRepMesh_IncrementalMesh`. Reports include stored/fresh mesh counts, planar trim
sample findings and material equivalence. The planar mesh check is sampled, does
not cover general curved-face triangulations, and is not a proof of intended
geometry. A valid closed solid alone also does not prove a correct face operation.
The supplied Fedora KDE snapshots reproduced actual extra material on Rotate
+30° followed by Move +49 mm. The shared reconstruction now retains missing
physical neighbor trims; see the [exact diagnosis and regression](BREP_RECONSTRUCTION.md).
The original bad payload remains a negative control, with manual retesting pending.

## Build and regression checks

libarchive development headers are required in addition to Qt and OCCT
(`libarchive-devel` on Fedora; `libarchive-dev` on Debian/Ubuntu).

```sh
cmake -S . -B build -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

`debug-model-export` covers the File action and dialog cancellation/acceptance,
all document slots, Unicode names, color/transformation, face selection indices,
exact-material roundtrip, retained meshes, corrupted-mesh versus material evidence,
fresh triangulation, active preview export, confirmed parameters/before solid,
Cancel/OK/Undo/Redo, independent CLI reload/report/probes/rendered comparisons, archive rejection,
failed export and empty snapshots. Existing modeling/preview suites remain enabled.
