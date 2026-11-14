import io

# The owner's game runs of 2026-11-14 evening: Cinder Rail Toon without the occlusion bake is fine, and the map built
# again with the (fixed) occlusion bake works too. The build log and the tooltip stop saying "not yet seen in game".

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/compile_iw8.cpp', '''        // The bake failed its first game test (2026-11-14, Cinder Rail Toon: surfaces came and went with the camera,
        // placed models were not drawn). Cause found and fixed the same day (the tile's format flag: research 129); the
        // fixed bake has not been seen in the game yet, which the build log says until it has.
        warn.push_back("note on the occlusion bake: it failed its first game test on 2026-11-14 (surfaces and models went missing); the cause "
                       "was found and fixed that day (research 129), but the fixed bake is not yet seen in game. If things go missing "
                       "in game, turn Occlusion bake off (worldspawn iw8r_umbra_bake)");
''', '''        // The bake failed its first game test (2026-11-14, Cinder Rail Toon: surfaces came and went with the camera,
        // placed models were not drawn). Cause found and fixed the same day (the tile's format flag: research 129), and
        // the owner then built the same map with the fixed bake and found it working in the game.
''')
patch('src/qtapp/compile_panel.cpp', '''"by occlusion, which always works. It failed its first game test on 2026-11-14 (surfaces and models went missing); the "
                               "cause was fixed that day, but the fixed bake is not yet seen in game. If things go missing, turn it off");''',
      '''"by occlusion, which always works. Seen working in game on 2026-11-14 (after a fault of the first version was fixed "
                               "that day). If things ever go missing in game, turn it off");''')

s = io.open(ROOT + 'docs/HANDOVER.md', encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:70])
    s = s.replace(a, b)


rep('''walls. Off by default, and leave it off for a map you want to play: its first game test (2026-11-14) lost surfaces
and models. The cause was found and fixed the same day (research 129) and the fix is proven on that very map
offline, but it has not been seen in game since. View > Umbra Visibility shows what a stock map's own occlusion data
hides.''', '''walls. Off by default. Its first game test (2026-11-14) lost surfaces and models; the cause was found and fixed the
same day (research 129), and Cinder Rail Toon built again with the fixed bake worked in game that evening. View >
Umbra Visibility shows what a stock map's own occlusion data hides.''')
rep('''- Cinder Rail Toon lost surfaces and models: that build had the occlusion bake on. The map in the game folder is a
  build without it (since 16:25 that day); not looked at since.
''', '''- Cinder Rail Toon lost surfaces and models: that build had the first occlusion bake. Settled the same evening: the
  build without the bake is fine in game, and so is a build with the fixed bake.
- The minimap was a quarter turn off against the player on built maps (seen on Cinder Rail Toon). Cause: the picture
  was drawn for a map whose north is +X while the build writes north = +Y. Fixed (the picture is drawn with the
  map's north up) and proven offline on two built maps; rebuild a map to get it. Not seen in game since.
''')
rep('''7. **The occlusion bake after its fix** (only if you want it): nothing is missing anywhere on a map built with it.
''', '''7. **The minimap** on a map built on or after the evening of 2026-11-14: the player arrow sits where the player is.
''')
io.open(ROOT + 'docs/HANDOVER.md', 'w', encoding='utf-8', newline='').write(s)

patch('docs/TEST_CHECKLIST.md', '''- [ ] Occlusion bake after the fix of 2026-11-14 (research 129): a map built with Occlusion bake on keeps all its
      surfaces and models from every camera position.''', '''- [x] (owner, 2026-11-14 evening) Occlusion bake after the fix of 2026-11-14 (research 129): Cinder Rail Toon built
      with Occlusion bake on "seems to give a working map".
- [ ] Minimap after the fix of 2026-11-14 evening (rebuild the map): the player arrow sits where the player stands
      and the picture turns with the player as on a stock map.''')
patch('docs/TEST_CHECKLIST.md', '''- [ ] Cinder Rail Toon without the occlusion bake (redeployed 2026-11-14 16:25): no surface or model goes missing.''',
      '''- [x] (owner, 2026-11-14 evening: "seemed to be fine") Cinder Rail Toon without the occlusion bake (redeployed
      2026-11-14 16:25): no surface or model goes missing.''')
print('ok')
