# Delete Part (v0.4.7)

Select whole Parts and press Delete while the viewport or Structure Browser has
keyboard focus, or use Delete Part in their context menus. Multiple selected
Parts are removed together as one Undo operation. Hidden Parts can be selected
and deleted through the Structure Browser. Assembly deletion is not supported.

Face, edge, vertex and sketch selections do not qualify as whole-Part viewport
selection. Delete remains available for text editing, and Part deletion is blocked
while modeling commands, sketch editing or interactive manipulation are active.

Deleting the Active Part clears activation and its outline. Deleting other Parts
preserves activation. Undo restores the deleted Parts, but does not reactivate
one: this follows the existing policy that activation is outside geometry history.
Redo removes the same Parts. Save/Open stores the remaining live Parts using the
existing native format; no format change is required.

## Implementation

The existing Boolean history transaction supports source-only removal, retaining
full Part snapshots and stable document slots rather than compacting the document.
Undo restores UUID, B-Rep, material, placement, color, visibility and browser order.
The normal AIS removal and history selection cleanup release selected/detected
owners; existing Active Part refresh removes invalid activation. Browser deletion
passes explicit Part rows, including hidden Parts. Viewport deletion additionally
requires AIS global selection owners, rejecting local subshape owners even if a
whole-Part selection index remains cached.

## Manual Fedora KDE acceptance (pending)

1. Select one Part, press Delete, then Undo and Redo; verify the correct Part.
2. Select multiple Parts with distinct placements, colors and materials; delete
   from each context menu and confirm one Undo restores properties and order.
3. Delete a hidden Part from the Structure Browser; Undo must keep it hidden.
4. Delete the Active Part and verify its indicator/outline disappear; delete a
   different Part and verify activation remains. Undo must not reactivate a Part.
5. Select faces, edges, vertices and sketch geometry; Delete must not remove their
   owning Part. Check text editing and active command/sketch tools retain Delete.
6. After deletion, orbit and pick remaining geometry; verify no stale highlights.
7. Save, close and reopen; confirm only surviving Parts remain with correct UUIDs
   and properties. Also check an empty document after deleting the final Part.
