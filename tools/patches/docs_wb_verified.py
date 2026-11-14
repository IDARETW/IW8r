import io

# Research 120: the Replay exe's own white balance functions were read (Hex-Rays) and agree with the game-test
# source step for step.

p = 'E:/IW8-Radiant/docs/research/120_white_balance.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:60]
    s = s.replace(a, b)


rep('''which is not Replay; the Replay function at
`0x140EF8500` uses the same constants and calls, but its arithmetic was not compared line by line. Nothing was run in
the game.''', '''which is not Replay. The two Replay functions (`0x140EF8500` the matrix, `0x140EF8090` the tint) were then read in
Hex-Rays and agree with GT step for step (section 3), so the formula is VERIFIED for Replay. Nothing was run in the
game.''')
rep('''## 3. The matrix (INFERRED arithmetic, VERIFIED constants)
''', '''## 3. The matrix (VERIFIED in the Replay exe: arithmetic and constants)

Replay `0x140EF8500`: the same branches (illuminant 0 with method 0 / 1 / other, 1, 2, table), the same D65
`(0.95047, 1, 1.0888)`, `MatrixInverse33` of the method's matrix, and the four products in the order below. Replay
`0x140EF8090`: the tint with `temperature + 1`, the uv forms, `strength x 0.00033333333`, `Math_SinCosFloat(-pi/2)` and
the clamps. GT `RB_GetUIWhiteBalanceParams` (`rb_backend.cpp:7413`) hands the shader the matrix's columns as its three
rows, which is the `out = M in` form the editor uses.
''')
rep('''- Whether Replay's arithmetic equals the game-test build's in every step (same constants and calls; not compared
  instruction by instruction). `FastSinCos(-pi/2)` is taken as exactly (-1, 0).''',
    '''- `Math_SinCosFloat(-pi/2)` is taken as exactly (-1, 0); an approximation there would turn the tint by a fraction of
  a degree.''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
