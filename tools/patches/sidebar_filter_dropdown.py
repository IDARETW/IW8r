import io

# Owner 2026-11-15: "make the buttons into a dropdown list". The filter chips of every side panel become one dropdown:
# a button across the panel naming what is shown ("Fire", "Walls · In this map"), its list holding the categories with
# their counts (one at a time) and, under a rule, the switches that combine with them (Recent, Loops, In this map).
# The ChipBar class keeps its interface (addChip / addToggle / setCount / setCurrent / isOn / setOn / chipIds and the
# currentChanged / toggled signals), so the panels are unchanged; chip(id) is now the list entry (a QAction).

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


def slice_replace(p, start, end, new):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    if new in s:
        return
    assert s.count(start) == 1 and s.count(end) == 1, (p, s.count(start), s.count(end))
    a = s.index(start)
    b = s.index(end)
    assert a < b
    s = s[:a] + new + s[b:]
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


HEADER = '''// The panel's filter: one dropdown (owner 2026-11-15: "make the buttons into a dropdown list"). Its button spans the
// panel and names what is shown; its list has the categories with their counts (one at a time; the first added is
// selected) and, under a rule, switches that combine with them (Recent, Loops, In this map). Widgets added with
// addWidget (a kit box, a size box) sit beside it.
class ChipBar : public QWidget {
    Q_OBJECT
public:
    explicit ChipBar(QWidget* parent = nullptr);
    void clear();
    // A category; the first one added is selected. count < 0 shows no count.
    QAction* addChip(const QString& id, const QString& label, int count = -1, const QString& tip = QString(), const QColor& dot = QColor());
    // A switch that is on or off by itself.
    QAction* addToggle(const QString& id, const QString& label, bool on = false, const QString& tip = QString());
    void addWidget(QWidget* w);
    void setCount(const QString& id, int count);
    void setCurrent(const QString& id);  // no signal
    QString current() const { return current_; }
    bool isOn(const QString& id) const;
    void setOn(const QString& id, bool on);  // no signal
    QStringList chipIds() const;
    QAction* chip(const QString& id) const;  // the list entry
    QPushButton* button() const { return button_; }
    QString shownText() const;               // what the button says (tests)

signals:
    void currentChanged(const QString& id);
    void toggled(const QString& id, bool on);

protected:
    void changeEvent(QEvent* e) override;

private:
    void restyle();
    void updateButton();
    QString entryText(const QString& id) const;
    QHBoxLayout* row_;
    QPushButton* button_;
    QMenu* menu_;
    QAction* rule_ = nullptr;  // the separator above the switches
    QString current_;
    struct Chip {
        QString id, label, tip;
        int count = -1;
        bool exclusive = true;
        QAction* action = nullptr;
        QIcon dot;
    };
    QList<Chip> chips_;
    QList<QWidget*> extras_;
};
'''

slice_replace('qtapp/panel_chrome.h', '// A wrapping row of filter chips.\n', '// The strip at the bottom of a panel', HEADER + '\n')

IMPL = r'''// ------------------------------------------------------------------------------------------------- ChipBar

ChipBar::ChipBar(QWidget* parent) : QWidget(parent) {
    setObjectName("chipBar");
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(6, 0, 6, 2);
    row_->setSpacing(4);
    button_ = new QPushButton(this);
    button_->setObjectName("filterDropdown");
    button_->setToolTip("What the list shows");
    button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button_->setMinimumWidth(0);
    menu_ = new QMenu(button_);
    menu_->setToolTipsVisible(true);
    menu_->setSeparatorsCollapsible(false);
    button_->setMenu(menu_);
    row_->addWidget(button_, 1);
    restyle();
}

void ChipBar::restyle() {
    // A square box across the panel, its text on the left, the arrow on the right.
    const QString sheet = QString("QPushButton#filterDropdown { border: 1px solid %1; border-radius: 0px; padding: 2px 6px; text-align: left; "
                                  "background: %2; color: %3; }"
                                  "QPushButton#filterDropdown:hover { border-color: %4; }"
                                  "QPushButton#filterDropdown::menu-indicator { subcontrol-origin: padding; subcontrol-position: right center; "
                                  "right: 6px; }")
                              .arg(themeMix(0.3).name(), themeMix(0.06).name(), themeMix(0.9).name(), themeMix(0.5).name());
    if (button_->styleSheet() != sheet) button_->setStyleSheet(sheet);
}

void ChipBar::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::PaletteChange) restyle();  // the same sheet again changes nothing (no loop)
}

QString ChipBar::entryText(const QString& id) const {
    // The label, the count in the list's right column; a '&' is shown, not taken as a shortcut marker ("S&D").
    for (const Chip& c : chips_)
        if (c.id == id) {
            const QString label = QString(c.label).replace("&", "&&");
            return c.count >= 0 ? label + "\t" + QLocale().toString(c.count) : label;
        }
    return {};
}

QString ChipBar::shownText() const { return button_->text(); }

void ChipBar::updateButton() {
    // What is shown: the category, then the switches that are on ("Walls · In this map").
    QStringList parts;
    QIcon icon;
    for (const Chip& c : chips_)
        if (c.exclusive && c.id == current_) {
            parts << c.label;
            icon = c.dot;
        }
    for (const Chip& c : chips_)
        if (!c.exclusive && c.action->isChecked()) parts << c.label;
    QString text = parts.join(factSep());
    text.replace("&", "&&");
    button_->setText(text);
    button_->setIcon(icon.isNull() ? glyphIcon(glyph::Filter, 12, themeMix(0.6)) : icon);
    button_->setIconSize(QSize(12, 12));
}

void ChipBar::clear() {
    for (Chip& c : chips_) delete c.action;
    chips_.clear();
    delete rule_;
    rule_ = nullptr;
    current_.clear();
    updateButton();
}

QAction* ChipBar::addChip(const QString& id, const QString& label, int count, const QString& tip, const QColor& dot) {
    QIcon icon;
    if (dot.isValid()) {
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        QPainter pt(&pm);
        pt.setRenderHint(QPainter::Antialiasing);
        pt.setPen(Qt::NoPen);
        pt.setBrush(dot);
        pt.drawEllipse(QRectF(4, 4, 8, 8));
        pt.end();
        icon = QIcon(pm);
    }
    auto* a = new QAction(menu_);
    a->setObjectName("chip_" + id);
    a->setCheckable(true);
    a->setToolTip(tip);
    a->setIcon(icon);
    chips_.push_back({id, label, tip, count, true, a, icon});
    a->setText(entryText(id));
    // Categories above the rule, the switches under it.
    menu_->insertAction(rule_, a);
    bool first = true;
    for (const Chip& c : chips_)
        if (c.exclusive && c.action != a) first = false;
    if (first) {
        current_ = id;
        a->setChecked(true);
    }
    connect(a, &QAction::triggered, this, [this, id] {
        const bool changed = current_ != id;
        setCurrent(id);  // the chosen category stays checked (choosing it again changes nothing)
        if (changed) emit currentChanged(id);
    });
    updateButton();
    return a;
}

QAction* ChipBar::addToggle(const QString& id, const QString& label, bool on, const QString& tip) {
    if (!rule_) rule_ = menu_->addSeparator();
    auto* a = menu_->addAction(QString(label).replace("&", "&&"));
    a->setObjectName("chip_" + id);
    a->setCheckable(true);
    a->setChecked(on);
    a->setToolTip(tip);
    chips_.push_back({id, label, tip, -1, false, a, QIcon()});
    connect(a, &QAction::toggled, this, [this, id](bool v) {
        updateButton();
        emit toggled(id, v);
    });
    updateButton();
    return a;
}

void ChipBar::addWidget(QWidget* w) {
    w->setParent(this);
    extras_.push_back(w);
    row_->addWidget(w);
}

void ChipBar::setCount(const QString& id, int count) {
    for (Chip& c : chips_)
        if (c.id == id) {
            c.count = count;
            if (c.exclusive) c.action->setText(entryText(id));
        }
}

void ChipBar::setCurrent(const QString& id) {
    current_ = id;
    for (Chip& c : chips_)
        if (c.exclusive) c.action->setChecked(c.id == id);
    updateButton();
}

bool ChipBar::isOn(const QString& id) const {
    for (const Chip& c : chips_)
        if (c.id == id) return c.action->isChecked();
    return false;
}

void ChipBar::setOn(const QString& id, bool on) {
    for (Chip& c : chips_)
        if (c.id == id && !c.exclusive) {
            const QSignalBlocker block(c.action);
            c.action->setChecked(on);
        }
    updateButton();
}

QStringList ChipBar::chipIds() const {
    QStringList out;
    for (const Chip& c : chips_) out << c.id;
    return out;
}

QAction* ChipBar::chip(const QString& id) const {
    for (const Chip& c : chips_)
        if (c.id == id) return c.action;
    return nullptr;
}

'''
slice_replace('qtapp/panel_chrome.cpp',
              '// ------------------------------------------------------------------------------------------------- ChipBar\n',
              '// ------------------------------------------------------------------------------------------------- DetailsStrip\n', IMPL)

rw('qtapp/panel_chrome.cpp', [
('''#include <QResizeEvent>
#include <QStyle>''', '''#include <QResizeEvent>
#include <QStyle>
#include <QAction>'''),
])
rw('qtapp/panel_chrome.h', [
('''class QHBoxLayout;
class QLabel;''', '''class QAction;
class QHBoxLayout;
class QLabel;'''),
])

# The kit box and the tile-size box beside the dropdown keep their own width.
rw('qtapp/asset_browser.cpp', [
('''        chips_->chip("loops")->setToolTip(QString("Looping aliases only: ambience, engines, music beds (%1)").arg(thousands(loops)));''',
 '''        chips_->chip("loops")->setText(QString("Loops\\t%1").arg(thousands(loops)));'''),
])

# The tests: the filter is a dropdown; switches are its checkable entries.
rw('qtapp/qt_gui_test_sidebar.cpp', [
('''            // Owner 2026-11-15: square, compact, uniform chips.
            QSet<int> widths, heights;
            if (auto* bar = b->findChild<ChipBar*>())
                for (QToolButton* c : bar->findChildren<QToolButton*>())
                    if (c->property("chip").toBool() && c->isVisibleTo(bar)) widths.insert(c->width()), heights.insert(c->height());
            const QString sheet = b->findChild<ChipBar*>() ? b->findChild<ChipBar*>()->styleSheet() : QString();
            check(widths.size() == 1 && heights == QSet<int>{ChipBar::kChipHeight} && sheet.contains("border-radius: 0px"),
                  QString("its chips are square and all one size (%1 width(s), height %2)").arg(widths.size()).arg(heights.isEmpty() ? 0 : *heights.begin()));''',
 '''            // Owner 2026-11-15: the filter is a dropdown list (a square box across the panel), not a row of buttons.
            auto* bar = b->findChild<ChipBar*>();
            QPushButton* drop = bar ? bar->button() : nullptr;
            const int entries = drop && drop->menu() ? int(drop->menu()->actions().size()) : 0;
            check(drop && drop->menu() && entries >= 2 && !bar->findChild<QToolButton*>() && drop->styleSheet().contains("border-radius: 0px") &&
                      !bar->shownText().isEmpty(),
                  QString("its filter is one dropdown (\\"%1\\", %2 entries)").arg(bar ? bar->shownText() : QString()).arg(entries));'''),
('''            auto* loops = sb->findChild<QToolButton*>("chip_loops");
            if (loops) loops->click();''', '''            auto* loops = sb->findChild<QAction*>("chip_loops");
            if (loops) loops->trigger();'''),
('''            if (loops) loops->click();
            sb->setChip("mus");''', '''            const QString shown = sb->findChild<ChipBar*>() ? sb->findChild<ChipBar*>()->shownText() : QString();
            check(shown.endsWith("Loops"), "the dropdown names the switches that are on (" + shown + ")");
            if (loops) loops->trigger();
            sb->setChip("mus");'''),
])
print('ok')
