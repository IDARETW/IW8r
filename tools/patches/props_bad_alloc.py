import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


H = 'src/qtapp/main_window.h'
C = 'src/qtapp/main_window.cpp'
s = io.open(H, encoding='utf-8', newline='').read()
assert s.count('    void rebuildReferenceProps();') == 1
patch(H, '''    void rebuildReferenceProps();''', '''    void rebuildReferenceProps();
    void rebuildReferencePropsUnguarded();  // rebuildReferenceProps without its out-of-memory guard''')
patch(C, '''void MainWindow::rebuildReferenceProps() {
''', '''void MainWindow::rebuildReferenceProps() {
    // Verdansk sits near the memory limit of a 16 GB machine: a rebuild that cannot get its memory keeps the props on
    // screen as they are and says so, instead of ending the editor (std::bad_alloc, 2026-11-13).
    try {
        rebuildReferencePropsUnguarded();
    } catch (const std::bad_alloc&) {
        refPropBuilt_.clear();
        refPropBuiltTris_ = 0;
        ctx_.setStatus("Not enough memory to rebuild the stock map's props: they stay as they are (close other programs, or "
                       "lower the streamed world radius in the settings)");
    }
}

void MainWindow::rebuildReferencePropsUnguarded() {
''')
print('ok')
