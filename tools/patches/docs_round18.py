import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| F.16 |')]
assert len(hit) == 1
cells = lines[hit[0]].split(' | ')
assert len(cells) == 4
add = (' Owner playtest 3 (2026-11-11 night, the first run of the new client exe; the map loaded and the grass showed on the cubes): '
       'the re-skinned cubes stopped bullets but not the player, and their texture was upside down in the editor camera while right in game. '
       'Both fixed offline: the collision pass now finds the mesh of a re-skinned copy through its base model (the copy has its own name), '
       'and the editor camera flips the texture coordinates of imported models to the convention it draws stock models with. '
       'Test: core compile_iw8_custom_model_material now also checks that re-skinned placements get the same collision brushes as plain ones. NOT SEEN in game.')
assert '|' not in add
lines[hit[0]] = ' | '.join(cells[:3]) + ' | ' + cells[3].rstrip(' |') + add + ' |'
save(p, nl.join(lines))

p = 'docs/TEST_CHECKLIST.md'
s, nl = load(p)
a = '## In game: materials on your own models (2026-11-11 evening), editor steps first, not seen in game'
assert s.count(a) == 1
sec = [
 '## In game: fixes after the third playtest (2026-11-11 night), not seen in game',
 '',
 '- [ ] Restart the editor first. In the camera view the grass cubes are the right way up.',
 '- [ ] Build and play: you can stand on the grass cubes and walk up the staircase (they stopped bullets only).',
 '- [ ] The door: still the editor-made one in this build. A real stock door as a prefab is being worked on.',
 '',
]
s = s.replace(a, nl.join(sec) + nl + a)
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
a = '  - Still open: impacts (F.12), the script-played loop of an own sound (F.6), reverb, probes, game modes, killstreak'
assert s.count(a) == 1
add = [
 '  - OWNER PLAYTEST 3 (2026-11-11 night): F5 worked after the probe fix (the new client exe ran and the map loaded;',
 '    the owner did not report on the cold start screens, the overlay or game modes). Reports: (1) re-skinned shape',
 '    cubes had no player collision: fixed, compile_iw8.cpp boundsOf resolves a variant to its base model; (2) their',
 '    texture is upside down in the editor camera only: fixed, main_window.cpp flips v of imported models for the',
 '    viewport; (3) the door must be a real stock door delivered as a prefab, and prefabs must be bundles (model +',
 '    script behaviour + data, drag and drop, real geometry in the viewport): research agent writing',
 '    docs/research/83_stock_door_and_prefab_bundles.md. Do NOT run the GUI suite while the owner\'s game runs: some',
 '    checks read the running game (one fails: "without a game the Live Link commands say why").',
]
s = s.replace(a, nl.join(add) + nl + a)
save(p, s)
print('ok')
