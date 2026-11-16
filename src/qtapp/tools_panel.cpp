#include "tools_panel.h"

#include <QAction>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>


namespace iw8r::qt {

namespace {
QString shortLabel(const QAction* a) {
    QString t = a->text();
    t.remove('&');
    if (const int i = int(t.indexOf(" (")); i > 0) t = t.left(i);
    t.remove("...");
    return t.trimmed();
}
}  // namespace

ToolsPanel::ToolsPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("toolsPanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);
    search_ = new QLineEdit(this);
    search_->setObjectName("toolsSearch");
    search_->setPlaceholderText("Search tools");
    search_->setToolTip("The Tools menu's commands, in groups; the same commands are in Tools in the menu bar");
    search_->setClearButtonEnabled(true);
    lay->addWidget(search_);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // narrow column: rows wrap, never scroll sideways
    auto* host = new QWidget(scroll);
    list_ = new QVBoxLayout(host);
    list_->setContentsMargins(0, 0, 0, 0);
    list_->setSpacing(2);
    list_->addStretch(1);
    scroll->setWidget(host);
    lay->addWidget(scroll, 1);
    connect(search_, &QLineEdit::textChanged, this, &ToolsPanel::setFilter);
}

void ToolsPanel::clear() {
    for (const auto& g : groups_) delete g.second;
    groups_.clear();
    rows_.clear();
}

void ToolsPanel::addGroup(const QString& title, const QList<Tool>& tools) {
    auto* group = new QWidget(this);
    group->setObjectName("toolsGroup_" + QString(title).replace(' ', '_'));
    auto* g = new QVBoxLayout(group);
    g->setContentsMargins(0, 6, 0, 2);
    g->setSpacing(1);
    auto* head = new QLabel(title, group);
    QFont f = head->font();
    f.setBold(true);
    head->setFont(f);
    head->setWordWrap(true);  // wraps in a narrow column instead of widening it
    g->addWidget(head);
    for (const Tool& t : tools) {
        if (!t.action) continue;
        auto* row = new QWidget(group);
        auto* r = new QVBoxLayout(row);
        r->setContentsMargins(8, 1, 0, 3);
        r->setSpacing(0);
        auto* b = new QToolButton(row);
        b->setObjectName("tool_" + t.action->objectName());
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setAutoRaise(true);
        const QString label = t.label.isEmpty() ? shortLabel(t.action) : t.label;
        // The button runs the menu's own action (checkable ones mirror its state); its own text stays short.
        b->setText(label);
        b->setCheckable(t.action->isCheckable());
        b->setChecked(t.action->isChecked());
        b->setToolTip(t.action->text().remove('&') +
                      (t.action->shortcut().isEmpty() ? QString() : " (" + t.action->shortcut().toString(QKeySequence::NativeText) + ")"));
        QAction* a = t.action;
        connect(b, &QToolButton::clicked, a, [a, b] {
            if (a->isCheckable()) b->setChecked(a->isChecked());  // the action decides (it may refuse or ask first)
            a->trigger();
        });
        connect(a, &QAction::changed, b, [a, b] {
            b->setCheckable(a->isCheckable());
            b->setChecked(a->isChecked());
            b->setEnabled(a->isEnabled());
        });
        connect(a, &QAction::toggled, b, [b](bool on) { b->setChecked(on); });
        auto* d = new QLabel(t.description, row);
        d->setWordWrap(true);
        d->setStyleSheet("color: gray");
        d->setMinimumWidth(0);
        d->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        d->setContentsMargins(6, 0, 0, 0);
        // A long label is cut at the column's edge rather than widening it (no sideways scrolling).
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        b->setMinimumWidth(0);
        r->addWidget(b, 0, Qt::AlignLeft);
        if (!t.description.isEmpty()) r->addWidget(d);
        else delete d;
        g->addWidget(row);
        rows_.push_back({row, (label + " " + t.description + " " + title + " " + t.action->text()).toLower(), group});
    }
    list_->insertWidget(list_->count() - 1, group);  // above the stretch
    groups_.push_back({title, group});
    setFilter(search_->text());
}

void ToolsPanel::setFilter(const QString& text) {
    const QStringList words = text.toLower().split(' ', Qt::SkipEmptyParts);
    QHash<QWidget*, int> shown;
    for (const Row& r : rows_) {
        bool match = true;
        for (const QString& w : words) match = match && r.hay.contains(w);
        r.widget->setVisible(match);
        shown[r.group] += match;
    }
    for (const auto& g : groups_) g.second->setVisible(shown.value(g.second) > 0);
}

int ToolsPanel::toolCount() const { return int(rows_.size()); }

int ToolsPanel::visibleToolCount() const {
    int n = 0;
    for (const Row& r : rows_) n += !r.widget->isHidden();
    return n;
}

QStringList ToolsPanel::groups() const {
    QStringList out;
    for (const auto& g : groups_) out << g.first;
    return out;
}

}  // namespace iw8r::qt
