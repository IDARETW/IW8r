import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


R = 'src/viewport/render.cpp'
patch(R, '''    rd.AntialiasedLineEnable = TRUE;
    dev_->CreateRasterizerState(&rd, &rsLines_);''', '''    rd.AntialiasedLineEnable = TRUE;
    dev_->CreateRasterizerState(&rd, &rsLines_);
    rd.AntialiasedLineEnable = FALSE;
    dev_->CreateRasterizerState(&rd, &rsLinesFast_);''')
patch(R, '''    safeRelease(rsLines_);''', '''    safeRelease(rsLines_);
    safeRelease(rsLinesFast_);''')
patch(R, '''        IW8R_PROF("d.lines");
        ctx_->RSSetState(rsLines_);''', '''        IW8R_PROF("d.lines");
        // Antialiased lines cost about 6 ns each on the GPU: Verdansk's 1.2 million entity outline lines took 7 ms a
        // frame. Past half a million line vertices they are drawn plain.
        const size_t lineVerts = (fLines.ready() ? fLines.s->v.size() : 0) + (fLinesMisc.ready() ? fLinesMisc.s->v.size() : 0);
        ctx_->RSSetState(lineVerts > 500000 && rsLinesFast_ ? rsLinesFast_ : rsLines_);''')
RH = 'src/viewport/render.h'
s = io.open(RH, encoding='utf-8', newline='').read()
import re
m = re.search(r'ID3D11RasterizerState\* rsLines_ = nullptr;', s)
assert m, 'rsLines_ member'
patch(RH, '''ID3D11RasterizerState* rsLines_ = nullptr;''', '''ID3D11RasterizerState* rsLines_ = nullptr;
    ID3D11RasterizerState* rsLinesFast_ = nullptr;  // the same without line antialiasing (very many lines)''')
print('ok')
