#include "command_palette.h"

#include <QAction>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPalette>
#include <QSet>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>

namespace iw8r::qt {

namespace {

QString clean(QString t) {
    t.remove('&');
    return t.replace("&&", "&").trimmed();
}

// Disabled commands stay in the list (greyed, not runnable): a search then shows the command exists, rather than
// suggesting it does not. A disabled submenu disables everything under it.
void collect(QMenu* menu, const QString& prefix, bool enabled, QList<PaletteEntry>& out) {
    for (QAction* a : menu->actions()) {
        if (a->isSeparator()) continue;
        const QString text = clean(a->text());
        if (QMenu* sub = a->menu()) {
            collect(sub, prefix + text + " > ", enabled && a->isEnabled(), out);
            continue;
        }
        if (text.isEmpty()) continue;
        out.push_back({prefix + text, a->shortcut().toString(QKeySequence::NativeText), a, enabled && a->isEnabled()});
    }
}

// Initials of the words of `text` ("Time of Day" -> "tod").
QString initials(const QString& text) {
    QString s;
    bool start = true;
    for (const QChar c : text) {
        if (c.isLetterOrNumber()) {
            if (start) s += c.toLower();
            start = false;
        } else {
            start = true;
        }
    }
    return s;
}

}  // namespace

QList<PaletteEntry> paletteEntries(QMenuBar* bar, const QWidget* commands) {
    QList<PaletteEntry> out;
    if (bar)
        for (QAction* top : bar->actions())
            if (QMenu* m = top->menu()) collect(m, clean(top->text()) + " > ", top->isEnabled(), out);
    if (commands) {
        // Commands registered without a menu (shortcut-only, e.g. Entity Inspector on N) are only actions of the window.
        QSet<const QAction*> listed;
        for (const PaletteEntry& e : out) listed.insert(e.action);
        for (QAction* a : commands->actions()) {
            if (a->isSeparator() || a->menu() || listed.contains(a)) continue;
            const QString text = clean(a->text());
            if (text.isEmpty()) continue;
            out.push_back({"Other > " + text, a->shortcut().toString(QKeySequence::NativeText), a, a->isEnabled()});
        }
    }
    return out;
}

int paletteScore(const QString& query, const QString& text) {
    const QString t = text.toLower();
    const QString init = initials(text);
    int score = 0;
    for (const QString& w : query.toLower().split(' ', Qt::SkipEmptyParts)) {
        const int at = int(t.indexOf(w));
        if (at >= 0) {
            score += 100 - std::min(at, 90);  // earlier is better
            const bool wordStart = at == 0 || !t[at - 1].isLetterOrNumber();
            if (wordStart) score += 40;
            // The command's own name (after the last '>') counts more than its menu.
            if (at > t.lastIndexOf('>')) score += 60;
        } else if (w.size() >= 2 && init.contains(w)) {
            score += 50;
        } else {
            return -1;
        }
    }
    return score - int(text.size()) / 8;  // shorter wins ties
}

CommandPalette::CommandPalette(QMenuBar* bar, QWidget* parent) : QDialog(parent, Qt::Popup), entries_(paletteEntries(bar, parent)) {
    setObjectName("commandPalette");
    setMinimumWidth(560);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText("Type a command (e.g. \"tod dusk\", \"compile\", \"normal maps\")");
    list_ = new QListWidget(this);
    list_->setMinimumHeight(320);
    lay->addWidget(edit_);
    lay->addWidget(list_);
    connect(edit_, &QLineEdit::textChanged, this, [this] { refill(); });
    connect(edit_, &QLineEdit::returnPressed, this, [this] { runFirst(); });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { runFirst(); });
    // Up / Down in the text field move through the list.
    class Keys : public QObject {
    public:
        Keys(QListWidget* l, QObject* p) : QObject(p), list(l) {}
        bool eventFilter(QObject*, QEvent* e) override {
            if (e->type() != QEvent::KeyPress) return false;
            const int k = static_cast<QKeyEvent*>(e)->key();
            if (k != Qt::Key_Down && k != Qt::Key_Up) return false;
            const int row = std::clamp(list->currentRow() + (k == Qt::Key_Down ? 1 : -1), 0, std::max(0, list->count() - 1));
            list->setCurrentRow(row);
            return true;
        }
        QListWidget* list;
    };
    edit_->installEventFilter(new Keys(list_, this));
    refill();
}

void CommandPalette::setQuery(const QString& q) { edit_->setText(q); }

void CommandPalette::refill() {
    list_->clear();
    const QString q = edit_->text().trimmed();
    struct Hit {
        int score;
        int index;
    };
    std::vector<Hit> hits;
    for (int i = 0; i < entries_.size(); ++i) {
        const int s = q.isEmpty() ? 0 : paletteScore(q, entries_[i].path);
        if (s >= 0) hits.push_back({s, i});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    int firstEnabled = -1;
    for (const Hit& h : hits) {
        const PaletteEntry& e = entries_[h.index];
        QString text = e.shortcut.isEmpty() ? e.path : e.path + "    [" + e.shortcut + "]";
        if (!e.enabled) text += "    (unavailable)";
        auto* item = new QListWidgetItem(text, list_);
        item->setData(Qt::UserRole, h.index);
        // Greyed but still selectable, so Up / Down move through it; runFirst does not run it.
        if (!e.enabled) {
            item->setForeground(list_->palette().color(QPalette::Disabled, QPalette::Text));
            item->setToolTip("This command is not available right now");
        } else if (firstEnabled < 0) {
            firstEnabled = list_->count() - 1;
        }
    }
    // Enter runs the best match that can run.
    if (list_->count()) list_->setCurrentRow(firstEnabled >= 0 ? firstEnabled : 0);
}

int CommandPalette::visibleCount() const { return list_->count(); }

QString CommandPalette::firstPath() const {
    QListWidgetItem* it = list_->currentItem() ? list_->currentItem() : list_->item(0);
    return it ? entries_[it->data(Qt::UserRole).toInt()].path : QString();
}

bool CommandPalette::runFirst() {
    QListWidgetItem* it = list_->currentItem() ? list_->currentItem() : list_->item(0);
    if (!it) return false;
    const PaletteEntry& e = entries_[it->data(Qt::UserRole).toInt()];
    // A greyed (unavailable) command is listed only; the palette stays open.
    if (!e.action || !e.enabled || !e.action->isEnabled()) return false;
    QAction* a = e.action;
    close();
    a->trigger();
    return true;
}

}  // namespace iw8r::qt
