import io


def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')


def rep(p, pairs):
    s, nl = load(p)
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


rep('docs/research/89_warzone_work.md', [
 ('''   follow the walk, 523 of 524. Next steps: (1) moving an ordinary entry''', '''   follow the walk, 523 of 524, and the 46 bucket chains of DynEntityList.dynEntSpatialPopulation (partitionIds[b] =
   the partition of bucket b) list exactly the ordinary entries of each partition in descending entry order, the tail
   the last (all 46 on Quarry). Built (x64-zt, "move" of an instance with ordinary dynamic entities): new pose, the
   walked partition, unlink / relink in descending order with the tails; refused for sparse occluders, transient
   entries, an entry placed apart from its instance (the 4 buy-station physics entries of Quarry) and a partition
   without a bucket. Proof tools/dynent_move_test.py on Quarry (a short and a far move: pose, partition, all chains
   exact, everything else unchanged, ACTS as stock, a door refused). Earlier plan: (1) moving an ordinary entry'''),
])
s, nl = load('docs/features/FEATURES.md')
s = s.replace("Move one, copy one to add another, or delete one to remove it (the rebuild renumbers the dynamic entities after it; checked on Quarry). Doors",
              "Move one, copy one to add another, or delete one to remove it (the rebuild renumbers the dynamic entities after it; checked on Quarry). Signs, fences and other placed objects with an ordinary physics entry can be moved too (the entry follows). Doors", 1)
io.open('docs/features/FEATURES.md', 'w', encoding='utf-8', newline='').write(s)
rep('docs/USER_GUIDE.md', [
 ('''   Console counts what was moved, added and removed. Delete a station's entity to remove it from the map. Doors and other
   objects with moving parts cannot be edited this way yet.''', '''   Console counts what was moved, added and removed. Delete a station's entity to remove it from the map. Other placed
   objects (signs, fences, props with physics) become entities too and can be moved; doors cannot be edited yet.'''),
])
rep('CHANGELOG.md', [
 ('''- **Remove buy stations**''', '''- **Move other placed objects** of a stock map (signs, fences, props with physics): their physics entry follows the
  move (pose, spatial partition and chain; checked on Quarry, tools/dynent_move_test.py). Doors stay. Not seen in game.
- **Remove buy stations**'''),
])
p = 'docs/ROADMAP.md'
s, nl = load(p)
s = s.replace('Open: editing one that has a dynamic entity (doors).',
              'Moving placed objects with an ordinary dynamic entity (signs, fences; tools/dynent_move_test.py). Open: doors (sparse occluders use another spatial index).', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
s = io.open('tools/run_offline_proofs.py', encoding='utf-8').read()
s = s.replace("    ('warzone_instances_remove_test', ['--exe', exe]),", "    ('warzone_instances_remove_test', ['--exe', exe]),\n    ('dynent_move_test', ['--exe', exe]),", 1)
io.open('tools/run_offline_proofs.py', 'w', encoding='utf-8').write(s)
print('ok')
