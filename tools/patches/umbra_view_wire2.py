import io

# Continuation of umbra_view_wire.py (its last patches on main_window_umbra.cpp).


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


U = 'E:/IW8-Radiant/src/qtapp/main_window_umbra.cpp'
patch(U, '''void MainWindow::setUmbraView(bool cull, bool overlay) {
    umbraCull_ = cull;
    umbraOverlay_ = overlay;''', '''void MainWindow::setUmbraView(bool cull, bool overlay) {
    umbraCull_ = cull;
    umbraOverlay_ = overlay;
    umbraPose_.clear();
    if (QAction* a = action("UmbraCullByTome")) a->setChecked(cull);
    if (QAction* a = action("UmbraShowCells")) a->setChecked(overlay);''')
patch(U, '''                umbraZone_ = zone;
                umbraTome_ = tome;
                umbraIndex_ = index;
                updateUmbraView();''', '''                umbraZone_ = zone;
                umbraTome_ = tome;
                umbraIndex_ = index;
                umbraPose_.clear();
                updateUmbraView();''')
patch(U, '''(err.empty() ? QString() : " (" + qs(err) + ")")''', '''(err.empty() ? QString() : " (" + QString::fromStdString(err) + ")")''')
print('ok')
