# Native OpenDesigner documents

OpenDesigner stores native documents as a single `.odc` file. The file is
an OCCT XCAF document written by the BinXCAF driver. Parts keep their exact
local B-Rep; no display tessellation is used as a source for Save/Open.

Schema version 1 contains one root assembly with Part components. The assembly
representation is deliberately label based so nested assemblies can be added
without changing the file container. Each component stores a stable Part ID,
name, color, visibility, local shape and placement. Camera eye, center, up,
scale and projection are stored in an OpenDesigner metadata label.

Save writes BinXCAF into a temporary staging directory beside the destination,
then commits the completed bytes using `QSaveFile` with direct-write fallback
disabled. Existing files are replaced only after successful completion; staging
files are removed on failure. Open stages its BinXCAF input in the system temp
directory, allowing documents in read-only folders. A failed Save or
Open leaves the current document unchanged. Save and Save As retain selection
and Undo/Redo. New and successful Open clear command sessions, selection,
previews, manipulators and Undo/Redo.

Camera navigation is view state and does not mark a document dirty. The latest
camera state is captured when Save runs. Undo/Redo history and face selections
are session data and are not serialized.

The format stores the current direct-modeling result rather than a parametric
feature history. Loaded geometry remains usable by Move Face, Rotate Face,
Delete Face, Boolean operations and the existing modeling Undo/Redo session.

Pull / Resize determines capabilities from the current exact B-Rep when the
command is requested. Stored primitive flags are editing caches, not prerequisites.
Complete boxes, spheres and supported local-Z cylinders/cones are recognized
from their surfaces and certified by validity and Boolean Common material
equivalence. Names, creation methods, import and modeling history do not enter
that test. General cylindrical walls, including holes, use the validated
face-offset path. A cone patch in a cut solid is not replaced by a whole cone;
unsupported reconstruction is refused. Recognition retains the exact source
B-Rep and Part placement and introduces no feature history.

`native-document-pull` covers Save/Open with absent primitive flags, shape
recognition, all four cone handles, cylindrical-hole Pull, cancellation and
refusal to rebuild a cut cone as a primitive. It checks the rendered orange
handles as well as hit testing. Pull activation explicitly redraws the arrows;
hole manipulators use a camera-based radial direction and the topmost layer so
surrounding material cannot hide them.

Save captures confirmed model geometry and the current camera. An active Pull
preview remains active, but its unconfirmed working geometry is excluded from
the saved snapshot. Save As applies the `.odc` suffix in the file dialog before
its overwrite confirmation. Save also remains available for an empty document.

Automated verification includes `native-document-roundtrip` (multiple Parts,
local B-Rep volume/area/centroid/topology, IDs, Unicode names, both colors,
visibility, rigid rotation/translation, full camera state, empty files, read-only
sources, malformed input and failed replacement/cleanup) and
`native-document-sessions` (real File dialogs, Save/Save As history and selection,
preview snapshots, camera navigation, unsaved prompts, failure preservation,
New/Open cleanup, hidden presentations and Structure Browser rebuilding).
The user tested and approved the functionality on Fedora KDE on 2026-10-21,
including Pull on the supplied cone and cylindrical hole.

Schema v1 supports one root assembly with Part instances. Nested assembly
editing/roundtrips are not implemented yet. Undo/Redo, face selection and
parametric feature history are intentionally not stored. Storage and geometric
recognition are synchronous. General curved/freeform reconstruction retains the
limits of the existing modeling solvers; recognition does not make an
unsupported edit safe. Windows/macOS runtime validation and portable application
packaging remain outstanding.

## v0.4.0 construction extension

Current writes use schema v2; the reader also accepts v0.3.7 schema v1 and
initializes an empty construction collection. Schema v2 retains exact Part
geometry/properties and camera and adds independent Workplane/Axis/Point
metadata in the same BinXCAF file. Its root marker and explicit version prevent
legacy readers from silently discarding these records. Unknown schemas and
malformed construction frames, flags and duplicate document IDs are refused.
Save captures confirmed construction values even while an editing preview is
active. Save/Save As retain construction selection and history; New/Open reset
and rebuild both Parts and construction objects. See
[Construction geometry](CONSTRUCTION_GEOMETRY.md) for the schema and tests.

Construction presentation metadata additionally stores a local display offset
separately from the analytic origin, plus per-type WP/AX/PT name counters on the
construction group. Earlier v2 files without these optional attributes retain
zero display offsets and infer their counters from existing names. All data stays
in the single `.odc` file. Viewport text labels are rebuilt from object names and
camera state; no AIS objects or screen positions are serialized.
