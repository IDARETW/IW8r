import io

# Docs: the occlusion bake's fault found and fixed (research 129), the viewmodel filter built to a zone that waits for
# the owner's go-ahead (research 128). 2026-11-14 17:00.

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **The occlusion bake's fault is found and fixed** (research 129). The baked data carried the format flag of the
  one-cell data (3) where every stock map has 5. That flag tells the game how to read the camera's cell; with the
  wrong one it started from an unrelated cell almost everywhere, so surfaces came and went and models were never
  reached. The bake now writes 5 (converter `3b2bcc93`, deployed), and the editor's own reader follows the game's
  rule, so its tests fail on data with the wrong flag as the game did. The fixed bake is not yet seen in game; the
  build log says so.
- **Toon look for the first-person weapon and hands: built, not installed** (research 128). Their shaders are in a
  zone every map loads (`techsets_global_stream_mp`). The converter's filter can now leave every name as it is and
  filter only the sets of matching materials (`"keep_names"`, `"materials"`): 631 technique sets of 6,884 weapon and
  arm materials got the toon filter with ink lines, and the game's loaders accept the zone (2,632 assets tested, 0
  failed). Installing it changes weapons and hands on every map until it is restored, so it waits for the owner's
  go-ahead (`--install-stock`, undone with `--restore-stock global_stream_mp`).

''')
s = read('docs/PLAN_REMAINING.md')
nl = '\r\n' if '\r\n' in s else '\n'
a = '''  - occlusion bake in game (2.4): FAILED its first game test 2026-11-14 16:15'''
assert s.count(a) == 1
s = s.replace(a, ('''  - occlusion bake in game (2.4), update 2026-11-14 17:00: CAUSE FOUND AND FIXED (research 129): tile flags 3 instead
    of the stock 5 (bit 2: a view leaf's value is an offset into the cell list; without it the game takes it for the
    cell number). Bake writes 5, the decoder follows the game's rule, converter 3b2bcc93 live, proof
    tools/umbra_bake_build_test.py passes. NOT SEEN IN GAME since the fix. Still different from stock and worth a
    look if something is still wrong: cell flags without bit 0 and size bits, no clusters, portal growth 0.
''').replace('\n', nl) + a)
write('docs/PLAN_REMAINING.md', s)
patch('docs/research/README.md', '''| 127 | [Volumetric boxes and fog on see-through surfaces]''',
      '''| 129 | [Why the baked occlusion failed in game](129_umbra_bake_game_failure.md) | The tile flag that tells the game how to read a view leaf (bit 2: an offset into the cell list, else the cell number), read in the exe; the bake wrote 3, stock is 5; replay of the game's rule over the failing tome; the fix and the check that catches it |
| 128 | [A toon filter for the viewmodel](128_viewmodel_filter.md) | Where the first-person weapon and hands keep their materials, sets and shaders (techsets_global_stream_mp), which names other zones use, what the filter must leave alone, a scratch build, the risks of installing a shared zone |
| 127 | [Volumetric boxes and fog on see-through surfaces]''')
patch('docs/TEST_CHECKLIST.md', '''- [ ] Toon look on the first-person weapon and hands: not built yet.''',
      '''- [ ] Toon look on the first-person weapon and hands: the zone is built (out/agent/toon_vm/out); after it is installed,
      weapons and hands show the toon colours and ink lines on every map, and nothing else changes.
- [ ] Occlusion bake after the fix of 2026-11-14 (research 129): a map built with Occlusion bake on keeps all its
      surfaces and models from every camera position.''')

p = 'docs/PROGRESS.md'
s = read(p)
nl = '\r\n' if '\r\n' in s else '\n'
a = '  OWNER GAME TEST 2026-11-14 16:15 (their words):'
assert s.count(a) == 1
note = '''  STATE 2026-11-14 17:00 (newest; read first). Converter 3b2bcc93 committed in the worktree and LIVE (old exe
  out/zt_backup/zonetool_a0a50852.exe). Core suite 181,961 checks / 1,377 tests, 0 failures; GUI 1,089 checks with
  one failure only while the game runs ("without a game the Live Link commands say why").
  - Occlusion bake: cause found by an Opus agent (research 129) and fixed: core umbra_bake.h tile flags 5 (was 3);
    core umbra_tome.cpp cellsAt / validate follow the game's rule (flag bit 2); the converter's copy re-made
    (umbra_bake.hpp) and its validator accepts 3 or 5; tests/umbra_bake_tests.cpp checks flags 5 and that the same
    tome with 3 is misread; tools/umbra_bake_build_test.py passes with the new converter. NOT SEEN IN GAME since.
    The owner's open map still has iw8r_umbra_bake 1: their next F7 uses the fixed bake.
  - Viewmodel toon (research 128): converter editzone lit_filter got "keep_names" and "materials"
    (tools/patches/zt_filter_keep_names.py). Built zone: out/agent/toon_vm/out/techsets_global_stream_mp.ff
    (16,173,584 bytes; recipe out/agent/toon_vm/edits_vm.json: editzone global_stream_mp, materials
    (_vm_|/vm_|_vm$|_arm_[lr]_), 631 sets, 401 shader copies, all with the screen filter; ACTS 2,632 tested, 0 failed
    when the output path is short; tools/scene_depth_build_test.py --stock: 617 techniques read the scene depth).
    NOT INSTALLED: it changes weapons and hands on every map while installed and the game must be closed; the owner
    was asked. Install: IW8Radiant.exe --install-stock out\\agent\\toon_vm\\out (keeps the stock file in
    <game>\\iw8r_stock, does not touch global_stream_mp.fp, which belongs to another file); restore:
    --restore-stock global_stream_mp. Third-person weapons and some operator arms share those sets and get the look
    too.
  - A helper deleted the whole Temp\\iwb scratch folder by mistake (bench model cache, old proof folders): benches
    refill their caches on the next run; nothing of the owner's was in it.
'''.replace('\n', nl)
s = s.replace(a, note + a)
write(p, s)
print('ok')
