#include "panel_chrome.h"

#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QSignalBlocker>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QAction>
#include <QToolButton>
#include <QVBoxLayout>

#include "flow_layout.h"

namespace iw8r::qt {

QString glyphFont() {
    static const QString family = QFontDatabase::families().contains("Segoe MDL2 Assets") ? QString("Segoe MDL2 Assets") : QString();
    return family;
}

QColor themeMix(double k) {
    const QPalette p = QApplication::palette();
    const QColor t = p.color(QPalette::WindowText), b = p.color(QPalette::Window);
    return QColor::fromRgbF(float(t.redF() * k + b.redF() * (1 - k)), float(t.greenF() * k + b.greenF() * (1 - k)),
                            float(t.blueF() * k + b.blueF() * (1 - k)));
}

QString factSep() { return QString(" ") + QChar(0x00B7) + QString(" "); }

QIcon glyphIcon(char16_t g, int side, const QColor& color, const QString& fallback) {
    const QColor c = color.isValid() ? color : QApplication::palette().color(QPalette::WindowText);
    QIcon icon;
    for (const int s : {side, side * 2}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        const bool font = !glyphFont().isEmpty();
        QFont f(font ? glyphFont() : QString("Segoe UI"));
        f.setPixelSize(font ? s * 7 / 8 : s * 5 / 8);
        f.setBold(!font);
        p.setFont(f);
        p.setPen(c);
        p.drawText(pm.rect(), Qt::AlignCenter, font ? QString(QChar(g)) : fallback.left(2));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

// ------------------------------------------------------------------------------------------------- PanelHeader

PanelHeader::PanelHeader(const QString& title, QWidget* parent) : QWidget(parent) {
    setObjectName("panelHeader");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 2, 2, 0);
    row->setSpacing(6);
    title_ = new QLabel(title, this);
    title_->setObjectName("panelTitle");
    count_ = new QLabel(this);
    count_->setObjectName("panelCount");
    count_->setMinimumWidth(0);
    count_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // a long count never widens the column
    row->addWidget(title_);
    row->addWidget(count_, 1);
    buttons_ = new QHBoxLayout();
    buttons_->setSpacing(0);
    row->addLayout(buttons_);
    restyle();
}

void PanelHeader::setTitle(const QString& title) { title_->setText(title); }

void PanelHeader::setCount(const QString& text, const QString& tip) {
    count_->setText(text);
    count_->setToolTip(tip);
}

QString PanelHeader::count() const { return count_->text(); }

QToolButton* PanelHeader::addButton(char16_t g, const QString& tip, bool checkable, const QString& objectName) {
    auto* b = new QToolButton(this);
    b->setAutoRaise(true);
    b->setCheckable(checkable);
    b->setToolTip(tip);
    b->setObjectName(objectName);
    b->setIcon(glyphIcon(g, 14));
    b->setIconSize(QSize(14, 14));
    b->setFixedSize(24, 22);
    b->setProperty("glyph", int(g));
    buttons_->addWidget(b);
    return b;
}

QToolButton* PanelHeader::addMenuButton(QMenu* menu, const QString& tip, const QString& objectName) {
    QToolButton* b = addButton(glyph::More, tip, false, objectName);
    b->setMenu(menu);
    b->setPopupMode(QToolButton::InstantPopup);
    b->setStyleSheet("QToolButton::menu-indicator { image: none; width: 0px; }");  // the dots are the indicator
    return b;
}

void PanelHeader::restyle() {
    QFont f = title_->font();
    f.setBold(true);
    title_->setFont(f);
    QPalette p = count_->palette();
    p.setColor(QPalette::WindowText, themeMix(0.6));
    count_->setPalette(p);
    for (QToolButton* b : findChildren<QToolButton*>())
        if (const int g = b->property("glyph").toInt()) b->setIcon(glyphIcon(char16_t(g), 14));
}

void PanelHeader::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::PaletteChange && !property("restyling").toBool()) {
        setProperty("restyling", true);
        restyle();
        setProperty("restyling", false);
    }
}

// ------------------------------------------------------------------------------------------------- ChipBar

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

// ------------------------------------------------------------------------------------------------- DetailsStrip

DetailsStrip::DetailsStrip(QWidget* parent) : QFrame(parent) {
    setObjectName("detailsStrip");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 5, 6, 5);
    outer->setSpacing(3);
    name_ = new QLabel(this);
    name_->setObjectName("detailsName");
    name_->setMinimumWidth(0);
    name_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    facts_ = new QLabel(this);
    facts_->setObjectName("detailsFacts");
    facts_->setWordWrap(true);
    facts_->setMinimumWidth(0);
    facts_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    outer->addWidget(name_);
    outer->addWidget(facts_);
    body_ = new QVBoxLayout();
    body_->setContentsMargins(0, 2, 0, 2);
    body_->setSpacing(3);
    outer->addLayout(body_);
    // The buttons wrap onto a second line in a narrow column instead of widening it.
    auto* actionsBox = new QWidget(this);
    actions_ = new FlowLayout(actionsBox, 4, 4);
    outer->addWidget(actionsBox);
    restyle();
}

void DetailsStrip::restyle() {
    // A rule above the strip, the strip a shade off the panel's background.
    const QString sheet = QString("QFrame#detailsStrip { border: none; border-top: 1px solid %1; background: %2; }")
                              .arg(themeMix(0.22).name(), themeMix(0.04).name());
    if (styleSheet() != sheet) setStyleSheet(sheet);
    QFont f = name_->font();
    f.setBold(true);
    name_->setFont(f);
    QPalette p = facts_->palette();
    p.setColor(QPalette::WindowText, themeMix(0.62));
    facts_->setPalette(p);
}

void DetailsStrip::changeEvent(QEvent* e) {
    QFrame::changeEvent(e);
    if (e->type() == QEvent::PaletteChange) restyle();
}

void DetailsStrip::setTitle(const QString& name, const QString& facts) {
    fullName_ = name;
    name_->setToolTip(name);
    facts_->setText(facts);
    facts_->setVisible(!facts.isEmpty());
    elide();
}

QString DetailsStrip::titleText() const { return fullName_; }

void DetailsStrip::evenButtons() {
    // One width for the strip's buttons, so a row of actions lines up.
    const QList<QPushButton*> buttons = findChildren<QPushButton*>(Qt::FindChildrenRecursively);
    int w = 0;
    for (QPushButton* b : buttons) w = std::max(w, b->sizeHint().width());
    for (QPushButton* b : buttons) b->setMinimumWidth(w);
}
QString DetailsStrip::factsText() const { return facts_->text(); }

void DetailsStrip::elide() {
    const int w = std::max(60, width() - 14);
    name_->setText(QFontMetrics(name_->font()).elidedText(fullName_, Qt::ElideMiddle, w));
}

void DetailsStrip::resizeEvent(QResizeEvent* e) {
    QFrame::resizeEvent(e);
    elide();
}

QPushButton* DetailsStrip::addAction(char16_t g, const QString& text, const QString& tip, const QString& objectName) {
    auto* b = new QPushButton(glyphIcon(g, 14), text, this);
    b->setToolTip(tip);
    b->setObjectName(objectName);
    b->setIconSize(QSize(14, 14));
    actions_->addWidget(b);
    evenButtons();
    return b;
}

}  // namespace iw8r::qt
