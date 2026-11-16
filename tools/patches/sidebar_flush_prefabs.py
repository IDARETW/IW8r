import io

# Side bar revamp: Prefabs (owner 2026-11-15, the approved plan: "Category chips, place/drag, open to edit, recent").
# The flush layout (panel_chrome.h): the header with the count, Rescan and the dots menu; the search; category chips
# from the prefab's folder (Bundles, Doors, Windows, Stairs, Fences, Cover, Structure, Props, Lights, Foliage, Other)
# and a Recent chip (the last 30 placed); the tiles edge to edge; the strip with the picked prefab's category, folder
# and badge, and Place / 3D / Open (the prefab's map, to edit it) / Folder.

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


rw('qtapp/prefab_browser.h', [
('''class QLabel;
class QLineEdit;''', '''class QLabel;
class QLineEdit;
class QMenu;'''),
('''signals:
    void previewRequested(const QString& mapFile);  // context menu > Preview in 3D''', '''    // Categories (the chips): "bundles", "doors", "windows", "stairs", "fences", "cover", "structure", "props", "lights",
    // "foliage", "other"; "" = all. A plain prefab's category comes from its folder's name.
    void setCategory(const QString& category);
    QString category() const { return category_; }
    QString categoryAt(int row) const;
    void setRecentOnly(bool on);  // the Recent chip: the last 30 placed
    QStringList recent() const;
    QString stripTitle() const;   // the picked prefab in the strip (tests)
    QString stripFacts() const;
    bool stripShown() const;
    void selectRow(int row);

signals:
    void previewRequested(const QString& mapFile);  // context menu > Preview in 3D
    void openRequested(const QString& mapFile);     // the strip's Open: the prefab's map opened to edit it'''),
('''    QLabel* count_ = nullptr;  // "N prefabs", "N of M shown" or why the list is empty''', '''    QLabel* count_ = nullptr;  // "N prefabs", "N of M shown" or why the list is empty (shown only when empty)
    class PanelHeader* header_ = nullptr;
    class ChipBar* chips_ = nullptr;
    class DetailsStrip* strip_ = nullptr;
    QString category_;
    bool recentOnly_ = false;
    void noteRecent(const QString& file);
    void updateStrip();'''),
])

P = 'qtapp/prefab_browser.cpp'
rw(P, [
('''    kPeekMissed,               // the pack has no picture: drawn or made by the picture source
};''', '''    kPeekMissed,               // the pack has no picture: drawn or made by the picture source
    kCategory,                 // the chip's category (categoryOf)
};

// The chips' categories, and a plain prefab's category from the words of its folder (stock/window -> windows).
struct Category {
    const char* id;
    const char* label;
    const char* tip;
};
const Category kCategories[] = {
    {"bundles", "Bundles", "Prefab bundles: a prefab with its own models, materials and parameters"},
    {"doors", "Doors", "Doors and handles"},
    {"windows", "Windows", "Windows and glass"},
    {"stairs", "Stairs", "Stairs and ladders"},
    {"fences", "Fences", "Railings, fences, barriers"},
    {"cover", "Cover", "Cover pieces and sandbags"},
    {"structure", "Structure", "Frames, pipes, trims, walls, pillars, roofs"},
    {"props", "Props", "Crates, barrels, furniture, debris, signs, vehicles"},
    {"lights", "Lights", "Lamps"},
    {"foliage", "Foliage", "Plants"},
    {"other", "Other", "Everything else"},
};

QString categoryOfFolder(const QString& rel) {
    static const QHash<QString, QString> words = {
        {"door", "doors"}, {"doors", "doors"}, {"handle", "doors"}, {"window", "windows"}, {"windows", "windows"}, {"glass", "windows"},
        {"stair", "stairs"}, {"stairs", "stairs"}, {"ladder", "stairs"}, {"railing", "fences"}, {"fence", "fences"},
        {"railings_fences", "fences"}, {"barrier", "fences"}, {"cover", "cover"}, {"sandbag", "cover"}, {"frame", "structure"},
        {"pipe", "structure"}, {"trim", "structure"}, {"walls", "structure"}, {"wall", "structure"}, {"pillars_beams", "structure"},
        {"roofs", "structure"}, {"crate", "props"}, {"barrel", "props"}, {"furniture", "props"}, {"debris", "props"}, {"sign", "props"},
        {"props", "props"}, {"vehicle", "props"}, {"lamp", "lights"}, {"lamps", "lights"}, {"foliage", "foliage"}};
    for (const QString& part : rel.toLower().split('/', Qt::SkipEmptyParts))
        if (const auto it = words.constFind(part); it != words.constEnd()) return *it;
    return "other";
}

QString categoryLabel(const QString& id) {
    for (const Category& c : kCategories)
        if (id == c.id) return c.label;
    return {};
}'''),
('''#include "prefab_browser.h"''', '''#include "prefab_browser.h"
#include "panel_chrome.h"
#include "settings.h"'''),
# the top of the panel
('''    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* top = new QHBoxLayout();
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText("Filter prefabs (name, tag, category)");
    filter_->setClearButtonEnabled(true);
    auto* rescan = new QPushButton("Rescan", this);
    top->addWidget(filter_, 1);
    top->addWidget(rescan);
    lay->addLayout(top);''', '''    // Flush (owner 2026-11-15, panel_chrome.h): the header (count, Rescan, the dots menu), the search, the category chips
    // and Recent, the tiles edge to edge, the picked prefab's strip.
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    header_ = new PanelHeader("Prefabs", this);
    QToolButton* rescan = header_->addButton(glyph::Refresh, "Rescan the prefab folders", false, "prefabRescan");
    auto* menu = new QMenu(this);
    menu->addAction("Rescan the Prefab Folders", this, &PrefabBrowser::refresh);
    header_->addMenuButton(menu, "Prefab folders", "prefabTools");
    lay->addWidget(header_);
    auto* top = new QHBoxLayout();
    top->setContentsMargins(6, 0, 6, 0);
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText("Search prefabs (name, tag, category)");
    filter_->setClearButtonEnabled(true);
    filter_->addAction(glyphIcon(0xE721, 14, themeMix(0.55), "?"), QLineEdit::LeadingPosition);
    top->addWidget(filter_, 1);
    lay->addLayout(top);
    chips_ = new ChipBar(this);
    chips_->setObjectName("prefabChips");
    chips_->addChip("", "All", -1, "Every prefab");
    for (const Category& c : kCategories) chips_->addChip(c.id, c.label, 0, c.tip);
    chips_->addToggle("recent", "Recent", false, "The last 30 prefabs placed");
    lay->addWidget(chips_);
    connect(chips_, &ChipBar::currentChanged, this, [this](const QString& id) { setCategory(id); });
    connect(chips_, &ChipBar::toggled, this, [this](const QString& id, bool on) {
        if (id == "recent") setRecentOnly(on);
    });'''),
('''    list_ = new DragList(this);
    list_->setViewMode(QListView::IconMode);''', '''    list_ = new DragList(this);
    list_->setFrameShape(QFrame::NoFrame);  // edge to edge
    list_->setViewMode(QListView::IconMode);'''),
('''    count_ = new QLabel(this);
    count_->setObjectName("prefabCount");
    count_->setStyleSheet("color: gray");
    count_->setWordWrap(true);  // a long message wraps instead of widening the panel
    lay->addWidget(count_);''', '''    count_ = new QLabel(this);
    count_->setObjectName("prefabCount");
    count_->setStyleSheet(QString("color: %1").arg(themeMix(0.6).name()));
    count_->setWordWrap(true);  // a long message wraps instead of widening the panel
    count_->setContentsMargins(6, 0, 6, 0);
    count_->setVisible(false);  // the header has the count; this line says why the list is empty
    lay->addWidget(count_);
    // The picked prefab: its category, folder and badge; Place, 3D, Open (to edit it), Folder.
    strip_ = new DetailsStrip(this);
    QPushButton* place = strip_->addAction(glyph::Add, "Place", "Place it in front of the camera (misc_prefab, one undo step), or drag the tile into a view",
                                           "prefabPlace");
    QPushButton* view3d = strip_->addAction(glyph::Cube, "3D", "Preview in 3D", "prefabPreview");
    QPushButton* open = strip_->addAction(glyph::Edit, "Open", "Open the prefab's map to edit it (the open map is closed first)", "prefabOpen");
    QPushButton* folder = strip_->addAction(glyph::Folder, "Folder", "Show its folder", "prefabFolder");
    strip_->setVisible(false);
    lay->addWidget(strip_);
    connect(place, &QPushButton::clicked, this, [this] {
        QString err;
        if (!stamp(list_->currentRow(), &err) && !err.isEmpty()) QMessageBox::warning(this, "Insert Prefab", err);
    });
    connect(view3d, &QPushButton::clicked, this, [this] {
        if (const QString f = previewFileAt(list_->currentRow()); !f.isEmpty()) emit previewRequested(f);
    });
    connect(open, &QPushButton::clicked, this, [this] {
        if (const QString f = previewFileAt(list_->currentRow()); !f.isEmpty()) emit openRequested(f);
    });
    connect(folder, &QPushButton::clicked, this, [this] {
        const QFileInfo path(fileAt(list_->currentRow()));
        if (path.exists()) QDesktopServices::openUrl(QUrl::fromLocalFile(path.isDir() ? path.absoluteFilePath() : path.absolutePath()));
    });
    connect(list_, &QListWidget::currentRowChanged, this, [this] { updateStrip(); });'''),
# the category of every row
('''            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
            item->setSizeHint(QSize(kThumb + 24, kThumb + 40));
            if (!b.placeable) {''', '''            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
            item->setSizeHint(QSize(kThumb + 24, kThumb + 40));
            item->setData(kCategory, QString("bundles"));
            if (!b.placeable) {'''),
('''            item->setData(Qt::UserRole + 2, label);
            item->setToolTip(label + "\\n" + f);''', '''            item->setData(Qt::UserRole + 2, label);
            item->setData(kCategory, categoryOfFolder(rel));
            item->setToolTip(label + "\\n" + f);'''),
('''    setFilter(filter_->text());
    thumbTimer_->start();
    ensureDecoder();''', '''    {
        QHash<QString, int> n;
        for (int i = 0; i < list_->count(); ++i) ++n[list_->item(i)->data(kCategory).toString()];
        chips_->setCount("", list_->count());
        for (const Category& c : kCategories) chips_->setCount(c.id, n.value(c.id));
    }
    setFilter(filter_->text());
    thumbTimer_->start();
    ensureDecoder();'''),
# filter: words, the category, Recent
('''    const QStringList words = text.split(' ', Qt::SkipEmptyParts);
    int shown = 0, bundles = 0;
    for (int i = 0; i < list_->count(); ++i) {
        const QString hay = list_->item(i)->data(kSearch).toString();
        bool match = true;
        for (const QString& word : words) match = match && hay.contains(word, Qt::CaseInsensitive);''', '''    const QStringList words = text.split(' ', Qt::SkipEmptyParts);
    int shown = 0, bundles = 0;
    QSet<QString> recentFiles;
    if (recentOnly_)
        for (const QString& f : recent()) recentFiles.insert(QDir::cleanPath(f).toLower());
    for (int i = 0; i < list_->count(); ++i) {
        const QString hay = list_->item(i)->data(kSearch).toString();
        bool match = category_.isEmpty() || list_->item(i)->data(kCategory).toString() == category_;
        if (match && recentOnly_) match = recentFiles.contains(QDir::cleanPath(list_->item(i)->data(kPath).toString()).toLower());
        for (const QString& word : words) match = match && hay.contains(word, Qt::CaseInsensitive);'''),
('''    else if (shown == 0) count_->setText(QString("No prefabs match \\"%1\\" (of %2)").arg(text.simplified()).arg(total));
    else count_->setText(QString("%1 of %2 shown").arg(shown).arg(total));
}''', '''    else if (shown == 0) count_->setText(QString("No prefabs match \\"%1\\" (of %2)").arg(text.simplified()).arg(total));
    else count_->setText(QString("%1 of %2 shown").arg(shown).arg(total));
    count_->setVisible(total == 0 || shown == 0);
    if (header_)
        header_->setCount(shown == total ? QString::number(total) : QString("%1 of %2").arg(shown).arg(total),
                          QString("%1 prefabs, %2 of them bundles").arg(total).arg(bundles));
    updateStrip();
}

void PrefabBrowser::setCategory(const QString& category) {
    category_ = category;
    if (chips_ && chips_->current() != category) chips_->setCurrent(category);
    setFilter(filter_->text());
}

void PrefabBrowser::setRecentOnly(bool on) {
    recentOnly_ = on;
    if (chips_) chips_->setOn("recent", on);
    setFilter(filter_->text());
}

QString PrefabBrowser::categoryAt(int row) const {
    const QListWidgetItem* it = list_->item(row);
    return it ? it->data(kCategory).toString() : QString();
}

QStringList PrefabBrowser::recent() const { return Settings::instance().value("prefabs/recent").toStringList(); }

void PrefabBrowser::noteRecent(const QString& file) {
    QStringList r = recent();
    r.removeAll(file);
    r.prepend(file);
    while (r.size() > 30) r.removeLast();
    Settings::instance().setValue("prefabs/recent", r);
    if (recentOnly_) setFilter(filter_->text());
}

void PrefabBrowser::selectRow(int row) { list_->setCurrentRow(row); }

QString PrefabBrowser::stripTitle() const { return strip_ ? strip_->titleText() : QString(); }
QString PrefabBrowser::stripFacts() const { return strip_ ? strip_->factsText() : QString(); }
bool PrefabBrowser::stripShown() const { return strip_ && strip_->isVisible(); }

void PrefabBrowser::updateStrip() {
    if (!strip_) return;
    const int row = list_->currentRow();
    QListWidgetItem* it = row >= 0 ? list_->item(row) : nullptr;
    if (!it || it->isHidden()) {
        strip_->setVisible(false);
        return;
    }
    QStringList facts{categoryLabel(it->data(kCategory).toString())};
    if (it->data(kIsBundle).toBool()) facts << "bundle";
    else if (const QString label = it->data(Qt::UserRole + 2).toString(); label.contains(" / ")) facts << label.section(" / ", 0, 0);
    if (const QString badge = it->data(kBadge).toString(); !badge.isEmpty()) facts << badge.toLower() + ": " + it->data(kBadgeReason).toString();
    strip_->setTitle(it->text(), facts.join(factSep()));
    if (QPushButton* p = strip_->findChild<QPushButton*>("prefabPlace")) p->setEnabled(placeableAt(row));
    if (QPushButton* o = strip_->findChild<QPushButton*>("prefabOpen")) o->setEnabled(!previewFileAt(row).isEmpty());
    strip_->setVisible(true);
}'''),
('''    if (!placeableAt(row)) {
        if (err) *err = badgeReasonAt(row);
        return false;
    }
    return insert_(f, err);''', '''    if (!placeableAt(row)) {
        if (err) *err = badgeReasonAt(row);
        return false;
    }
    const bool ok = insert_(f, err);
    if (ok) noteRecent(f);
    return ok;'''),
])

rw('qtapp/main_window.cpp', [
('''        connect(prefabs_, &PrefabBrowser::previewRequested, this, [this](const QString& f) { openAssetPreview(f, true); });''',
 '''        connect(prefabs_, &PrefabBrowser::previewRequested, this, [this](const QString& f) { openAssetPreview(f, true); });
        connect(prefabs_, &PrefabBrowser::openRequested, this, [this](const QString& f) { openPath(f); });  // asks to save first'''),
])

rw('qtapp/qt_gui_test_sidebar.cpp', [
('''    section("side bar: flush asset panels");''', '''    section("side bar: Prefabs");
    if (PrefabBrowser* pb = (w.showDock("Prefabs"), QTest::qWait(50), w.prefabBrowser())) {
        if (pb->count() == 0) pb->refresh();
        pb->setCategory("doors");
        int doors = 0, other = 0;
        for (int r = 0; r < pb->count(); ++r)
            if (pb->shownAt(r)) (pb->categoryAt(r) == "doors" ? doors : other)++;
        int first = -1;
        for (int r = 0; r < pb->count() && first < 0; ++r)
            if (pb->shownAt(r)) first = r;
        pb->selectRow(first);
        QTest::qWait(30);
        const bool strip = pb->stripShown() && pb->stripTitle() == pb->titleAt(first) && pb->stripFacts().startsWith("Doors") &&
                           pb->findChild<QPushButton*>("prefabOpen") && pb->findChild<QPushButton*>("prefabPlace");
        pb->setCategory("");
        check(doors > 20 && other == 0 && strip,
              QString("the Prefabs chips list a category (Doors: %1, %2 others) and the strip shows the pick (\\"%3\\": %4)")
                  .arg(doors).arg(other).arg(pb->stripTitle(), pb->stripFacts()));
    }

    section("side bar: flush asset panels");'''),
('''#include "map_check_panel.h"
#include "panels.h"''', '''#include "map_check_panel.h"
#include "panels.h"
#include "prefab_browser.h"'''),
])
print('ok')
