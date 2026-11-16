import io

# Side bar revamp, step 1, part 3: the asset browser's list modes, strips, folders, facts (see sidebar_flush_assets.py).

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


P = 'qtapp/asset_browser.cpp'
rw(P, [
('''#include <QTreeWidget>
#include <cmath>''', '''#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QLocale>
#include <QMap>
#include <cmath>'''),
# context menu: play a sound, show an effect's folder
(r'''            if (kind_ == Kind::Models) menu.addAction("Preview in 3D...", this, [this, name] { emit previewRequested(name); });''',
 r'''            if (kind_ == Kind::Models) menu.addAction("Preview in 3D...", this, [this, name] { emit previewRequested(name); });
            if (kind_ == Kind::Sounds)
                menu.addAction(playingName_ == name && soundPlay_ && soundPlay_->property("playing").toBool() ? "Stop" : "Play", this,
                               [this, idx] { playRow(idx); });
            if (kind_ == Kind::Effects && folders_)
                menu.addAction("Show Its Folder", this, [this, name] { setFolder(name.section('/', 0, -2)); });'''),
# selection -> details strip
(r'''            if (soundPlay_) soundPlay_->setEnabled(idx.isValid() || soundPlay_->property("playing").toBool());
            if (kind_ == Kind::Effects && idx.isValid()) emit effectHighlighted(idx.data(Qt::DisplayRole).toString());''',
 r'''            if (soundPlay_) soundPlay_->setEnabled(idx.isValid() || soundPlay_->property("playing").toBool());
            updateStrip();
            if (kind_ == Kind::Effects && idx.isValid()) emit effectHighlighted(idx.data(Qt::DisplayRole).toString());'''),
(r'''            if (!idx.isValid()) {
                if (previewBox_) previewBox_->setVisible(false);  // models: the preview shows only while one is selected
                return;
            }
            if (kind_ == Kind::Images) emit imageHighlighted(idx.data(Qt::DisplayRole).toString());''',
 r'''            if (!idx.isValid()) {
                if (previewBox_) previewBox_->setVisible(false);  // models: the preview shows only while one is selected
                updateStrip();
                return;
            }
            updateStrip();  // before the preview: the main window may set it right away
            if (kind_ == Kind::Images) emit imageHighlighted(idx.data(Qt::DisplayRole).toString());'''),
(r'''    connect(use, &QPushButton::clicked, this, [this, choose] { choose(view_->currentIndex()); });''',
 r'''    if (use) connect(use, &QPushButton::clicked, this, [this, choose] { choose(view_->currentIndex()); });
    if (place) connect(place, &QPushButton::clicked, this, [this, choose] { choose(view_->currentIndex()); });'''),
# the catalogue: groups and distances
(r'''        int tone = 0;  // 0 normal, 1 builds grey (no colour image), 2 not available in a build, 3 a looping sound
        bool loop = false;''', r'''        int tone = 0;  // 0 normal, 1 builds grey (no colour image), 2 not available in a build, 3 a looping sound
        bool loop = false;
        QString group;  // the chip group (groupsOf)
        int dist = 0;   // sounds: audible to (units)'''),
(r'''                row.loop = s[1] == "1";''', r'''                row.loop = s[1] == "1";
                row.dist = s[2].toInt();'''),
(r'''    // Colour images from ACTS exports (data/stock_assets/<zone>/index.json).''',
 r'''    // The chip group of every row (here, on the worker thread, with this thread's expressions).
    if (const auto& groups = groupsOf(kind); !groups.empty()) {
        const std::vector<QRegularExpression> res = groupExpressions(groups);
        for (Catalog::Row& r : c->rows) r.group = groupOfName(groups, res, r.name.toLower());
    }
    // Colour images from ACTS exports (data/stock_assets/<zone>/index.json).'''),
(r'''        if (kind_ == Kind::Sounds) item->setData(r.loop, Qt::UserRole + 2);''',
 r'''        if (kind_ == Kind::Sounds) item->setData(r.loop, Qt::UserRole + 2);
        if (!r.group.isEmpty()) item->setData(r.group, kGroupRole);
        if (r.dist > 0) item->setData(r.dist, kDistRole);'''),
(r'''        else if (r.tone == 3) item->setForeground(QColor(120, 200, 255));
        items.append(item);''', r'''        // tone 3 (a looping sound): the row's "loop" pill says it
        items.append(item);'''),
(r'''    for (auto it = c.imageFiles.constBegin(); it != c.imageFiles.constEnd(); ++it) imageFiles_.insert(it.key(), it.value());''',
 r'''    for (auto it = c.imageFiles.constBegin(); it != c.imageFiles.constEnd(); ++it) imageFiles_.insert(it.key(), it.value());
    // The chips' counts and the effect folders.
    if (chips_ && !groupsOf(kind_).empty()) {
        QHash<QString, int> n;
        for (const Catalog::Row& r : c.rows) ++n[r.group];
        chips_->setCount("all", int(c.rows.size()));
        for (const Group& g : groupsOf(kind_)) chips_->setCount(g.id, n.value(g.id));
    }
    if (kind_ == Kind::Sounds && chips_ && chips_->chip("loops")) {
        int loops = 0;
        for (const Catalog::Row& r : c.rows) loops += r.loop;
        chips_->chip("loops")->setText(QString("Loops  %1").arg(thousands(loops)));
    }
    fillFolders();'''),
(r'''                                : kind_ == Kind::Sounds ? "sound aliases (blue = loop)" : "models")''',
 r'''                                : kind_ == Kind::Sounds ? "sound aliases" : "models")'''),
# load
(r'''    const bool buildable = buildableOnly_ && buildableOnly_->isChecked();''', r'''    const bool buildable = buildableOnly_;'''),
(r'''    if (model_->rowCount() == 0) info_->setText("Reading the catalogue...");''',
 r'''    if (model_->rowCount() == 0) {
        info_->setText("Reading the catalogue...");
        info_->setVisible(true);
    }'''),
# the count line
(r'''void AssetBrowser::updateInfo() {
    const int shown = proxy_->rowCount(), total = model_->rowCount();
    if (shown == total) {
        info_->setText(catalogInfo_);
        return;
    }
    // What narrows the list, so an empty list says why it is empty.
    QStringList by;
    if (!filterText_.isEmpty()) by << "\"" + filterText_ + "\"";
    if (listMode_ && listMode_->currentIndex() != 0) by << listMode_->currentText();
    if (kit_ && kit_->currentIndex() > 0) by << "kit " + kit_->currentText();
    if (usedOnly_->isChecked()) by << "in this map";
    const QString why = by.isEmpty() ? QString() : " (" + by.join(", ") + ")";
    info_->setText(shown == 0 ? QString("No matches for %1 among %2").arg(by.isEmpty() ? QString("the filters") : by.join(", ")).arg(total)
                              : QString("%1 of %2 shown%3").arg(shown).arg(total).arg(why));
}''', r'''void AssetBrowser::updateInfo() {
    const int shown = proxy_->rowCount(), total = model_->rowCount();
    // What narrows the list: the count's tooltip says it, and an empty list says why it is empty.
    QStringList by;
    if (!filterText_.isEmpty()) by << "\"" + filterText_ + "\"";
    if (listMode_ == 1) by << "favourites";
    else if (listMode_ == 2) by << "recent";
    else if (listMode_ == 3) by << "#" + listTag_;
    if (chips_ && !chips_->current().isEmpty() && chips_->current() != "all") by << groupLabel(kind_, chips_->current());
    if (!folder_.isEmpty()) by << "in " + folder_;
    if (chips_ && chips_->isOn("loops")) by << "loops";
    if (kit_ && kit_->currentIndex() > 0) by << "kit " + kit_->currentText();
    if (usedOnly_) by << "in this map";
    if (header_)
        header_->setCount(shown == total ? thousands(total) : QString("%1 of %2").arg(thousands(shown), thousands(total)),
                          catalogInfo_ + (by.isEmpty() ? QString() : "\nNarrowed by " + by.join(", ")));
    if (total == 0 && !catalogLoaded()) {
        info_->setText("Reading the catalogue...");
        info_->setVisible(true);
        return;
    }
    const bool empty = shown == 0 && total > 0;
    info_->setText(empty ? QString("No matches for %1 among %2").arg(by.isEmpty() ? QString("the filters") : by.join(", "), thousands(total))
                         : QString());
    info_->setVisible(empty);
}'''),
# list modes
(r'''void AssetBrowser::refillListModes() {
    if (!listMode_) return;
    const QString cur = listMode_->currentText();
    QSignalBlocker block(listMode_);
    while (listMode_->count() > 3) listMode_->removeItem(3);
    for (const QString& t : allTags()) listMode_->addItem("#" + t);
    const int i = listMode_->findText(cur);
    listMode_->setCurrentIndex(i >= 0 ? i : 0);
    applyListMode();
}

void AssetBrowser::showTag(const QString& tag) {
    refillListModes();
    const int i = listMode_ ? listMode_->findText("#" + tag.toLower()) : -1;
    if (i >= 0) listMode_->setCurrentIndex(i);
    applyListMode();
}''', r'''void AssetBrowser::refillListModes() {
    // A tag no asset has any more lists everything again.
    if (listMode_ == 3 && !allTags().contains(listTag_)) {
        listMode_ = 0;
        listTag_.clear();
    }
    setListModeTo(listMode_, listTag_);
}

void AssetBrowser::showTag(const QString& tag) {
    if (allTags().contains(tag.toLower())) setListModeTo(3, tag.toLower());
}

void AssetBrowser::setListModeTo(int mode, const QString& tag) {
    listMode_ = mode;
    listTag_ = mode == 3 ? tag : QString();
    // The star and the Recent chip show the mode; the dots menu is made again after the action that chose it.
    if (favButton_) {
        const QSignalBlocker block(favButton_);
        favButton_->setChecked(mode == 1);
        const char16_t g = mode == 1 ? glyph::StarFilled : glyph::Star;
        favButton_->setProperty("glyph", int(g));
        favButton_->setIcon(glyphIcon(g, 14));
    }
    if (chips_) chips_->setOn("recent", mode == 2);
    QMetaObject::invokeMethod(this, [this] { rebuildMenu(); }, Qt::QueuedConnection);
    applyListMode();
}

void AssetBrowser::rebuildMenu() {
    if (!menu_) return;
    menu_->clear();
    menu_->setToolTipsVisible(true);
    auto* show = new QActionGroup(menu_);
    const auto mode = [this, show](QMenu* in, const QString& text, int m, const QString& tag) {
        QAction* a = in->addAction(text);
        a->setCheckable(true);
        a->setChecked(listMode_ == m && (m != 3 || listTag_ == tag));
        show->addAction(a);
        connect(a, &QAction::triggered, this, [this, m, tag] { setListModeTo(m, tag); });
    };
    menu_->addSection("Show");
    mode(menu_, "All", 0, {});
    mode(menu_, "Favourites", 1, {});
    mode(menu_, "Recent", 2, {});
    if (const QStringList tags = allTags(); !tags.isEmpty()) {
        QMenu* tm = menu_->addMenu("Tags");
        for (const QString& t : tags) mode(tm, "#" + t, 3, t);
    }
    if (kind_ != Kind::Sounds) {
        menu_->addSeparator();
        QAction* b = menu_->addAction("Buildable Only");
        b->setCheckable(true);
        b->setChecked(buildableOnly_);
        b->setToolTip("Hide stock assets the availability audit could not export from Replay");
        connect(b, &QAction::toggled, this, [this](bool on) {
            buildableOnly_ = on;
            load();
        });
    }
    if (!toolActions_.isEmpty()) {
        menu_->addSection("Tools");
        for (QAction* a : toolActions_) menu_->addAction(a);
    }
}'''),
(r'''    if (listMode_ && listMode_->currentIndex() == 2) applyListMode();''', r'''    if (listMode_ == 2) applyListMode();'''),
(r'''void AssetBrowser::setListMode(ListMode m) { listMode_->setCurrentIndex(int(m)); applyListMode(); }

void AssetBrowser::applyListMode() {
    auto* filter = static_cast<NameFilter*>(proxy_);
    const int mode = listMode_ ? listMode_->currentIndex() : 0;
    filter->useList = mode != 0;
    filter->list.clear();
    QStringList names = mode == 1 ? Settings::instance().value(settingsKey("favourites")).toStringList() : mode == 2 ? recent() : QStringList();
    if (mode >= 3) {  // a tag
        const QString tag = listMode_->currentText().mid(1);
        const QVariantMap all = Settings::instance().value(settingsKey("tags")).toMap();
        for (auto it = all.begin(); it != all.end(); ++it)
            if (it.value().toStringList().contains(tag)) names << it.key();
    }''', r'''void AssetBrowser::setListMode(ListMode m) { setListModeTo(int(m), {}); }

void AssetBrowser::applyListMode() {
    auto* filter = static_cast<NameFilter*>(proxy_);
    const int mode = listMode_;
    filter->useList = mode != 0;
    filter->list.clear();
    QStringList names = mode == 1 ? Settings::instance().value(settingsKey("favourites")).toStringList() : mode == 2 ? recent() : QStringList();
    if (mode == 3) {  // a tag
        const QVariantMap all = Settings::instance().value(settingsKey("tags")).toMap();
        for (auto it = all.begin(); it != all.end(); ++it)
            if (it.value().toStringList().contains(listTag_)) names << it.key();
    }'''),
# images: the strip shows the caption
(r'''    QPixmap pm = image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    if (!pm.isNull() && (pm.width() > preview_->width() - 4 || pm.height() > preview_->height() - 4))
        pm = pm.scaled(preview_->size() - QSize(4, 4), Qt::KeepAspectRatio, Qt::SmoothTransformation);''',
 r'''    QPixmap pm = image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    // Images: the strip may not be laid out yet (shown with this preview), so the box is the panel's width.
    const QSize box = kind_ == Kind::Images ? QSize(std::max(96, width() - 16), 210) : preview_->size() - QSize(4, 4);
    if (!pm.isNull() && (pm.width() > box.width() || pm.height() > box.height()))
        pm = pm.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);'''),
(r'''        previewCaption_->setToolTip(caption);
        return;
    }
    previewCaption_->setText(caption);''', r'''        previewCaption_->setToolTip(caption);
        if (kind_ == Kind::Images && strip_) {
            // The strip: the name, then where it is used, its size and format (or why there is no picture).
            const QModelIndex cur = view_->currentIndex();
            QStringList facts{groupLabel(kind_, cur.data(kGroupRole).toString())};
            facts += lines.mid(1);
            facts.removeAll(QString());
            strip_->setTitle(lines.value(0), facts.join(factSep()));
            strip_->setVisible(cur.isValid());
        }
        return;
    }
    previewCaption_->setText(caption);'''),
# effects: the caption under the preview is the status; the strip's title is the effect
(r'''    if (def) fxPreview_->setEffect(*def);
    fxPreview_->setVisible(def != nullptr);
    fxCaption_->setText(caption);
    fxCaption_->setToolTip(caption);''', r'''    if (def) fxPreview_->setEffect(*def);
    fxPreview_->setVisible(def != nullptr);
    // The strip's title is the effect; this line says what the preview shows, or why there is none.
    const QString status = caption.section('\n', 1);
    fxCaption_->setText(status.isEmpty() ? caption : status);
    fxCaption_->setToolTip(caption);'''),
# sounds: Play / Stop with its icon, the row that plays
(r'''    soundPlay_->setProperty("playing", playing);
    soundPlay_->setText(playing ? "Stop" : "Play");
    soundPlay_->setEnabled(playing || view_->currentIndex().isValid());
    soundCaption_->setText(text);
    soundCaption_->setToolTip(text);''', r'''    soundPlay_->setProperty("playing", playing);
    soundPlay_->setText(playing ? "Stop" : "Play");
    soundPlay_->setIcon(glyphIcon(playing ? glyph::Stop : glyph::Play, 14));
    soundPlay_->setEnabled(playing || view_->currentIndex().isValid());
    soundCaption_->setText(text);
    soundCaption_->setToolTip(text);
    // The row of the alias that plays shows Stop; the waveform's playhead stops with it.
    static_cast<AssetTileDelegate*>(delegate_)->playing = playing ? playingName_ : QString();
    if (!playing && wave_) static_cast<WaveView*>(wave_)->setPlaying(false);
    view_->viewport()->update();'''),
# tools: in the dots menu
(r'''void AssetBrowser::setToolActions(const QList<QAction*>& actions) {
    // One "Tools" button in the options row (a narrow column has no room for a row of buttons): its menu holds the
    // Tools menu commands of this kind of asset, the same actions as the menu bar's.
    delete toolRow_;
    toolRow_ = nullptr;
    if (actions.isEmpty()) return;
    // A push button with a menu: its arrow sits on the right, centred, like the combo boxes beside it (a tool button
    // put a small arrow in its bottom-right corner; owner re-check 2026-11-15).
    auto* b = new QPushButton("Tools", this);
    b->setObjectName("assetTools");
    b->setToolTip("Tools for this kind of asset (the same commands as in the Tools menu and the Tools panel)");
    auto* menu = new QMenu(b);
    for (QAction* a : actions)
        if (a) menu->addAction(a);
    b->setMenu(menu);
    toolRow_ = b;
    if (QWidget* box = findChild<QWidget*>("assetOptions"); box && box->layout()) box->layout()->addWidget(b);
}''', r'''void AssetBrowser::setToolActions(const QList<QAction*>& actions) {
    // The Tools menu commands of this kind of asset (the same actions as the menu bar's), at the end of the dots menu.
    toolActions_ = actions;
    toolActions_.removeAll(nullptr);
    rebuildMenu();
}'''),
(r'''void AssetBrowser::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);''', r'''void AssetBrowser::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!shownOnce_) {
        shownOnce_ = true;
        emit firstShown();
    }'''),
(r'''void AssetBrowser::selectRow(int row) { view_->setCurrentIndex(proxy_->index(row, 0)); }
''', r'''void AssetBrowser::selectRow(int row) { view_->setCurrentIndex(proxy_->index(row, 0)); }

void AssetBrowser::updateStrip() {
    if (!strip_) return;
    const QModelIndex idx = view_->currentIndex();
    if (!idx.isValid()) {
        strip_->setVisible(false);
        return;
    }
    const QString name = idx.data(Qt::DisplayRole).toString();
    const QString group = groupLabel(kind_, idx.data(kGroupRole).toString());
    QStringList facts;
    if (kind_ == Kind::Effects) {
        if (!group.isEmpty()) facts << group;
        if (const QString f = facts_.value(name); !f.isEmpty()) facts << (f == "loop" ? QString("looping") : "one-shot " + f);
    } else if (kind_ == Kind::Sounds) {
        facts << (idx.data(Qt::UserRole + 2).toBool() ? "loop" : "one-shot");
        if (const QString f = facts_.value(name); !f.isEmpty()) facts += f.split(factSep(), Qt::SkipEmptyParts);
        if (const int d = idx.data(kDistRole).toInt(); d > 0) facts << QString("audible to %1 u").arg(thousands(d));
        if (!group.isEmpty()) facts << group;
        if (wave_ && playingName_ != name) static_cast<WaveView*>(wave_)->clear();  // the waveform is of what plays
    } else if (!group.isEmpty()) {
        facts << group;  // images: the size and format come with the preview (setPreview)
    }
    strip_->setTitle(name, facts.join(factSep()));
    strip_->setVisible(true);
}

void AssetBrowser::setRowFact(const QString& name, const QString& fact, bool keep) {
    if (facts_.value(name) == fact) return;
    if (fact.isEmpty()) facts_.remove(name);
    else facts_.insert(name, fact);
    if (keep) {
        QVariantMap kept = Settings::instance().value(settingsKey("facts")).toMap();
        if (fact.isEmpty()) kept.remove(name);
        else kept.insert(name, fact);
        Settings::instance().setValue(settingsKey("facts"), kept);
    }
    view_->viewport()->update();
    if (view_->currentIndex().data(Qt::DisplayRole).toString() == name) updateStrip();
}

void AssetBrowser::setRowFacts(const QHash<QString, QString>& facts) {
    for (auto it = facts.begin(); it != facts.end(); ++it) facts_.insert(it.key(), it.value());
    view_->viewport()->update();
    updateStrip();
}

void AssetBrowser::setSoundWave(const QVector<float>& peaks, double seconds, bool loop) {
    if (wave_) static_cast<WaveView*>(wave_)->setWave(peaks, seconds, loop);
}

void AssetBrowser::playRow(const QModelIndex& idx) {
    if (!idx.isValid()) return;
    const QString name = idx.data(Qt::DisplayRole).toString();
    if (view_->currentIndex() != idx) view_->setCurrentIndex(idx);
    if (soundPlay_ && soundPlay_->property("playing").toBool() && playingName_ == name) {
        emit soundStopRequested();
        return;
    }
    playingName_ = name;
    emit soundPlayRequested(name);
}

void AssetBrowser::fillFolders() {
    if (!folders_) return;
    const QSignalBlocker block(folders_);
    folders_->clear();
    // Every folder of every path with the number of effects under it (at any depth); "vfx" is the root.
    QMap<QString, int> count;
    for (int r = 0; r < model_->rowCount(); ++r) {
        const QStringList parts = model_->item(r)->text().split('/');
        QString path;
        for (int i = 0; i + 1 < int(parts.size()); ++i) {
            path += (i ? "/" : "") + parts[i];
            ++count[path];
        }
    }
    auto* all = new QTreeWidgetItem(folders_, {"All effects", thousands(model_->rowCount())});
    all->setData(0, Qt::UserRole, QString());
    all->setIcon(0, glyphIcon(glyph::Effects, 14));
    all->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    QHash<QString, QTreeWidgetItem*> items;
    for (auto it = count.begin(); it != count.end(); ++it) {  // sorted: a folder comes before the folders in it
        const QString& path = it.key();
        if (path == "vfx") continue;
        auto* item = new QTreeWidgetItem(items.value(path.section('/', 0, -2), all), {path.section('/', -1), thousands(it.value())});
        item->setData(0, Qt::UserRole, path);
        item->setToolTip(0, path);
        item->setIcon(0, glyphIcon(glyph::Folder, 14, themeMix(0.7)));
        item->setForeground(1, themeMix(0.5));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        items.insert(path, item);
    }
    all->setExpanded(true);
    folders_->setCurrentItem(items.value(folder_, all));
}

void AssetBrowser::setFolder(const QString& folder) {
    folder_ = folder == "vfx" ? QString() : folder;
    auto* f = static_cast<NameFilter*>(proxy_);
    f->folder = folder_;
    static_cast<AssetTileDelegate*>(delegate_)->folderBase = folder_;
    f->refresh();
    view_->viewport()->update();
    updateInfo();
    if (!folders_) return;
    // The tree follows (the context menu's Show Its Folder, tests).
    for (QTreeWidgetItemIterator it(folders_); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == folder_) {
            if (folders_->currentItem() != *it) {
                const QSignalBlocker block(folders_);
                folders_->setCurrentItem(*it);
                folders_->scrollToItem(*it);
            }
            break;
        }
}

void AssetBrowser::setChip(const QString& id) {
    if (!chips_) return;
    chips_->setCurrent(id);
    static_cast<NameFilter*>(proxy_)->group = id == "all" ? QString() : id;
    static_cast<NameFilter*>(proxy_)->refresh();
    updateInfo();
}

QString AssetBrowser::chip() const { return chips_ ? chips_->current() : QString(); }
QStringList AssetBrowser::chipIds() const { return chips_ ? chips_->chipIds() : QStringList(); }

QString AssetBrowser::groupOf(const QString& name) const {
    const auto& groups = groupsOf(kind_);
    if (groups.empty()) return {};
    const std::vector<QRegularExpression> res = groupExpressions(groups);
    return groupOfName(groups, res, name.toLower());
}

QString AssetBrowser::countText() const { return header_ ? header_->count() : QString(); }
QString AssetBrowser::stripTitle() const { return strip_ ? strip_->titleText() : QString(); }
QString AssetBrowser::stripFacts() const { return strip_ ? strip_->factsText() : QString(); }
bool AssetBrowser::stripShown() const { return strip_ && strip_->isVisible(); }
'''),
])
print('ok part 3')
