import io

# Owner request 2026-11-14: the Models and Prefabs tabs show textured pictures (list thumbnails and the side preview),
# pre-generated and kept in a cache, not drawn grey. Part 1: what the picture maker needs from the views.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/qtapp/camera_view.h', '''    QImage grabFrame();  // last rendered frame (D3D readback)''',
      '''    QImage grabFrame();  // last rendered frame (D3D readback)
    // Images this view still waits for: textures held back by the per-frame load budget, and materials whose images
    // the caches are still fetching. A picture taken while this is above 0 shows flat colours where textures belong.
    int texturesWaiting() const;''')
patch('src/qtapp/camera_view.cpp', '''QImage CameraView::grabFrame() {''', '''int CameraView::texturesWaiting() const {
    int n = moreToLoad_ || refMoreToLoad_ ? 1 : 0;
    if (imageLookup_)
        for (const std::string& m : requested_)
            if (!texIds_.count(m) && !imageLookup_(m)) ++n;
    return n;
}

QImage CameraView::grabFrame() {''')

patch('src/qtapp/asset_preview_window.h', '''    QString assetName() const { return name_; }''', '''    QString assetName() const { return name_; }

    // Pictures for the asset browsers (TexturedThumbs): the Lit view without the floor; a square picture of the asset
    // as shown, from its three-quarter view; the images and the models the view still waits for.
    void setPictureStyle();
    QImage picture(int side);
    int texturesWaiting() const;
    QStringList modelNames() const;''')
patch('src/qtapp/asset_preview_window.cpp', '''void AssetPreviewWindow::frame() {''', '''void AssetPreviewWindow::setPictureStyle() {
    floor_->blockSignals(true);
    floor_->setChecked(false);
    floor_->blockSignals(false);
    mode_->setCurrentIndex(0);  // Lit
    applyMode(0);
}

QImage AssetPreviewWindow::picture(int side) { return view_->renderImage(side, side, true, 1.0, 0, false); }

int AssetPreviewWindow::texturesWaiting() const { return view_->texturesWaiting(); }

QStringList AssetPreviewWindow::modelNames() const {
    QStringList out;
    for (const Entity& e : ctx_.doc.map().entities)
        if (const std::string* m = e.value("model"); m && !m->empty() && (*m)[0] != '*') out << qs(*m);
    out.removeDuplicates();
    return out;
}

void AssetPreviewWindow::frame() {''')
print('ok')
