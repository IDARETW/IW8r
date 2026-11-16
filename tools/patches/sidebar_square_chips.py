import io

# Owner 2026-11-15: "make them square and compact in the menus instead of rounded on the ends, and more uniform".
# Chips: square corners, one short height, tight padding, and every chip of a panel the same width, in a grid that
# fills the row (as many columns as the widest label allows). The pills on the list rows (loop, length, start, stock)
# are square too, and the details strips' buttons share one width.

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
('''signals:
    void currentChanged(const QString& id);
    void toggled(const QString& id, bool on);

protected:
    void changeEvent(QEvent* e) override;

private:
    void restyle();''', '''    int chipWidth() const;  // the one width every chip has (tests)
    static constexpr int kChipHeight = 20;

signals:
    void currentChanged(const QString& id);
    void toggled(const QString& id, bool on);

protected:
    void changeEvent(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void restyle();
    void fitChips();  // the uniform grid: one width for every chip, filling the row'''),
('''    QPushButton* addAction(char16_t glyph, const QString& text, const QString& tip = QString(), const QString& objectName = QString());''',
 '''    QPushButton* addAction(char16_t glyph, const QString& text, const QString& tip = QString(), const QString& objectName = QString());
    void evenButtons();  // the strip's buttons share one width (the widest one's)'''),
])

rw('qtapp/panel_chrome.cpp', [
('''#include <QResizeEvent>''', '''#include <QResizeEvent>
#include <QStyle>'''),
('''    // Rounded chips: a thin rule round each, the selected ones in the highlight colour.
    const QPalette p = QApplication::palette();
    const QString sheet = QString(
        "QToolButton[chip=\\"true\\"] { border: 1px solid %1; border-radius: 9px; padding: 0px 7px; min-height: 17px; background: transparent; color: %2; }"''',
 '''    // Square, compact chips (owner 2026-11-15): a thin rule round each, the selected ones in the highlight colour.
    const QPalette p = QApplication::palette();
    const QString sheet = QString(
        "QToolButton[chip=\\"true\\"] { border: 1px solid %1; border-radius: 0px; padding: 0px 4px; background: transparent; color: %2; }"'''),
('''void ChipBar::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::PaletteChange) restyle();  // the same sheet again changes nothing (no loop)
}''', '''void ChipBar::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::PaletteChange) restyle();  // the same sheet again changes nothing (no loop)
    if (e->type() == QEvent::FontChange || e->type() == QEvent::StyleChange) fitChips();
}

void ChipBar::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    fitChips();
}

void ChipBar::fitChips() {
    // One width for every chip: the widest label's, then widened so the columns fill the row exactly.
    if (chips_.isEmpty()) return;
    int widest = 0;
    for (const Chip& c : chips_) {
        const QFontMetrics fm(c.button->font());
        const int icon = c.button->icon().isNull() ? 0 : c.button->iconSize().width() + 4;
        widest = std::max(widest, fm.horizontalAdvance(c.button->text().replace("&&", "&")) + icon + 14);
    }
    const QMargins m = flow_->contentsMargins();
    const int avail = std::max(widest, width() - m.left() - m.right()), gap = 4;
    const int cols = std::max(1, std::min(int(chips_.size()), (avail + gap) / (widest + gap)));
    const int cell = std::max(widest, (avail - (cols - 1) * gap) / cols);
    for (const Chip& c : chips_)
        if (c.button->width() != cell || c.button->height() != kChipHeight) c.button->setFixedSize(cell, kChipHeight);
}

int ChipBar::chipWidth() const { return chips_.isEmpty() ? 0 : chips_.front().button->width(); }'''),
('''void ChipBar::clear() {
    for (Chip& c : chips_) delete c.button;
    chips_.clear();
    current_.clear();
}''', '''void ChipBar::clear() {
    for (Chip& c : chips_) delete c.button;
    chips_.clear();
    current_.clear();
}
'''),
('''    connect(b, &QToolButton::clicked, this, [this, id, b] {
        if (current_ == id) {''', '''    b->setFixedHeight(kChipHeight);
    fitChips();
    connect(b, &QToolButton::clicked, this, [this, id, b] {
        if (current_ == id) {'''),
('''    connect(b, &QToolButton::toggled, this, [this, id](bool v) { emit toggled(id, v); });
    return b;''', '''    b->setFixedHeight(kChipHeight);
    fitChips();
    connect(b, &QToolButton::toggled, this, [this, id](bool v) { emit toggled(id, v); });
    return b;'''),
('''    actions_->addWidget(b);
    return b;''', '''    actions_->addWidget(b);
    evenButtons();
    return b;'''),
('''QString DetailsStrip::titleText() const { return fullName_; }''', '''QString DetailsStrip::titleText() const { return fullName_; }

void DetailsStrip::evenButtons() {
    // One width for the strip's buttons, so a row of actions lines up.
    const QList<QPushButton*> buttons = findChildren<QPushButton*>(Qt::FindChildrenRecursively);
    int w = 0;
    for (QPushButton* b : buttons) w = std::max(w, b->sizeHint().width());
    for (QPushButton* b : buttons) b->setMinimumWidth(w);
}'''),
])

# Square pills on the rows.
rw('qtapp/asset_browser.cpp', [
('''            p->setBrush(loop ? QColor(c.red(), c.green(), c.blue(), 40) : QColor(Qt::transparent));
            p->drawRoundedRect(pr, pr.height() / 2, pr.height() / 2);''', '''            p->setBrush(loop ? QColor(c.red(), c.green(), c.blue(), 40) : QColor(Qt::transparent));
            p->drawRect(pr.adjusted(0.5, 0.5, -0.5, -0.5));  // square, as the chips (owner 2026-11-15)'''),
])
rw('qtapp/panels.cpp', [
('''            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(pr, pr.height() / 2, pr.height() / 2);''', '''            p->setBrush(Qt::NoBrush);
            p->drawRect(pr.adjusted(0.5, 0.5, -0.5, -0.5));  // square, as the chips'''),
])

# The test: square, one width, one height.
rw('qtapp/qt_gui_test_sidebar.cpp', [
('''            check(!b->countText().isEmpty() && b->findChild<QToolButton*>("assetFavourites") && b->findChild<QWidget*>("assetChips") && view &&
                      view->frameShape() == QFrame::NoFrame,
                  QString("an asset panel has the flush header (count %1), favourites star, chips and an edge-to-edge list").arg(b->countText()));''',
 '''            check(!b->countText().isEmpty() && b->findChild<QToolButton*>("assetFavourites") && b->findChild<QWidget*>("assetChips") && view &&
                      view->frameShape() == QFrame::NoFrame,
                  QString("an asset panel has the flush header (count %1), favourites star, chips and an edge-to-edge list").arg(b->countText()));
            // Owner 2026-11-15: square, compact, uniform chips.
            QSet<int> widths, heights;
            if (auto* bar = b->findChild<ChipBar*>())
                for (QToolButton* c : bar->findChildren<QToolButton*>())
                    if (c->property("chip").toBool() && c->isVisibleTo(bar)) widths.insert(c->width()), heights.insert(c->height());
            const QString sheet = b->findChild<ChipBar*>() ? b->findChild<ChipBar*>()->styleSheet() : QString();
            check(widths.size() == 1 && heights == QSet<int>{ChipBar::kChipHeight} && sheet.contains("border-radius: 0px"),
                  QString("its chips are square and all one size (%1 width(s), height %2)").arg(widths.size()).arg(heights.isEmpty() ? 0 : *heights.begin()));'''),
('''#include <QSignalSpy>''', '''#include <QSet>
#include <QSignalSpy>'''),
])
print('ok')
