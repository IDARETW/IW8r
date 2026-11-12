import io


def patch(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:70])
        s = s.replace(a, b, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('docs/ROADMAP.md', [
 ('''Doors move inside their area (the box of the stock doors that share their spatial index). Open: copying or removing objects with a dynamic entity. | L |''',
  '''Doors move inside their area (the box of the stock doors that share their spatial index). Copying and removing objects with dynamic entities (2026-11-12, x64-zt 45112b43 / 8ed35d9a): signs, fences, props, doors, extraction sites and physics entries; entries renumbered, every spatial chain and the GfxWorld client arrays rebuilt; doors follow an exact walk of the second MapEnts tree; copies keep their targetname and script_noteworthy (tools/dynent_copy_remove_test.py on Quarry and Shipment, ACTS as stock). Refused with the reason: streamed (transient) entries, removals on maps whose transient zones name later instances (Verdansk). Not seen in game. | L |'''),
])
patch('CHANGELOG.md', [
 ('''Lighting:
''', '''Warzone and stock maps (later the same day):

- **Copy and delete placed objects with physics.** File > Edit Placed Objects now lets you copy and delete doors,
  signs, fences, props and extraction sites too, not only buy stations; Rebuild Stock Zone writes them with their
  dynamic entities (renumbered, spatial chains and counts rebuilt). Copies keep the group names scripts look for.
  Proven offline on Quarry and Shipment (tools/dynent_copy_remove_test.py). Verdansk removals are refused (its
  streamed zones point at later objects). Not seen in game.

Lighting:
'''),
])
patch('docs/PROGRESS.md', [
 ('''  stock probes / specular / retro-reflective diffuse are not drawn.''',
  '''  stock probes / specular / retro-reflective diffuse are not drawn.
- Then copy / remove of placed objects with dynamic entities (x64-zt 45112b43 + 8ed35d9a, live; old exe
  out/zt_backup/zonetool_2594f62e.exe): core stock_placed allows them, tools/dynent_copy_remove_test.py (in
  run_offline_proofs.py); warzone_instances_remove_test / _edit_test updated for the new behaviour. Doors follow an
  exact walk of MapEnts.dynEntSpatialPopulation[1] (319 of 319 on Quarry), so the door-move area box could be replaced.
  Note: every editzone output loads one asset fewer than stock in ACTS (73054 vs 73055), already before this change.'''),
])
print('ok')
