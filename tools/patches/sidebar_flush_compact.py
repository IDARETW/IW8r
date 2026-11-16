import io

# Side bar revamp: the first shots showed the chips wrapping into 6-8 rows (labels with counts) and the effect preview
# square leaving the list one row tall. Chips: short labels, the count in the tooltip, tighter. The side bar's effect
# square: at most 2/5 of the panel's height (200-420 px), centred.

ROOT = 'E:/IW8-Radiant/src/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw('qtapp/panel_chrome.h', [
('''    struct Chip {
        QString id, label;
        int count = -1;''', '''    struct Chip {
        QString id, label, tip;
        int count = -1;'''),
])
rw('qtapp/panel_chrome.cpp', [
('''        "QToolButton[chip=\\"true\\"] { border: 1px solid %1; border-radius: 9px; padding: 1px 7px; background: transparent; color: %2; }"''',
 '''        "QToolButton[chip=\\"true\\"] { border: 1px solid %1; border-radius: 9px; padding: 0px 7px; min-height: 17px; background: transparent; color: %2; }"'''),
('''QString ChipBar::text(const QString& id) const {
    for (const Chip& c : chips_)
        if (c.id == id) return c.count >= 0 ? QString("%1  %2").arg(c.label).arg(QLocale().toString(c.count)) : c.label;
    return {};
}''', '''QString ChipBar::text(const QString& id) const {
    // The label only (a narrow column fits two or three rows of short chips); the count is in the tooltip.
    for (const Chip& c : chips_)
        if (c.id == id) return c.label;
    return {};
}

static QString chipTip(const QString& tip, int count) {
    return count < 0 ? tip : QString("%1 (%2)").arg(tip, QLocale().toString(count));
}'''),
('''    chips_.push_back({id, label, count, true, b});
    b->setText(text(id));''', '''    chips_.push_back({id, label, tip, count, true, b});
    b->setText(text(id));
    b->setToolTip(chipTip(tip, count));'''),
('''    chips_.push_back({id, label, -1, false, b});''', '''    chips_.push_back({id, label, tip, -1, false, b});'''),
('''        if (c.id == id) {
            c.count = count;
            c.button->setText(text(id));
        }''', '''        if (c.id == id) {
            c.count = count;
            c.button->setToolTip(chipTip(c.tip, count));
        }'''),
])

rw('qtapp/asset_browser.cpp', [
('''{"muzzle", "Muzzle flashes", "Weapon muzzle flashes",''', '''{"muzzle", "Muzzle", "Weapon muzzle flashes",'''),
('''{"smoke", "Smoke & dust", "Smoke, steam, fog, dust",''', '''{"smoke", "Smoke", "Smoke, steam, fog, dust",'''),
('''{"light", "Lights & markers", "Glows, lamps, flares, beams, objective markers",''', '''{"light", "Lights", "Glows, lamps, flares, beams, objective markers",'''),
('''{"impact", "Impacts & damage", "Impacts, debris, glass, tracers, damage states",''', '''{"impact", "Impacts", "Impacts, debris, glass, tracers, damage states",'''),
('''{"ui", "UI & icons", "Menus, HUD, icons, reticles",''', '''{"ui", "UI", "Menus, HUD, icons, reticles",'''),
('''        chips_->chip("loops")->setText(QString("Loops  %1").arg(thousands(loops)));''',
 '''        chips_->chip("loops")->setToolTip(QString("Looping aliases only: ambience, engines, music beds (%1)").arg(thousands(loops)));'''),
('''        split->setSizes({130, 400});''', '''        split->setSizes({110, 400});'''),
('''void AssetBrowser::showEvent(QShowEvent* e) {''', '''void AssetBrowser::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    // The effect square takes at most 2/5 of the panel's height, so the list keeps its room (first shots: one row).
    if (fxPreview_) fxPreview_->setMaxSide(std::clamp(height() * 2 / 5, 200, 420));
}

void AssetBrowser::showEvent(QShowEvent* e) {'''),
])
rw('qtapp/asset_browser.h', [
('''protected:
    void showEvent(QShowEvent* e) override;''', '''protected:
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;'''),
])

rw('qtapp/vfx_preview.h', [
('''    int heightForWidth(int w) const override;
    QSize sizeHint() const override;''', '''    int heightForWidth(int w) const override;
    QSize sizeHint() const override;
    // The square's largest side (520 by default; the side bar keeps it to part of the panel's height).
    void setMaxSide(int side);
    int maxSide() const { return maxSide_; }'''),
('''    int side() const;               // the square's side for the current width''', '''    int side() const;               // the square's side for the current width
    int maxSide_ = 520;'''),
])
rw('qtapp/vfx_preview.cpp', [
('''int VfxPreview::side() const { return std::max(1, std::min(width(), kMaxSide)); }''',
 '''int VfxPreview::side() const { return std::max(1, std::min(width(), maxSide_)); }

void VfxPreview::setMaxSide(int s) {
    s = std::clamp(s, 64, kMaxSide);
    if (s == maxSide_) return;
    maxSide_ = s;
    updateGeometry();
    followWidth();
    dirty_ = true;
    update();
}'''),
('''int VfxPreview::heightForWidth(int w) const { return std::max(1, std::min(w, kMaxSide)) + stripHeight(); }''',
 '''int VfxPreview::heightForWidth(int w) const { return std::max(1, std::min(w, maxSide_)) + stripHeight(); }'''),
])

rw('qtapp/qt_gui_test_sidebar.cpp', [
('''            check(vr.width() == vr.height() && vr.width() >= std::min(pv->width(), 520) - 1 && pv->height() > vr.height(),
                  QString("the effect preview is a square across the panel (%1 x %2 in a %3 x %4 px preview)")
                      .arg(vr.width()).arg(vr.height()).arg(pv->width()).arg(pv->height()));''',
 '''            // A square, as wide as the panel or as its height allows (2/5 of it: the list keeps its room), centred.
            check(vr.width() == vr.height() && vr.width() >= std::min(pv->width(), pv->maxSide()) - 1 && pv->height() > vr.height() &&
                      std::abs(vr.center().x() - pv->width() / 2) <= 1 && pv->maxSide() <= std::max(200, eb->height() * 2 / 5),
                  QString("the effect preview is a centred square (%1 x %2 in a %3 x %4 px preview; at most %5 in a %6 px high panel)")
                      .arg(vr.width()).arg(vr.height()).arg(pv->width()).arg(pv->height()).arg(pv->maxSide()).arg(eb->height()));'''),
])
print('ok')
