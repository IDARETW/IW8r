import io

# PROGRESS: the live state after the mount edge work (2026-11-14 19:20).

p = 'E:/IW8-Radiant/docs/PROGRESS.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '''  - Converter commits in the worktree: a8114ae6 (mount edges), 7076f8da (adjacency). LIVE STATE: see the end of this
    block.'''.replace('\n', nl)
b = '''  - Converter commits in the worktree: a8114ae6 (mount edges), 7076f8da (adjacency). LIVE since 19:10: the shared
    checkout E:\\mw124\\x64-zt is fast-forwarded to 7076f8da and rebuilt (old exe out/zt_backup/zonetool_3b2bcc93.exe).
    All 27 offline proofs passed on 7076f8da (the run used the worktree exe and out/audit_bin; the mount proof was
    run again on the live exe). Suites: core 182,854 checks / 1,385 tests, GUI 1,093, smoke ok, 0 failures.
  - docs/HANDOVER.md and docs/KEYS_REFERENCE.md are brought up to this state (what was seen in game on 2026-11-14,
    what is reported and not settled, the seven keys that had no row).
  - STILL OPEN, in this order: (1) Shipment's popping objects: unexplained; IDA was not reachable for research 130
    (does a first run compile pipeline states in the background? research 76 saw "Building PSOs" during loading,
    which speaks against it); (2) the owner's steps: File > First-Person Look > Install Into Game; nothing else needs
    them; (3) plan rows left: volumetric box masks, the fog spline for see-through surfaces, viewport volumetric air
    against the game's numbers, radar pings, name-only models, fonts and menus (deferred by decision).'''.replace('\n', nl)
assert s.count(a) == 1
io.open(p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))
print('ok')
