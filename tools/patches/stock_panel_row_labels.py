import io

# The Stock Map panel's row labels (Colour, Intensity, Pitch, Yaw) were given a fixed width from the font at
# construction; under the GUI suite's offscreen platform that font was twice as wide at start-up, so the labels stayed
# 116 px and the column needed 300 px. They now follow the current font: sized when shown and when the font or style
# changes.

ROOT = 'E:/IW8-Radiant/src/qtapp/'


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


rw('stock_map_panel.h', [
('''protected:
    void showEvent(QShowEvent* e) override;''', '''protected:
    void showEvent(QShowEvent* e) override;
    void changeEvent(QEvent* e) override;  // font / style changes: the row labels are sized again'''),
('''    QLabel* sunInfo_ = nullptr;''', '''    QLabel* sunInfo_ = nullptr;
    std::vector<QLabel*> rowLabels_;  // Colour / Intensity / Pitch / Yaw: one width, from the current font
    void fitRowLabels();'''),
])

rw('stock_map_panel.cpp', [
('''    const int labelW = QFontMetrics(font()).horizontalAdvance("Intensity") + 8;
    const auto rowLabel = [labelW](const QString& t) {
        auto* l = new QLabel(t);
        l->setFixedWidth(labelW);
        return l;
    };''', '''    // Row labels share one width, made from the current font (fitRowLabels: when shown and on font changes).
    const auto rowLabel = [this](const QString& t) {
        auto* l = new QLabel(t);
        rowLabels_.push_back(l);
        return l;
    };'''),
('''void StockMapPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);''', '''void StockMapPanel::fitRowLabels() {
    for (QLabel* l : rowLabels_) l->setFixedWidth(l->fontMetrics().horizontalAdvance("Intensity") + 8);
}

void StockMapPanel::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::FontChange || e->type() == QEvent::StyleChange) fitRowLabels();
}

void StockMapPanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    fitRowLabels();'''),
])
print('ok')
