import io, shutil

src = 'out/streamed_exports/iw8_streamed_images.all_mp.tsv'
dst = 'data/asset_catalog/iw8_streamed_images.tsv'
rows = [l for l in io.open(src, encoding='utf-8').read().splitlines() if l and not l.startswith('#')]
assert rows[0].split('\t') == ['image', 'full_width', 'full_height', 'resident_width', 'resident_height', 'streamed']
data = rows[1:]
assert len(data) == 19396 and all(len(r.split('\t')) == 6 for r in data)
streamed = sum(r.split('\t')[5] == '1' for r in data)
assert streamed == 18619, streamed
assert len({r.split('\t')[0] for r in data}) == len(data)  # one row per image
old = [l for l in io.open(dst, encoding='utf-8').read().splitlines() if l and not l.startswith('#')][1:]
new = {r.split('\t')[0]: r for r in data}
diff = [o.split('\t')[0] for o in old if new.get(o.split('\t')[0]) != o]
assert diff == ['*ieslookup'], diff  # the only old row that changes (a per-map lookup image)
shutil.copyfile(src, dst)

p = 'tests/streamed_images_tests.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
a = '    CHECK(cat.size() == 2671);'
assert s.count(a) == 1
s = s.replace(a, '    CHECK(cat.size() == 19396);  // 21 stock multiplayer zones (2026-11-11); the first catalogue had mp_backlot2 only (2671)')
io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'docs/ROADMAP.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
lines = s.split(nl)
hit = [i for i, l in enumerate(lines) if l.startswith('| 4.25 |')]
assert len(hit) == 1
old_tail = 'The catalog covers mp_backlot2 only: all zones open'
assert lines[hit[0]].count(old_tail) == 1
new_tail = ('The catalog now covers 21 stock multiplayer zones (2026-11-11: 19,396 images, 18,619 streamed; exports and the merge script in out/streamed_exports; '
            'four menu zones could not be exported by ACTS and are left out: mp_frontend2, mp_frontend6, mp_frontend7b, mp_frontendc3s5). Single player and co-op zones are not covered')
assert '|' not in new_tail
lines[hit[0]] = lines[hit[0]].replace(old_tail, new_tail)
io.open(p, 'w', encoding='utf-8', newline='').write(nl.join(lines))
print('ok', len(data), streamed)
