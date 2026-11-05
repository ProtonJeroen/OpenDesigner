# Phase 9E: face editing history and native persistence

The `face-history-persistence` regression exercises the viewport transaction and
native BinXCAF storage boundary together. Run it with:

```
cmake --build build --target OpenDesignerMoveFaceTests OpenDesignerNativeDocumentTests -j2
ctest --test-dir build -R '^(face-history-persistence|native-document-roundtrip)$' --output-on-failure
```

The GUI regression requires a working desktop/OpenGL display. Its default fixtures
are generated B-Reps, including an oblique planar rim and a Part with rotated
placement. Private native documents can be replayed without modifying them:

```
build/OpenDesignerMoveFaceTests --history-persistence /path/to/documents
```

That directory must contain `moveCylinder.odc`, `moveCylinderBug.odc`,
`crash03112026.odc`, and `hole2.odc`. All output documents live in a
`QTemporaryDir`; the regression checks source bytes remain unchanged. A single
`.odc` file path is also accepted to rerun only an unresolved document case.

The inspected implementation already uses `FaceChange` before/after B-Reps for
these face commands. Confirm appends one transaction and clears Redo; Undo/Redo
restores the cached states. Move Face and Delete Face previews keep canonical
geometry unchanged. Pull previews temporarily update the live shape, so snapshot
capture explicitly substitutes the transaction's confirmed `before` state.
BinXCAF stores the B-Rep and per-Part metadata; reopening builds new presentations
and starts a new history session. No production geometry or persistence algorithm
was changed for this regression.

The native replay derives feature radii from the current faces. It uses small
boss moves on the supplied documents, whose current dimensions and supports can
differ from earlier acceptance fixtures. Explicit boss boundary crossings with
both policies are covered by the generated fixture.

Coverage:

- Complete hole to boundary notch, repeated Move Face, cached Undo/Redo.
- Boss boundary crossing with both Trim and Extend, repeated movement, height Pull.
- Diameter Pull, invalid radius after a valid preview, Cancel, and boundary Delete Face.
- Save during Move Face, Pull and Delete Face previews serializes confirmed material.
- Save retains live history and selection; Open clears history, selection and tools.
- Supported reopened faces accept Move Face, diameter Pull and Delete Face with fresh owners.
- Part ID, name, placement, color, visibility and Active Part survive native storage;
  edits and history retain the presentation and Structure Browser row.
- Hidden witness Part remains hidden; other Parts retain geometry and metadata.
- Reopened solids match confirmed topology counts, validity, volume, Boolean common
  material and sampled occupancy.

The test uses the same native storage and viewport import/export entry points as
Save/Open. File chooser and window dirty-state behavior remain covered by the
existing `native-document-sessions` test. History is intentionally session-local;
`.odc` stores the current B-Rep and identity, not edit history. The regression does
not extend geometry support or certify previously unsupported interacting features.

Validation on 2026-11-05:

- `face-history-persistence`: passed (37.70 seconds), including both generated cases.
- Native document cases: `moveCylinder.odc`, `moveCylinderBug.odc`,
  `crash03112026.odc`, and `hole2.odc` passed; each source remained byte-identical.
  The first three passes were retained when only the unresolved rotated-rim
  deletion case was adjusted and replayed individually.
- The existing `native-document-roundtrip` test passed before the power failure;
  its unchanged result was reused. No full CTest or unchanged geometry suite ran.

No production history, identity or serialization gap was found. The gap closed by
this change is combined regression coverage across editing and native storage.
At the Phase 9E baseline, Delete Face on the edited `hole2.odc` shape rejected the
sloped cylindrical boundary both before and after serialization. That test verified
that refusal preserved geometry/history and Save captured confirmed material.
Phase 9E did not repair that geometry limitation. [Phase 9F.1](SLOPED_OPENING_DELETE_FACE.md)
now reuses the actual planar-rim fill for deletion; the history regression requires
this case to succeed before and after serialization instead of allowing refusal.
