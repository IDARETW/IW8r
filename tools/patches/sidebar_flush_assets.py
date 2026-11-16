import io

# Side bar revamp, step 1 (owner 2026-11-15: "each upper section sidebar menu needs their own individual revamp. they
# need to have more flush menus and actually useful UIs"): the asset panels get the shared flush layout of
# panel_chrome.h (header line with the count, favourites star and dots menu; search across the panel; filter chips;
# the list edge to edge; a details strip with the selection's actions). Effects: a folder tree by path, kind dots,
# loop / one-shot facts per row, the square preview and Place fx_origin in the strip. Sounds: a play button and the
# length / variants on every row, category chips and a Loops chip, the waveform with a playhead and Place speaker in
# the strip. Images: a thumbnail grid with type chips, the picture, size and format in the strip.

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


# ------------------------------------------------------------------------------------------------ header
rw('qtapp/asset_browser.h', [
('''class QPushButton;
namespace iw8r {''', '''class QPushButton;
class QToolButton;
class QMenu;
class QTreeWidget;
namespace iw8r {'''),
('''    // A Tools button in the options row whose menu holds the Tools menu commands of this kind of asset.
    void setToolActions(const QList<QAction*>& actions);''', '''    // The Tools menu commands of this kind of asset, at the end of the panel's dots menu.
    void setToolActions(const QList<QAction*>& actions);
    QList<QAction*> toolActions() const { return toolActions_; }
    // Short facts at the right of a row and in the details strip ("loop", "1.5 s", "0.4-1.2 s · x3"), by name; `keep`
    // stores them in the settings (effects: known once previewed).
    void setRowFact(const QString& name, const QString& fact, bool keep = false);
    void setRowFacts(const QHash<QString, QString>& facts);
    QString rowFact(const QString& name) const { return facts_.value(name); }
    // Sounds: the waveform of what plays (peak per bucket, 0-1), its length; the strip draws it with a playhead.
    void setSoundWave(const QVector<float>& peaks, double seconds, bool loop);
    // The filter chips: the exclusive chip ("all" or a group id) and the chip ids (tests).
    void setChip(const QString& id);
    QString chip() const;
    QStringList chipIds() const;
    // The category of a name in this panel's chips ("" for Materials / Models), the same rule the chips filter by.
    QString groupOf(const QString& name) const;
    // Effects: the folder the list is narrowed to ("" = all), as the folder tree selects it.
    void setFolder(const QString& folder);
    QString folder() const { return folder_; }
    // The header line's count ("37 of 1,020") and the details strip's title / facts (tests).
    QString countText() const;
    QString stripTitle() const;
    QString stripFacts() const;
    bool stripShown() const;'''),
('''    void soundPlayRequested(const QString& name);  // Sounds: Play (or Enter on the Play button)
    void soundStopRequested();''', '''    void soundPlayRequested(const QString& name);  // Sounds: Play (or Enter on the Play button)
    void soundStopRequested();
    void firstShown();  // the panel was shown for the first time (sounds: the bank index is read then)'''),
('''    EditorContext* ctx_;
    Kind kind_;
    QLineEdit* search_;
    QCheckBox* usedOnly_;
    QComboBox* listMode_ = nullptr;
    QComboBox* kit_ = nullptr;
    QHash<QString, QStringList> kitNames_;  // kit -> names of this browser's kind
    QString settingsKey(const char* what) const;
    void applyListMode();
    void refillListModes();
    QCheckBox* buildableOnly_ = nullptr;  // hide assets the availability catalog lists as missing
    int hiddenMissing_ = 0;''', '''    EditorContext* ctx_;
    Kind kind_;
    QLineEdit* search_;
    bool usedOnly_ = false;          // "In this map" chip
    int listMode_ = 0;               // 0 all, 1 favourites, 2 recent, 3 the tag listTag_
    QString listTag_;
    QComboBox* kit_ = nullptr;
    QHash<QString, QStringList> kitNames_;  // kit -> names of this browser's kind
    QString settingsKey(const char* what) const;
    void applyListMode();
    void refillListModes();
    void setListModeTo(int mode, const QString& tag);
    void rebuildMenu();               // the dots menu: show modes, tags, options, the kind's tools
    void updateStrip();               // the details strip of the current row
    void fillFolders();               // effects: the folder tree from the names
    void playRow(const QModelIndex& idx);
    bool buildableOnly_ = true;       // hide assets the availability catalog lists as missing
    int hiddenMissing_ = 0;
    class PanelHeader* header_ = nullptr;
    class ChipBar* chips_ = nullptr;
    class DetailsStrip* strip_ = nullptr;
    QToolButton* favButton_ = nullptr;
    QMenu* menu_ = nullptr;
    QList<QAction*> toolActions_;
    QHash<QString, QString> facts_;   // row facts (setRowFact)
    QTreeWidget* folders_ = nullptr;  // effects: folders by path
    QString folder_;
    QWidget* wave_ = nullptr;         // sounds: WaveView
    QString playingName_;             // sounds: the alias asked to play (its row shows Stop)
    bool shownOnce_ = false;'''),
('''    QPushButton* soundPlay_ = nullptr;          // sounds: play / stop the selected alias
    QLabel* soundCaption_ = nullptr;
    QWidget* toolRow_ = nullptr;''', '''    QPushButton* soundPlay_ = nullptr;          // sounds: play / stop the selected alias
    QLabel* soundCaption_ = nullptr;'''),
])

# ------------------------------------------------------------------------------------------------ includes, roles, groups
rw('qtapp/asset_browser.cpp', [
('''#include "brush_tools.h"
#include "vfx_preview.h"''', '''#include "brush_tools.h"
#include "panel_chrome.h"
#include "vfx_preview.h"
#include <QActionGroup>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTreeWidget>
#include <cmath>'''),
('''namespace {

// Asset list items: tiles''', '''namespace {

// Item data of a row: its chip group (groupsOf), and an audible distance (sounds).
constexpr int kGroupRole = Qt::UserRole + 5;
constexpr int kDistRole = Qt::UserRole + 6;

// The filter chips of a panel: an id, the label, what it is, a pattern on the lower-case name (the first group whose
// pattern matches; a group without one takes the rest) and, for effects, the dot colour of its rows.
struct Group {
    const char* id;
    const char* label;
    const char* tip;
    const char* pattern;
    QColor color;
};

const std::vector<Group>& groupsOf(AssetBrowser::Kind kind) {
    static const std::vector<Group> none;
    // Effects: what the effect is, from the words of its path (a fire in any folder is under Fire).
    static const std::vector<Group> effects = {
        {"muzzle", "Muzzle flashes", "Weapon muzzle flashes", "muzflash|muzzle", QColor(240, 176, 48)},
        {"expl", "Explosions", "Explosions, grenades, missiles", "expl|_exp(_|$)|blast|grenade|airstrike|bomb|missile|rocket", QColor(224, 80, 64)},
        {"fire", "Fire", "Fire, flames, embers", "fire|flame|burn|ember|molotov|thermite|napalm", QColor(255, 128, 48)},
        {"smoke", "Smoke & dust", "Smoke, steam, fog, dust", "smk|smoke|steam|fog|mist|dust|haze|vapor|cloud", QColor(160, 164, 172)},
        {"spark", "Sparks", "Sparks and electricity", "spark|elec|zap", QColor(245, 224, 80)},
        {"light", "Lights & markers", "Glows, lamps, flares, beams, objective markers", "light|glow|lamp|flare|beam|laser|strobe|signal|marker|beacon|flagbase|chev", QColor(232, 232, 176)},
        {"impact", "Impacts & damage", "Impacts, debris, glass, tracers, damage states", "impact|hit|debris|glass|tread|bullet|trail|tracer|damage|dmg|leak|dest", QColor(176, 128, 96)},
        {"shell", "Shells", "Shell casings", "/shell/|shell_|casing|eject", QColor(200, 160, 80)},
        {"blood", "Blood", "Blood and executions", "blood|flesh|gore|dismember|exec", QColor(176, 48, 64)},
        {"other", "Other", "Everything else", nullptr, QColor(80, 192, 176)},
    };
    // Sounds: the alias's family, from its first word.
    static const std::vector<Group> sounds = {
        {"amb", "Ambience", "Ambient beds and emitters (amb_, emt_)", "^(amb|emt)_|ambient|wind|rain", {}},
        {"mus", "Music", "Music (mus_)", "^mus_", {}},
        {"expl", "Explosions", "Explosions", "^(exp|expl|explo|grenade|frag)_|explosion", {}},
        {"veh", "Vehicles", "Vehicles", "^(veh|lbravo|apache|heli|tank|jeep|atv|cargo|truck|boat)", {}},
        {"weap", "Weapons", "Weapon fire, reloads, shells, bullets", "^(weap|wex|wfoly|shell|mag|bullet|bulletspray|sniper|ar|smg|lm|pi|sh|case|attach|gun)_", {}},
        {"move", "Movement", "Footsteps, mantles, foley", "^(mvmt|step|mantle|mount|plr|foley|move|slide|gesture|jump|land|prone|sprint)_", {}},
        {"melee", "Melee", "Melee (melee_)", "^melee_", {}},
        {"ui", "UI", "Menu and HUD sounds", "^(ui|hud|menu)_", {}},
        {"other", "Other", "Everything else", nullptr, {}},
    };
    // Images: where the image is used.
    static const std::vector<Group> images = {
        {"world", "World", "World and prop images", nullptr, {}},
        {"sky", "Sky", "Skies", "sky", {}},
        {"weapons", "Weapons", "Weapons, attachments, cosmetics", "^(weapon|attachment|cosmetic|wpn|wm|vm)_", {}},
        {"chars", "Characters", "Characters' bodies, heads, gear", "^(body|head|hands|viewhands|char|fullbody|mp_(eastern|western)|russian_army)", {}},
        {"veh", "Vehicles", "Vehicles", "^veh", {}},
        {"vfx", "Effects", "Effect sprites", "^(vfx|fx)_", {}},
        {"ui", "UI & icons", "Menus, HUD, icons, reticles", "^(ui|icon|hud|compass|minimap|loadscreen|emblem|callsign|reticle|playercard)_|_reticle", {}},
        {"engine", "Engine", "Engine images ($ and * names)", "^[$*]", {}},
    };
    return kind == AssetBrowser::Kind::Effects ? effects : kind == AssetBrowser::Kind::Sounds ? sounds
           : kind == AssetBrowser::Kind::Images ? images : none;
}

// The group of a lower-case name: the first patterned group that matches, else the group without a pattern. The
// expressions are made by the caller (one set per thread).
QString groupOfName(const std::vector<Group>& groups, const std::vector<QRegularExpression>& res, const QString& lower) {
    QString rest;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (!groups[i].pattern) {
            rest = groups[i].id;
            continue;
        }
        if (res[i].match(lower).hasMatch()) return groups[i].id;
    }
    return rest;
}

std::vector<QRegularExpression> groupExpressions(const std::vector<Group>& groups) {
    std::vector<QRegularExpression> out;
    for (const Group& g : groups) out.emplace_back(g.pattern ? QString(g.pattern) : QString());
    return out;
}

QString groupLabel(AssetBrowser::Kind kind, const QString& id) {
    for (const Group& g : groupsOf(kind))
        if (id == g.id) return g.label;
    return {};
}

QColor groupColor(AssetBrowser::Kind kind, const QString& id) {
    for (const Group& g : groupsOf(kind))
        if (id == g.id) return g.color;
    return {};
}

QString thousands(int n) { return QLocale().toString(n); }

// Sounds: the waveform of what plays, the played part bright, a playhead while it plays.
class WaveView : public QWidget {
public:
    explicit WaveView(QWidget* parent) : QWidget(parent) {
        setObjectName("soundWave");
        setFixedHeight(44);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        timer_ = new QTimer(this);
        timer_->setInterval(33);
        connect(timer_, &QTimer::timeout, this, [this] { update(); });
    }
    void setWave(const QVector<float>& peaks, double seconds, bool loop) {
        peaks_ = peaks;
        seconds_ = seconds;
        loop_ = loop;
        clock_.start();
        setPlaying(true);
    }
    void setPlaying(bool on) {
        playing_ = on;
        if (on) timer_->start();
        else timer_->stop();
        update();
    }
    void clear() {
        peaks_.clear();
        setPlaying(false);
    }
    bool hasWave() const { return !peaks_.isEmpty(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), themeMix(0.08));
        if (peaks_.isEmpty()) {
            p.setPen(themeMix(0.45));
            p.drawText(rect(), Qt::AlignCenter, "Play to see the waveform");
            return;
        }
        const int w = width(), h = height(), mid = h / 2;
        double t = playing_ && seconds_ > 0 ? double(clock_.elapsed()) / 1000.0 : -1;
        if (t >= 0 && loop_) t = std::fmod(t, seconds_);
        if (t > seconds_) {
            t = -1;
            timer_->stop();
        }
        const int head = t >= 0 ? int(t / seconds_ * w) : -1;
        const QColor bright = palette().color(QPalette::Highlight), dim = themeMix(0.4);
        for (int x = 0; x < w; ++x) {
            const int i = std::min(int(peaks_.size()) - 1, int(qint64(x) * peaks_.size() / std::max(1, w)));
            const int a = std::max(1, int(peaks_[i] * float(h / 2 - 3)));
            p.setPen(head < 0 || x <= head ? bright : dim);
            p.drawLine(x, mid - a, x, mid + a);
        }
        if (head >= 0) {
            p.setPen(palette().color(QPalette::WindowText));
            p.drawLine(head, 0, head, h);
        }
        QFont f = font();
        f.setPointSizeF(std::max(6.5, f.pointSizeF() - 1.5));
        p.setFont(f);
        p.setPen(themeMix(0.75));
        p.drawText(rect().adjusted(4, 2, -4, -2), Qt::AlignRight | Qt::AlignBottom,
                   QString("%1 s%2").arg(seconds_, 0, 'f', seconds_ < 10 ? 1 : 0).arg(loop_ ? ", loop" : ""));
    }

private:
    QVector<float> peaks_;
    double seconds_ = 0;
    bool loop_ = false, playing_ = false;
    QElapsedTimer clock_;
    QTimer* timer_;
};

// Asset list items: tiles'''),
])

# ------------------------------------------------------------------------------------------------ delegate
rw('qtapp/asset_browser.cpp', [
('''    enum Style { Rows, MaterialTiles, ModelTiles };
    AssetTileDelegate(Style style, QObject* parent) : QStyledItemDelegate(parent), style_(style) {}''', '''    enum Style { Rows, MaterialTiles, ModelTiles, ImageTiles, EffectRows, SoundRows };
    AssetTileDelegate(Style style, QObject* parent) : QStyledItemDelegate(parent), style_(style) {}
    // Rows: the facts of a name (pills at the right), the effect folder the list shows (folders are drawn relative to
    // it), the alias that plays (its row shows Stop), the kind for dot colours, and the play button's action.
    const QHash<QString, QString>* facts = nullptr;
    QString folderBase, playing;
    AssetBrowser::Kind kind = AssetBrowser::Kind::Materials;
    std::function<void(const QModelIndex&)> onPlay;
    static QRect playRect(const QRect& r) {
        const int s = 18;
        return QRect(r.x() + 5, r.y() + (r.height() - s) / 2, s, s);
    }
    bool editorEvent(QEvent* e, QAbstractItemModel* m, const QStyleOptionViewItem& opt, const QModelIndex& idx) override {
        // A click on a sound row's play button plays (or stops) it; the row is selected as by any click.
        if (style_ == SoundRows && onPlay && e->type() == QEvent::MouseButtonRelease) {
            const auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton && playRect(opt.rect).contains(me->position().toPoint())) {
                onPlay(idx);
                return true;
            }
        }
        return QStyledItemDelegate::editorEvent(e, m, opt, idx);
    }
    void paintRow(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
        p->save();
        const QRect r = opt.rect;
        const bool sel = opt.state & QStyle::State_Selected;
        if (sel) p->fillRect(r, opt.palette.highlight());
        else if (opt.state & QStyle::State_MouseOver) p->fillRect(r, themeMix(0.07));
        const QString name = idx.data(Qt::DisplayRole).toString();
        QColor text = opt.palette.text().color();
        if (const QVariant fg = idx.data(Qt::ForegroundRole); fg.canConvert<QBrush>()) text = fg.value<QBrush>().color();
        if (sel) text = opt.palette.highlightedText().color();
        const QColor dim = sel ? text : themeMix(0.55);
        const QFontMetrics fm(opt.font);
        QFont small = opt.font;
        small.setPointSizeF(std::max(6.5, small.pointSizeF() - 1.5));
        const QFontMetrics sfm(small);
        p->setRenderHint(QPainter::Antialiasing);
        int x = r.x() + 6;
        const int lineH = fm.height(), y1 = style_ == EffectRows ? r.y() + 3 : r.y() + (r.height() - lineH) / 2;
        if (style_ == SoundRows) {
            const QRect play = playRect(r);
            const bool now = !playing.isEmpty() && playing == name;
            const QColor ring = now ? (sel ? text : opt.palette.highlight().color()) : dim;
            p->setPen(QPen(ring, 1.2));
            p->setBrush(now ? QColor(ring.red(), ring.green(), ring.blue(), 50) : QColor(Qt::transparent));
            p->drawEllipse(QRectF(play).adjusted(0.5, 0.5, -0.5, -0.5));
            QFont gf = glyphFont().isEmpty() ? opt.font : QFont(glyphFont());
            gf.setPixelSize(8);
            p->setFont(gf);
            p->setPen(now ? ring : text);
            p->drawText(play, Qt::AlignCenter, glyphFont().isEmpty() ? QString(now ? "x" : ">") : QString(QChar(now ? glyph::Stop : glyph::Play)));
            p->setFont(opt.font);
            x = play.right() + 7;
        } else {
            const QColor dot = groupColor(kind, idx.data(kGroupRole).toString());
            p->setPen(Qt::NoPen);
            p->setBrush(dot.isValid() ? dot : dim);
            p->drawEllipse(QRectF(r.x() + 7, y1 + (lineH - 8) / 2.0, 8, 8));
            x = r.x() + 22;
        }
        // Facts as pills on the right ("loop" in blue first).
        QStringList pills;
        if (idx.data(Qt::UserRole + 2).toBool()) pills << "loop";
        if (facts)
            for (const QString& f : facts->value(name).split(factSep(), Qt::SkipEmptyParts))
                if (!pills.contains(f)) pills << f;
        int right = r.right() - 6;
        p->setFont(small);
        for (int i = int(pills.size()) - 1; i >= 0; --i) {
            const int w = sfm.horizontalAdvance(pills[i]) + 10;
            if (right - w < x + 60) break;
            const QRectF pr(right - w, y1 + (lineH - sfm.height() - 2) / 2.0, w, sfm.height() + 2);
            const bool loop = pills[i] == "loop";
            const QColor c = loop && !sel ? QColor(120, 200, 255) : dim;
            p->setPen(c);
            p->setBrush(loop ? QColor(c.red(), c.green(), c.blue(), 40) : QColor(Qt::transparent));
            p->drawRoundedRect(pr, pr.height() / 2, pr.height() / 2);
            p->drawText(pr, Qt::AlignCenter, pills[i]);
            right -= w + 4;
        }
        p->setFont(opt.font);
        p->setPen(text);
        QString shown = name, folder;
        if (style_ == EffectRows) {
            shown = name.section('/', -1);
            folder = name.section('/', 0, -2);
            if (!folderBase.isEmpty() && folder == folderBase) folder.clear();
            else if (!folderBase.isEmpty() && folder.startsWith(folderBase + "/")) folder = folder.mid(folderBase.size() + 1);
            else if (folder.startsWith("vfx/")) folder = folder.mid(4);
        }
        p->drawText(QRect(x, y1, right - x, lineH), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(shown, Qt::ElideMiddle, right - x));
        if (!folder.isEmpty()) {
            p->setFont(small);
            p->setPen(dim);
            const int w = r.right() - 6 - x;
            p->drawText(QRect(x, y1 + lineH, w, sfm.height() + 2), Qt::AlignLeft | Qt::AlignVCenter, sfm.elidedText(folder, Qt::ElideMiddle, w));
        }
        p->restore();
    }'''),
('''    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        if (style_ == Rows) {''', '''    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        if (style_ == EffectRows || style_ == SoundRows) return paintRow(p, opt, idx);
        if (style_ == Rows) {'''),
('''        const QString leaf = name.section('/', -1), folder = name.contains('/') ? name.section('/', 0, -2) : QString();''',
 '''        // Images: the name up to its packed partner ("a&b~hash" shows "a"); the tooltip has the whole name.
        const QString leaf = style_ == ImageTiles ? name.section('~', 0, 0).section('&', 0, 0) : name.section('/', -1);
        const QString folder = style_ != ImageTiles && name.contains('/') ? name.section('/', 0, -2) : QString();'''),
('''        if (style_ == ModelTiles) return QSize(std::max(minTileW(), cellW_ - 4), tileH());
        if (style_ != Rows) return QSize(minTileW(), tileH());''', '''        if (style_ == ModelTiles) return QSize(std::max(minTileW(), cellW_ - 4), tileH());
        if (style_ == EffectRows || style_ == SoundRows) {
            // Edge to edge: the row as wide as the view (effects: the name, the folder under it).
            const QFontMetrics fm(opt.font);
            const int h = style_ == EffectRows ? 2 * fm.height() + 6 : std::max(24, fm.height() + 8);
            const auto* v = qobject_cast<const QAbstractItemView*>(opt.widget);
            return QSize(v ? std::max(60, v->viewport()->width() - 2) : 200, h);
        }
        if (style_ != Rows) return QSize(minTileW(), tileH());'''),
])

# ------------------------------------------------------------------------------------------------ the filter
rw('qtapp/asset_browser.cpp', [
('''    std::set<QString> kit;   // names of the chosen kit
    bool useKit = false;
    void refresh() { invalidateFilter(); }''', '''    std::set<QString> kit;   // names of the chosen kit
    bool useKit = false;
    QString group;           // the chip ("" = all)
    QString folder;          // effects: names under this folder ("" = all)
    bool loopsOnly = false;  // sounds: the Loops chip
    void refresh() { invalidateFilter(); }'''),
('''        if (useKit && !kit.count(name)) return false;
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);''', '''        if (useKit && !kit.count(name)) return false;
        const QModelIndex src = sourceModel()->index(row, 0, parent);
        if (!group.isEmpty() && src.data(kGroupRole).toString() != group) return false;
        if (!folder.isEmpty() && !name.startsWith(folder + "/")) return false;
        if (loopsOnly && !src.data(Qt::UserRole + 2).toBool()) return false;
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);'''),
])
print('ok part 1')
