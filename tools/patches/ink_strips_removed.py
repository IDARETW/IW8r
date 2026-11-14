import io
import re

# The ink strip brushes are gone (owner 2026-11-13: outlines must be a real shader effect of constant screen width, not
# geometry): Tools > Toon Ink Outlines, --ink-outlines and the iw8r_ink material writer are removed. Outlines now come
# from core ink_edges (edge data on brush faces) and the surface shader.


def cut(p, start, end, keep_end=True):
    s = io.open(p, encoding='utf-8', newline='').read()
    a = s.index(start)
    b = s.index(end, a)
    s = s[:a] + (s[b:] if keep_end else s[b + len(end):])
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


M = 'E:/IW8-Radiant/src/qtapp/main_window.cpp'
patch(M, '#include "toon_outline.h"\n', '')
cut(M, '    addCommand(tools, "ToonInkOutlines",', '    addCommand(tools, "AdjustImage",')
cut(M, 'size_t MainWindow::generateInkOutlines(double width) {', 'size_t MainWindow::exportReferenceRegion() {')

H = 'E:/IW8-Radiant/src/qtapp/main_window.h'
s = io.open(H, encoding='utf-8', newline='').read()
a = s.index('    // M6 toon maps: ink strips')
b = s.index('    size_t generateInkOutlines(double width);')
b = s.index('\n', b) + 1
s = s[:a] + s[b:]
io.open(H, 'w', encoding='utf-8', newline='').write(s)

C = 'E:/IW8-Radiant/src/qtapp/main.cpp'
s = io.open(C, encoding='utf-8', newline='').read()
s = re.sub(r'    double inkWidth = 0;  // --ink-outlines <width>\r?\n', '', s)
s = re.sub(r'        else if \(a == "--ink-outlines" && i \+ 1 < argc\) inkWidth = std::atof\(argv\[\+\+i\]\);\r?\n', '', s)
a = s.index('        if (inkWidth > 0) {')
b = s.index('        if (!compileFile.isEmpty()) {', a)
s = s[:a] + s[b:]
assert 'inkWidth' not in s
io.open(C, 'w', encoding='utf-8', newline='').write(s)

B = 'E:/IW8-Radiant/src/qtapp/brush_tools.cpp'
cut(B, 'bool writeInkMaterial(const QString& root, bool* written, QString* err) {', 'bool createBoxBrush(EditorContext& ctx, const AABB& box) {')
BH = 'E:/IW8-Radiant/src/qtapp/brush_tools.h'
s = io.open(BH, encoding='utf-8', newline='').read()
a = s.index('// M6 toon maps: the unlit near-black ink material')
b = s.index('bool writeInkMaterial(const QString& materialRoot, bool* written, QString* err);')
b = s.index('\n', b) + 1
s = s[:a].rstrip('\r\n') + ('\r\n' if '\r\n' in s else '\n') + s[b:]
io.open(BH, 'w', encoding='utf-8', newline='').write(s)
print('ok')
