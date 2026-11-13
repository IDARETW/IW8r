import io


def edit(p, a, b):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    if b in s:
        return
    assert s.count(a) == 1, (p, a[:60])
    io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


edit('CHANGELOG.md', '''## 2026-11-13

''', '''## 2026-11-13

- **Verdansk needs far less memory to open.** Reading the map's 422 MB export used to take about 1.6 GB per step (world,
  props, displacement, clutter, streamed tile list); each now takes 5 to 280 MB, with exactly the same result. The
  reflection probes' 160 MB of images stay on disk and only the ones around the camera are read. Two Verdansk tiles
  whose export had come out empty are exported again.
''')
edit('docs/PROGRESS.md', '''## Latest (2026-11-13)

''', '''## Latest (2026-11-13)

- Memory (2026-11-13, helper): json_lite file views (memory-mapped, validated with parse()'s rules and messages, members
  parsed in place); readStockWorld / readStreamedWorldZones / readStockModelInstances / displacement / clutter read
  members only. mp_donetsk peaks: world 1,683 -> 33 MB (with every tile 1,889 -> 641), streamed zones 1,683 -> 7, smodels
  1,614 -> 281, displacement 1,712 -> 113, clutter 427 -> 23; fingerprints identical; tests json_lite_view_tests,
  stock_reader_views_tests (1,261 tests). Mapped files are write-locked while read. Two Verdansk tiles had 0-byte
  trzone exports: the export now counts a 0-byte file as failed and updateStreamedTiles removes such tiles (exported
  again). Warzone probes keep records + StockProbeImageIndex (indexStockProbeCacheImages over the main and tile files);
  buildStockProbeSubset(records, index, camera) reads only the chosen images. Verdansk crash before this: 0-byte dump
  (out of memory while writing it).
''')
print('ok')
