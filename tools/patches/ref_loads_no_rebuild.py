import io
p = 'src/qtapp/camera_view.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'


def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, a[:80]
    s = s.replace(a, b)


rep('''        if (moreToLoad_) {
            refPollDue_ = true;  // the budget stopped the loads: the next frame goes on (render() asks for it below)
        } else if''', '''        // The stock reference's loads do not rebuild the map's own geometry: only its own textures do (mp_aniyah_tac
        // rebuilt everything, 175 ms and 173 MB of uploads, on every frame while 9,000 layer textures streamed in).
        refMoreToLoad_ = moreToLoad_;
        moreToLoad_ = false;
        if (refMoreToLoad_) {
            refPollDue_ = true;  // the budget stopped the loads: the next frame goes on (render() asks for it below)
        } else if''')
rep('''    if (moreToLoad_) QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);  // finish loading next frame''',
    '''    if (moreToLoad_ || refMoreToLoad_)
        QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);  // finish loading next frame''')
rep('''    while (moreToLoad_ && t.elapsed() < settleMs) {''', '''    while ((moreToLoad_ || refMoreToLoad_) && t.elapsed() < settleMs) {''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)

p = 'src/qtapp/camera_view.h'
s = io.open(p, encoding='utf-8', newline='').read()
a = '    std::vector<std::string> refTexMaterials_;'
assert s.count(a) == 1
s = s.replace(a, '    bool refMoreToLoad_ = false;  // the reference loop stopped at the per-frame load budget\n' + a, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
