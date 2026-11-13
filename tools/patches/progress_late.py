import io
p = 'docs/PROGRESS.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = "- Checked: Speedball (against the owner's shot)"
b = r'''- Late evening: stock decal volumes (research 99: paint stripes, stains; projected on the world, ACTS
  decalvolumematerial / decalvolumemask, image exporter), King's bakedLightScale (1/64), the sky exporters look in
  the open map's zones (Speed), sky brightness from the world's own sky material, probe fallback for far terrain,
  material cache ACTS runs in a short temp folder (260-character paths), a dangling reference in the stock batch
  builder fixed. Checked every map with a zone in this install (Aniyah Tac, Backlot, Emporium, Fork, Hideout, King,
  Rust, Shipment, Speed, Speedball, Village) from a path-node camera: IW8R_BENCH_CAM="ent node_pathnode N [yaw] [fov]".
  Bench caches live in a short temp path on purpose (AppData\Local\Temp\iwb). Installer rebuilt (no ini).
- Checked: Speedball (against the owner's shot)'''
assert s.count(a) == 1
s = s.replace(a, b.replace('\n', nl), 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
