import io

def load(p):
    s = io.open(p, encoding='utf-8', newline='').read()
    return s, ('\r\n' if '\r\n' in s else '\n')

def save(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)

def rep(s, nl, a, b, p):
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    return s.replace(a, b)

p = 'docs/research/89_warzone_work.md'
s, nl = load(p)
s = rep(s, nl, '''## 7. Tests
''', '''## 6c. The streamed world of Verdansk and Quarry in the reference view (2026-11-12)

A surface of a big map's `gfx_map` names its transient zone (`transientZone` 0 .. 303 on Verdansk); every zone whose
buffers the main zone does not hold is streamed from `<map>_<NNNNN>_tr.ff` (checked: surface zone 30 is in
`mp_donetsk_00030_tr`, whose `gfx_map_trzone` has `transientZoneIndex` 30; the main zone's empty records 587 .. 867
are something else). `surfaceBounds` (56 bytes per surface, Bounds first) places each tile. ACTS writes a small array
as decoded `values` instead of hex `bytes` (5 of 12 tiles tried); the reader takes both.

- Core `readStreamedWorldZones` / `streamedZonesNear` (stock_world.*); `readStockWorld` also reads tiles exported to
  `<cache>/<zone>/streamed/<tile>`.
- Editor: File > Load Streamed World Around Camera exports the tiles within `stock/streamRadius` (default 12000 units,
  at most `stock/streamLimit` = 48) of the camera, one ACTS run each, keeps them in the world cache and redraws the
  reference.
- Proof: `IW8R_STREAM_AT="0 0 8000 12" IW8Radiant.exe --world-cache-test mp_donetsk` (12 tiles around the centre:
  the reference grows from 9,663 to 285,919 triangles; 258 streamed zones listed), core test
  `stock_world_streamed_zones`.

## 7. Tests
''', p)
save(p, s)

p = 'docs/ROADMAP.md'
s, nl = load(p)
lines = s.split(nl)
for i, line in enumerate(lines):
    if line.startswith('| 13.5 |'):
        a = "Open: Verdansk's world is 749 streamed zones (the reference view loads one zone);"
        assert a in line, line[-400:]
        lines[i] = line.replace(a, "Streamed world (2026-11-12, research 89 section 6c): File > Load Streamed World Around Camera exports and draws the world tiles near the camera (Verdansk: 12 tiles took the reference from 9,663 to 285,919 triangles). Open:")
save(p, nl.join(lines))

p = 'docs/USER_GUIDE.md'
s, nl = load(p)
s = rep(s, nl, '''9. Buy stations: File > Edit Placed Objects''', '''9. Big Warzone maps (Quarry, Verdansk) keep most of their world in streamed tiles: the reference first shows only
   what the map zone holds. Fly to the area you work on and use File > Load Streamed World Around Camera: the tiles
   near the camera are read (a few seconds each, kept for next time) and drawn.
10. Buy stations: File > Edit Placed Objects''', p)
s = s.replace(nl + '10. New brushes: draw brushes', nl + '11. New brushes: draw brushes')
save(p, s)

p = 'docs/PROGRESS.md'
s, nl = load(p)
s = rep(s, nl, '''    REPOSITORY: the project is in the private GitHub repo IDARETW/IW8r''', '''    STREAMED WORLD (research 89 section 6c): core readStreamedWorldZones / streamedZonesNear, WorldCache::
    requestStreamed (tiles kept under <world_cache>/<zone>/streamed), File > Load Streamed World Around Camera.
    Proof: IW8R_STREAM_AT="x y radius [limit]" with --world-cache-test.
    REPOSITORY: the project is in the private GitHub repo IDARETW/IW8r''', p)
save(p, s)
print('ok')
