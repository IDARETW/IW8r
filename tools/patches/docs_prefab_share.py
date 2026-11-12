import io


def rep(p, pairs):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (p, s.count(a), a[:80])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


rep('docs/PREFAB_BUNDLES.md', [
 ('''2. **Placement hints**: `placement.surface` (`wall`, `ceiling`) and `placement.yaw_step` are read but not used; a drop
   always lands on the surface point with the prefab upright.
3. **No editor for parameters, script and fallback**: they are written into `prefab.json` and the bundle folder by
   hand. **Convert To Bundle** (wrap a plain `.map`) and importing a `<name>.iw8prefab.zip` do not exist yet.''',
  '''2. (Done 2026-11-12) **Placement hints**: Insert Prefab uses `placement.surface`: a `wall` prefab placed on a wall
   turns its +X out of the wall (yaw from the face normal, rounded to `placement.yaw_step`), `snap` snaps x and y (and
   the height on a wall); the status bar says when the surface does not fit. File > Rotate Selected Prefab turns a
   placement by its `yaw_step` (90 for a plain prefab). Core `prefabPlacementFor`.
3. **No editor for parameters, script and fallback**: they are written into `prefab.json` and the bundle folder by
   hand. (Done 2026-11-12: File > Convert Plain .map Prefab To Bundle wraps a plain `.map` with the pivot at its own
   origin, as Save Selection As Prefab does with the mapper's own assets; File > Export Prefab Bundle As Zip and Import
   Prefab Bundles From Zip share bundles: the import reads stored and deflated zips (Windows "Compressed folder"),
   takes "<name>.iw8prefab/" folders or one bundle's files at the top, never replaces an existing bundle and checks
   each bundle before it lands in the prefab folder. Core `prefabBundleFromPlainMap`, `exportPrefabBundleZip`,
   `importPrefabBundleZip`; `extractZip` inflates with stb.)'''),
])
rep('docs/features/FEATURES.md', [
 ('''- Prefabs: convert a plain `.map` to a bundle, zip import of bundles, and the placement hints (wall, ceiling and yaw step).
''', ''''''),
 ('''- Save Selection As Plain .map Prefab writes the old single-file form.''', '''- Sharing: File > Export Prefab Bundle As Zip, and Import Prefab Bundles From Zip (also zips made by Windows or other tools; an existing bundle is never replaced). File > Convert Plain .map Prefab To Bundle wraps an old single-file prefab, keeping its origin.
- Placement hints: a bundle made for walls (`placement.surface` `wall`) turns to face out of the wall it is placed on; File > Rotate Selected Prefab turns a placement by the bundle's yaw step.
- Save Selection As Plain .map Prefab writes the old single-file form.'''),
])
rep('CHANGELOG.md', [
 ('''- **Blend By Slope**''', '''- **Prefab sharing**: export and import prefab bundles as zips (deflated zips too), convert a plain `.map` prefab to a
  bundle, wall placement and Rotate Selected Prefab by the bundle's yaw step.
- **Blend By Slope**'''),
])
print('ok')
