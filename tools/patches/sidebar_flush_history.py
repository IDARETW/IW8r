import io

# Side bar revamp: Undo History (owner 2026-11-15, "more flush menus and actually useful UIs"). The flush layout
# (panel_chrome.h): the header with the position ("9 of 12 applied") and Undo / Redo buttons, a search over the steps,
# rows with an icon for what kind of step it is and a line on what it touched ("2 entities added, 5 brushes changed"),
# the current step marked, undone steps dimmed; a click still goes to that step. The strip under the list: the step,
# what it touched, and Select What It Touched.

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


rw('qtapp/panels.h', [
(r'''class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(EditorContext* ctx, QWidget* parent = nullptr);
public slots:
    void refresh();
private:
    EditorContext* ctx_;
    QListWidget* list_;
    QLabel* position_;     // "Position: n of m steps applied." (wraps in a narrow column)
    bool filling_ = false;
    std::vector<std::string> shownNames_;  // the step names and undo position the list shows
    size_t shownPos_ = size_t(-1);
};''', r'''class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(EditorContext* ctx, QWidget* parent = nullptr);
    // What step `step` (1-based) touched, in words ("2 entities added, 5 brushes changed"); tests.
    QString summaryOf(size_t step) const;
    QString countText() const;   // the header's position line (tests)
    QString stripTitle() const;  // the step the strip shows (tests)
    void setFilter(const QString& text);
    int visibleSteps() const;    // rows the search keeps, the opened document included (tests)
public slots:
    void refresh();
private:
    void updateStrip();
    void applyFilter();
    EditorContext* ctx_;
    class PanelHeader* header_ = nullptr;
    QToolButton* undo_ = nullptr;
    QToolButton* redo_ = nullptr;
    QLineEdit* search_ = nullptr;
    QListWidget* list_;
    class DetailsStrip* strip_ = nullptr;
    QPushButton* selectTouched_ = nullptr;
    bool filling_ = false;
    std::vector<std::string> shownNames_;  // the step names and undo position the list shows
    size_t shownPos_ = size_t(-1);
};'''),
])

HISTORY = r'''// ------------------------------------------------------------------ history

namespace {

// What a step is, from the first word of its name: the row's icon.
char16_t historyGlyph(const QString& name) {
    const QString n = name.toLower();
    const auto has = [&n](std::initializer_list<const char*> words) {
        for (const char* w : words)
            if (n.startsWith(w) || n.contains(QString(" ") + w)) return true;
        return false;
    };
    if (has({"delete", "remove", "cut", "hide"})) return glyph::Delete;
    if (has({"place", "add", "create", "new ", "insert", "paste", "generate", "spawn"})) return glyph::Add;
    if (has({"clone", "duplicate", "copy"})) return glyph::Copy;
    if (has({"move", "drag", "nudge", "translate", "drop", "snap"})) return 0xE7C2;  // move arrows
    if (has({"rotate", "turn", "flip", "mirror"})) return 0xE7AD;                      // rotate
    if (has({"scale", "resize", "size"})) return 0xE740;                               // full screen corners
    if (has({"apply", "material", "texture", "paint", "fit", "align"})) return glyph::Brush;
    if (has({"set ", "edit", "key", "rename", "change"})) return glyph::Edit;
    if (has({"select"})) return 0xE762;                                                // select all
    return glyph::History;
}

// What a step touched, counted from its operations.
struct Touched {
    int entAdded = 0, entRemoved = 0, primAdded = 0, primRemoved = 0, primChanged = 0, keys = 0, layers = 0;
    std::set<SelRef> refs;  // the entities and primitives it touched (selectable while the step is applied)
};

Touched touchedBy(const Transaction& t) {
    Touched out;
    for (const Op& op : t.ops) {
        const EntityItem* item = op.after ? &*op.after : op.before ? &*op.before : nullptr;
        const bool prim = item && item->isPrimitive();
        switch (op.type) {
        case Op::Type::InsertEntity: ++out.entAdded; out.refs.insert({op.entity, kNoId}); break;
        case Op::Type::RemoveEntity: ++out.entRemoved; break;
        case Op::Type::InsertItem:
            if (prim) ++out.primAdded, out.refs.insert({op.entity, op.item});
            else ++out.keys, out.refs.insert({op.entity, kNoId});
            break;
        case Op::Type::RemoveItem:
            if (prim) ++out.primRemoved;
            else ++out.keys, out.refs.insert({op.entity, kNoId});
            break;
        case Op::Type::ReplaceItem:
            if (prim) ++out.primChanged, out.refs.insert({op.entity, op.item});
            else ++out.keys, out.refs.insert({op.entity, kNoId});
            break;
        case Op::Type::ReplaceHeader: ++out.layers; break;
        }
    }
    return out;
}

QString touchedText(const Touched& t) {
    QStringList parts;
    const auto n = [&parts](int count, const char* one, const char* many, const char* what) {
        if (count > 0) parts << QString("%1 %2 %3").arg(count).arg(count == 1 ? one : many, what);
    };
    n(t.entAdded, "entity", "entities", "added");
    n(t.entRemoved, "entity", "entities", "removed");
    n(t.primAdded, "brush", "brushes", "added");
    n(t.primRemoved, "brush", "brushes", "removed");
    n(t.primChanged, "brush", "brushes", "changed");
    n(t.keys, "key", "keys", "set");
    n(t.layers, "layer change", "layer changes", "");
    return parts.isEmpty() ? QString("no change to the map") : parts.join(", ").trimmed();
}

// Rows: an icon, the step's name, what it touched under it; the current step marked with a bar, undone ones dimmed.
class HistoryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const override {
        const QFontMetrics fm(opt.font);
        return {100, 2 * fm.height() + 8};
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        p->save();
        const QRect r = opt.rect;
        const bool sel = opt.state & QStyle::State_Selected;
        const bool current = idx.data(Qt::UserRole + 1).toBool(), undone = idx.data(Qt::UserRole + 2).toBool();
        if (sel) p->fillRect(r, themeMix(0.14));
        else if (opt.state & QStyle::State_MouseOver) p->fillRect(r, themeMix(0.07));
        if (current) p->fillRect(QRect(r.x(), r.y() + 2, 3, r.height() - 4), opt.palette.highlight());
        const QColor text = undone ? themeMix(0.45) : opt.palette.text().color(), dim = themeMix(undone ? 0.35 : 0.55);
        const QFontMetrics fm(opt.font);
        const QRect icon(r.x() + 9, r.y() + (r.height() - 16) / 2, 16, 16);
        idx.data(Qt::DecorationRole).value<QIcon>().paint(p, icon, Qt::AlignCenter, undone ? QIcon::Disabled : QIcon::Normal);
        const int x = icon.right() + 8;
        QFont small = opt.font;
        small.setPointSizeF(std::max(6.5, small.pointSizeF() - 1.5));
        const QFontMetrics sfm(small);
        // The step number on the right.
        const QString num = idx.data(Qt::UserRole + 3).toString();
        p->setFont(small);
        p->setPen(dim);
        const int numW = sfm.horizontalAdvance(num) + 2;
        p->drawText(QRect(r.right() - 6 - numW, r.y() + 3, numW, fm.height()), Qt::AlignRight | Qt::AlignVCenter, num);
        QFont name = opt.font;
        name.setBold(current);
        p->setFont(name);
        p->setPen(text);
        const int w = r.right() - 10 - numW - x;
        p->drawText(QRect(x, r.y() + 3, w, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(name).elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, w));
        p->setFont(small);
        p->setPen(dim);
        const int w2 = r.right() - 6 - x;
        p->drawText(QRect(x, r.y() + 3 + fm.height(), w2, sfm.height() + 2), Qt::AlignLeft | Qt::AlignVCenter,
                    sfm.elidedText(idx.data(Qt::UserRole + 4).toString(), Qt::ElideRight, w2));
        p->restore();
    }
};

}  // namespace

HistoryPanel::HistoryPanel(EditorContext* ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    setObjectName("historyPanel");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    header_ = new PanelHeader("Undo History", this);
    undo_ = header_->addButton(glyph::Undo, "Undo (Ctrl+Z)", false, "historyUndo");
    redo_ = header_->addButton(glyph::Redo, "Redo (Ctrl+Y)", false, "historyRedo");
    lay->addWidget(header_);
    connect(undo_, &QToolButton::clicked, this, [this] {
        if (ctx_->doc.undo()) ctx_->documentEdited();
    });
    connect(redo_, &QToolButton::clicked, this, [this] {
        if (ctx_->doc.redo()) ctx_->documentEdited();
    });
    auto* searchRow = new QVBoxLayout();
    searchRow->setContentsMargins(6, 0, 6, 0);
    search_ = new QLineEdit(this);
    search_->setObjectName("historySearch");
    search_->setPlaceholderText("Search steps");
    search_->setClearButtonEnabled(true);
    search_->addAction(glyphIcon(0xE721, 14, themeMix(0.55), "?"), QLineEdit::LeadingPosition);
    searchRow->addWidget(search_);
    lay->addLayout(searchRow);
    connect(search_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    // Narrow side column (300-420 px): never scroll sideways; long step names are elided (the full name is the tooltip).
    list_ = new QListWidget(this);
    list_->setObjectName("historyList");
    list_->setFrameShape(QFrame::NoFrame);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setUniformItemSizes(true);
    list_->setMouseTracking(true);
    list_->setItemDelegate(new HistoryDelegate(list_));
    list_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    lay->addWidget(list_, 1);
    strip_ = new DetailsStrip(this);
    selectTouched_ = strip_->addAction(glyph::Target, "Select what it touched",
                                       "Select the entities and brushes this step added or changed (the step must be applied)",
                                       "historySelectTouched");
    lay->addWidget(strip_);
    connect(selectTouched_, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        const auto& h = ctx_->doc.history();
        if (row < 1 || size_t(row) > h.size()) return;
        ctx_->doc.deselectAll();
        size_t n = 0;
        for (const SelRef& r : touchedBy(h[size_t(row) - 1]).refs) {
            const Entity* e = ctx_->doc.findEntity(r.entity);
            if (!e || (r.item != kNoId && !ctx_->doc.findItem(r.entity, r.item))) continue;  // gone since
            ctx_->doc.select(r, true);
            ++n;
        }
        ctx_->documentEdited(QString("Selected %1 object(s) the step \"%2\" touched").arg(n).arg(qs(h[size_t(row) - 1].name)));
    });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (filling_ || row < 0) return;
        const size_t target = size_t(row);
        while (ctx_->doc.undoPosition() > target && ctx_->doc.undo()) {}
        while (ctx_->doc.undoPosition() < target && ctx_->doc.redo()) {}
        ctx_->documentEdited();
    });
    connect(ctx_, &EditorContext::documentChanged, this, &HistoryPanel::refresh);
    refresh();
}

QString HistoryPanel::summaryOf(size_t step) const {
    const auto& h = ctx_->doc.history();
    return step >= 1 && step <= h.size() ? touchedText(touchedBy(h[step - 1])) : QString();
}

QString HistoryPanel::countText() const { return header_->count(); }
QString HistoryPanel::stripTitle() const { return strip_->titleText(); }

void HistoryPanel::setFilter(const QString& text) { search_->setText(text); }

int HistoryPanel::visibleSteps() const {
    int n = 0;
    for (int r = 0; r < list_->count(); ++r) n += !list_->item(r)->isHidden();
    return n;
}

void HistoryPanel::applyFilter() {
    const QStringList words = search_->text().toLower().split(' ', Qt::SkipEmptyParts);
    for (int r = 1; r < list_->count(); ++r) {
        QListWidgetItem* it = list_->item(r);
        const QString hay = (it->text() + " " + it->data(Qt::UserRole + 4).toString()).toLower();
        bool match = true;
        for (const QString& w : words) match = match && hay.contains(w);
        it->setHidden(!match);
    }
}

void HistoryPanel::updateStrip() {
    const auto& h = ctx_->doc.history();
    const size_t pos = ctx_->doc.undoPosition();
    if (pos == 0) {
        strip_->setTitle("Opened document", h.empty() ? QString("No steps yet") : QString("%1 step(s) undone").arg(h.size()));
        selectTouched_->setEnabled(false);
        return;
    }
    const Transaction& t = h[pos - 1];
    strip_->setTitle(qs(t.name), QString("Step %1 of %2").arg(pos).arg(h.size()) + factSep() + touchedText(touchedBy(t)));
    selectTouched_->setEnabled(!touchedBy(t).refs.empty());
}

void HistoryPanel::refresh() {
    const auto& h = ctx_->doc.history();
    const size_t pos = ctx_->doc.undoPosition();
    undo_->setEnabled(pos > 0);
    redo_->setEnabled(pos < h.size());
    {
        // documentChanged also comes for selection clicks: the list is rebuilt only when what it shows (the step names
        // and the undo position) changed, so a long history does not cost a rebuild per click.
        bool same = pos == shownPos_ && h.size() == shownNames_.size();
        for (size_t i = 0; same && i < h.size(); ++i) same = h[i].name == shownNames_[i];
        if (same) return;
        shownNames_.clear();
        for (const Transaction& t : h) shownNames_.push_back(t.name);
        shownPos_ = pos;
    }
    filling_ = true;
    list_->clear();
    // Row 0 is the opened document; row k is step k (applied when k <= pos, otherwise undone and dimmed).
    const auto add = [this](const QString& name, const QString& touched, char16_t g, bool current, bool undone, const QString& num) {
        auto* item = new QListWidgetItem(glyphIcon(g, 16), name);
        item->setToolTip(name + "\n" + touched + (undone ? "\n(undone: click to redo up to here)" : ""));
        item->setData(Qt::UserRole + 1, current);
        item->setData(Qt::UserRole + 2, undone);
        item->setData(Qt::UserRole + 3, num);
        item->setData(Qt::UserRole + 4, touched);
        list_->addItem(item);
    };
    add("Opened document", h.empty() ? QString("the map as it was opened") : QString("before step 1"), 0xE8A5, pos == 0, false, QString());
    for (size_t i = 0; i < h.size(); ++i)
        add(qs(h[i].name), touchedText(touchedBy(h[i])), historyGlyph(qs(h[i].name)), i + 1 == pos, i >= pos, QString::number(i + 1));
    list_->setCurrentRow(int(pos));
    list_->scrollToItem(list_->currentItem());
    filling_ = false;
    applyFilter();
    const size_t steps = h.size();
    header_->setCount(steps == 0 ? QString("no steps") : pos == steps ? QString("%1 step%2").arg(steps).arg(steps == 1 ? "" : "s")
                                                                     : QString("%1 of %2 applied").arg(pos).arg(steps),
                      steps > pos ? QString("%1 step(s) undone (dimmed): click one to redo up to it").arg(steps - pos) : QString());
    updateStrip();
}

'''
slice_replace('qtapp/panels.cpp', '// ------------------------------------------------------------------ history\n',
              '// ------------------------------------------------------------------ IW8 spawns\n', HISTORY)
print('ok')
