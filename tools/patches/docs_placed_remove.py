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


rep('docs/features/FEATURES.md', [
 ('Move one, or copy one to add another. A station cannot be removed. Doors',
  'Move one, copy one to add another, or delete one to remove it (the rebuild renumbers the dynamic entities after it; checked on Quarry). Doors'),
])
rep('docs/USER_GUIDE.md', [
 ('''   Console counts what was moved and added. A station cannot be removed (move it out of the way), and doors and other
   objects with moving parts cannot be edited this way yet.''', '''   Console counts what was moved, added and removed. Delete a station's entity to remove it from the map. Doors and other
   objects with moving parts cannot be edited this way yet.'''),
])
rep('docs/research/89_warzone_work.md', [
 ('''3. (New clusters and zones on a stock map: done offline, section 6d.) Removing a placed object, and moving or adding one that has a dynamic entity (doors, extraction sites).''',
  '''3. (New clusters and zones on a stock map: done offline, section 6d.) (Removing a placed object without a dynamic entity:
   done offline 2026-11-12, x64-zt aacfad65: editzone `scriptable_instances.remove` drops the records and renumbers the
   dynamic entities that name later instances; the 64 reserved physics entries name 500000, no instance, and stay; the
   spatial tree holds split planes only. `tools/warzone_instances_remove_test.py` on Quarry: two of 120 buy stations
   removed, counts and the six physics limits -2, all 864 renumbered dynamic entities still name the same instance, ACTS
   as stock, an instance with a dynamic entity refused.) Moving or adding one that has a dynamic entity (doors,
   extraction sites) is open.'''),
])
p = 'docs/ROADMAP.md'
s, nl = load(p)
s = s.replace('Open: removing a placed object, and editing one that has a dynamic entity (doors).',
              'Removing a placed object (2026-11-12, x64-zt aacfad65): delete its entity; the rebuild drops it and renumbers the dynamic entities after it (tools/warzone_instances_remove_test.py). Open: editing one that has a dynamic entity (doors).', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
rep('CHANGELOG.md', [
 ('''- **Buy stations.**''', '''- **Remove buy stations** from a stock Warzone map: delete the entity after Edit Placed Objects; the rebuild drops
  the instance and renumbers the dynamic entities after it (converter aacfad65). Not seen in game.
- **Buy stations.**'''),
])
print('ok')
