import io

# TEST_CHECKLIST: "the short list" as the first In game section (plan M5: one list holding only what needs the game).

p = 'E:/IW8-Radiant/docs/TEST_CHECKLIST.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
assert 'In game: the short list' not in s
first = s.index('## In game')
block = '''## In game: the short list (2026-11-14), what only the game can settle, most important first

Everything else in this file was proven without the game. If you look at nothing else, look at these, in this order.
Each one names the longer section below that has the steps.

- [ ] SHORT 1. An installed stock edit loads: install the Shipment toon zone (folder `out\\agent\\toon_ship\\edit`,
      File > Install Edited Stock Map Into Game) and start Shipment. Every stock-map and Warzone edit goes into the
      game this way, and it has never been run. Section "Shipment with the toon shader's filter".
- [ ] SHORT 2. Your own map looks like View > Game Lighting, once with Compile > Bake lightmaps off and once on.
      Section "an own map with and without baked lightmaps".
- [ ] SHORT 3. A stock door carried into your own map opens. Section "real stock doors and objects".
- [ ] SHORT 4. A stock map rebuilt with removed world geometry: the object is gone and you can walk through where it
      was. Section "an edited stock map".
- [ ] SHORT 5. Copied loot, doors and extraction sites on Quarry work like the originals. Section "game modes and a
      small Warzone map".
- [ ] SHORT 6. Only if you want it: a map built with Compile > Occlusion bake shows nothing missing. Section "the
      occlusion bake".

'''.replace('\n', nl)
s = s[:first] + block + s[first:]
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
