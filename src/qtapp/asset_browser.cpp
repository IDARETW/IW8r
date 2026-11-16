#include "settings.h"
#include "asset_browser.h"

#include <QSplitter>
#include <QFrame>
#include <QShowEvent>

#include "flow_layout.h"
#include <QVariantMap>
#include <QSignalBlocker>
#include <QInputDialog>

#include <QComboBox>
#include <QDesktopServices>
#include <QDrag>
#include <QKeyEvent>
#include <QMimeData>
#include <QGuiApplication>
#include <QClipboard>
#include <QMenu>
#include <QColor>
#include <QPointer>
#include <QUrl>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QHelpEvent>
#include <QListView>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QPushButton>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>
#include <memory>
#include <set>
#include <thread>

#include "brush_tools.h"
#include "map_queries.h"
#include "menu_pack.h"
#include "panel_chrome.h"
#include "vfx_preview.h"
#include <QActionGroup>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QLocale>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <QAction>
#include <QToolButton>
#include "worker_guard.h"
#include "texture_decode.h"

namespace iw8r::qt {

namespace {

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
        {"muzzle", "Muzzle", "Weapon muzzle flashes", "muzflash|muzzle", QColor(240, 176, 48)},
        {"expl", "Explosions", "Explosions, grenades, missiles", "expl|_exp(_|$)|blast|grenade|airstrike|bomb|missile|rocket", QColor(224, 80, 64)},
        {"fire", "Fire", "Fire, flames, embers", "fire|flame|burn|ember|molotov|thermite|napalm", QColor(255, 128, 48)},
        {"smoke", "Smoke", "Smoke, steam, fog, dust", "smk|smoke|steam|fog|mist|dust|haze|vapor|cloud", QColor(160, 164, 172)},
        {"spark", "Sparks", "Sparks and electricity", "spark|elec|zap", QColor(245, 224, 80)},
        {"light", "Lights", "Glows, lamps, flares, beams, objective markers", "light|glow|lamp|flare|beam|laser|strobe|signal|marker|beacon|flagbase|chev", QColor(232, 232, 176)},
        {"impact", "Impacts", "Impacts, debris, glass, tracers, damage states", "impact|hit|debris|glass|tread|bullet|trail|tracer|damage|dmg|leak|dest", QColor(176, 128, 96)},
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
        {"ui", "UI", "Menus, HUD, icons, reticles", "^(ui|icon|hud|compass|minimap|loadscreen|emblem|callsign|reticle|playercard)_|_reticle", {}},
        {"engine", "Engine", "Engine images ($ and * names)", "^[$*]", {}},
    };
    // Materials: what the surface is, from the words of its name; the mapper's own first ("own" is set on their rows).
    static const std::vector<Group> materials = {
        {"own", "Own", "The map's own materials", "^$", {}},
        {"ground", "Ground", "Dirt, grass, gravel, rock, asphalt, floors",
         "ground|dirt|grass|gravel|mud|sand|soil|rock|asphalt|road|terrain|moss|snow|floor|pavement|cobble", {}},
        {"walls", "Walls", "Brick, plaster, concrete, tiles, stone", "wall|brick|plaster|concrete|tile|stucco|siding|block|stone", {}},
        {"metal", "Metal", "Metal, steel, rust, corrugated sheets", "metal|steel|iron|rust|alum|corrugated|chrome", {}},
        {"wood", "Wood", "Wood, planks, bark", "wood|plank|plywood|timber|bark", {}},
        {"glass", "Glass", "Glass and windows", "glass|window", {}},
        {"fabric", "Fabric", "Fabric, carpet, leather", "fabric|cloth|carpet|curtain|leather|rug", {}},
        {"decals", "Decals", "Decals, reveal layers, grime, signs", "decal|_dec($|_)|^i/|_rvl|leak|grime|stain|graffiti|poster|sign", {}},
        {"other", "Other", "Everything else (terrain blends, weapons, characters, effects)", nullptr, {}},
    };
    // Models: what the prop is.
    static const std::vector<Group> models = {
        {"structure", "Structure", "Walls, doors, windows, stairs, fences, pipes, frames",
         "wall|door|window|stair|pillar|column|beam|fence|roof|frame|pipe|railing|rail_|scaffold|girder|gate|ladder|bridge|building|floor", {}},
        {"containers", "Containers", "Crates, boxes, barrels, pallets, bins", "crate|box|barrel|container|pallet|bin_|dumpster|case_|sack|bag", {}},
        {"furniture", "Furniture", "Chairs, tables, shelves, beds, lockers",
         "chair|table|desk|sofa|couch|bed_|shelf|cabinet|furniture|bench|stool|dresser|wardrobe|locker|bookcase", {}},
        {"household", "Household", "Small props: kitchen, books, bottles, decor",
         "decor|kitchen|dinnerware|accessory|school|book|cup|plate|bottle|can_|food|toy|clock|vase|towel", {}},
        {"foliage", "Foliage", "Trees, bushes, grass, plants", "foliage|tree|bush|grass|plant|fern|shrub|ivy|flower|hedge|leaf|leaves|vine|weed|cactus|palm", {}},
        {"veh", "Vehicles", "Cars, trucks, helicopters, boats", "^veh|vehicle|car_|_car|truck|heli|boat|bike|tractor|bus_|plane|train", {}},
        {"equipment", "Equipment", "Machinery, military and industrial gear", "equipment|machinery|industrial|military|pump|ac_unit|tank", {}},
        {"lights", "Lights", "Lamps and light fixtures", "light|lamp|lantern|bulb", {}},
        {"electronics", "Electronics", "Screens, computers, cables, speakers",
         "tv_|computer|monitor|phone|radio|electronic|cable|wire|server|speaker|generator|antenna", {}},
        {"debris", "Debris", "Rubble, trash, rocks, tires", "debris|rubble|trash|garbage|junk|litter|rock|stone|brick|plank|tire|cinder", {}},
        {"signs", "Signs", "Signs, posters, flags", "sign|poster|banner|flag|billboard", {}},
        {"other", "Other", "Everything else", nullptr, {}},
    };
    return kind == AssetBrowser::Kind::Effects ? effects : kind == AssetBrowser::Kind::Sounds ? sounds
           : kind == AssetBrowser::Kind::Images ? images : kind == AssetBrowser::Kind::Materials ? materials : models;
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

// Asset list items: tiles (materials: thumbnail fitted in its box, short name, folder in grey; models: a larger picture
// and the name on up to two lines) or rows (icon + name, elided in the middle). The tooltip is the full name.
class AssetTileDelegate : public QStyledItemDelegate {
public:
    enum Style { Rows, MaterialTiles, ModelTiles, ImageTiles, EffectRows, SoundRows };
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
            p->drawRect(pr.adjusted(0.5, 0.5, -0.5, -0.5));  // square, as the chips (owner 2026-11-15)
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
    }
    Style style() const { return style_; }
    // Models: the preset picture side (the tile is at least that wide) and the most it may grow to fill a row.
    void setModelSize(int icon, int maxIcon) {
        preset_ = icon;
        maxIcon_ = maxIcon;
        icon_ = icon;
    }
    int maxIcon() const { return style_ == ModelTiles ? maxIcon_ : kIcon; }
    int minTileW() const { return style_ == ModelTiles ? preset_ + 16 : kTileW; }
    int tileH() const {
        if (style_ != ModelTiles) return kTileH;
        return 4 + icon_ + 4 + 2 * QFontMetrics(QApplication::font()).height() + 6;
    }
    // A grid cell of this width: the picture grows (up to maxIcon) to use the row.
    void fitCell(int cellW) {
        cellW_ = cellW;
        if (style_ == ModelTiles) icon_ = std::clamp(cellW - 14, preset_, maxIcon_);
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        if (style_ == EffectRows || style_ == SoundRows) return paintRow(p, opt, idx);
        if (style_ == Rows) {
            QStyleOptionViewItem o = opt;
            o.textElideMode = Qt::ElideMiddle;
            QStyledItemDelegate::paint(p, o, idx);
            return;
        }
        p->save();
        const QRect r = opt.rect.adjusted(2, 2, -2, -2);
        if (opt.state & QStyle::State_Selected) p->fillRect(r, opt.palette.highlight());
        else if (opt.state & QStyle::State_MouseOver) p->fillRect(r, opt.palette.alternateBase());
        const int side = style_ == ModelTiles ? icon_ : kIcon;
        const QSize ic(side, side);  // fixed box: a thumbnail arriving later does not move the text
        const QRect iconBox(r.x() + (r.width() - ic.width()) / 2, r.y() + 3, ic.width(), ic.height());
        const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull()) icon.paint(p, iconBox, Qt::AlignCenter);  // scaled to fit, aspect kept
        else if (style_ == ModelTiles) p->fillRect(iconBox, QColor(32, 34, 38));  // the box while its picture loads
        const QString name = idx.data(Qt::DisplayRole).toString();
        const QFontMetrics fm(opt.font);
        QColor textColor = opt.palette.text().color();
        if (const QVariant fg = idx.data(Qt::ForegroundRole); fg.canConvert<QBrush>()) textColor = fg.value<QBrush>().color();
        if (opt.state & QStyle::State_Selected) textColor = opt.palette.highlightedText().color();
        p->setPen(textColor);
        if (style_ == ModelTiles) {
            // The name on two lines: as much of the start as fits (broken after an underscore when one is near), then
            // the rest elided in the middle, so both ends of a long name show. The tooltip has the whole name.
            const int w = r.width() - 6;
            const QRect line1(r.x() + 3, iconBox.bottom() + 4, w, fm.height());
            const QRect line2(line1.x(), line1.bottom() + 1, w, fm.height());
            if (fm.horizontalAdvance(name) <= w) {
                p->drawText(line1, Qt::AlignHCenter | Qt::AlignVCenter, name);
            } else {
                int cut = 1;
                while (cut < name.size() && fm.horizontalAdvance(name.left(cut + 1)) <= w) ++cut;
                if (const int us = int(name.left(cut).lastIndexOf('_')); us > cut * 2 / 3) cut = us + 1;
                p->drawText(line1, Qt::AlignHCenter | Qt::AlignVCenter, name.left(cut));
                p->drawText(line2, Qt::AlignHCenter | Qt::AlignVCenter, fm.elidedText(name.mid(cut), Qt::ElideMiddle, w));
            }
            p->restore();
            return;
        }
        // Images: the name up to its packed partner ("a&b~hash" shows "a"); the tooltip has the whole name.
        const QString leaf = style_ == ImageTiles ? name.section('~', 0, 0).section('&', 0, 0) : name.section('/', -1);
        const QString folder = style_ != ImageTiles && name.contains('/') ? name.section('/', 0, -2) : QString();
        const QRect line1(r.x() + 3, iconBox.bottom() + 3, r.width() - 6, fm.height());
        p->drawText(line1, Qt::AlignHCenter | Qt::AlignVCenter, fm.elidedText(leaf, Qt::ElideMiddle, line1.width()));
        if (!folder.isEmpty()) {
            QFont small = opt.font;
            small.setPointSizeF(std::max(6.0, small.pointSizeF() - 1));
            p->setFont(small);
            p->setPen(QColor(140, 145, 155));
            const QRect line2(line1.x(), line1.bottom() + 1, line1.width(), QFontMetrics(small).height());
            p->drawText(line2, Qt::AlignHCenter | Qt::AlignVCenter, QFontMetrics(small).elidedText(folder, Qt::ElideMiddle, line2.width()));
        }
        p->restore();
    }
    // Every tile is the same size (the grid cell), whatever its name or whether its thumbnail has loaded: a varying
    // size hint made the view re-flow (tiles per row changing) while scrolling.
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        // Model tiles fill their grid cell (the picture grows with it, so a narrower item would cut it off and leave
        // gaps between tiles that clicks fall through).
        if (style_ == ModelTiles) return QSize(std::max(minTileW(), cellW_ - 4), tileH());
        if (style_ == EffectRows || style_ == SoundRows) {
            // Edge to edge: the row as wide as the view (effects: the name, the folder under it).
            const QFontMetrics fm(opt.font);
            const int h = style_ == EffectRows ? 2 * fm.height() + 6 : std::max(24, fm.height() + 8);
            const auto* v = qobject_cast<const QAbstractItemView*>(opt.widget);
            return QSize(v ? std::max(60, v->viewport()->width() - 2) : 200, h);
        }
        if (style_ != Rows) return QSize(minTileW(), tileH());
        // List rows no wider than the view: a long name is elided in the middle instead of scrolling the list sideways.
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        if (const auto* v = qobject_cast<const QAbstractItemView*>(opt.widget)) s.setWidth(std::min(s.width(), std::max(60, v->viewport()->width() - 4)));
        return s;
    }
    // Narrow column: 124 px material tiles (3 per row at about 400 px, 2 at 300); the grid widens each row to fill the view.
    static constexpr int kTileW = 124, kTileH = 104, kIcon = 64;
    bool helpEvent(QHelpEvent* e, QAbstractItemView* view, const QStyleOptionViewItem& opt, const QModelIndex& idx) override {
        if (e && e->type() == QEvent::ToolTip && idx.isValid()) {
            const QString tip = idx.data(Qt::ToolTipRole).toString();
            QToolTip::showText(e->globalPos(), tip.isEmpty() ? idx.data(Qt::DisplayRole).toString() : tip, view);
            return true;
        }
        return QStyledItemDelegate::helpEvent(e, view, opt, idx);
    }

private:
    Style style_;
    int preset_ = 120, maxIcon_ = 156, icon_ = 120, cellW_ = 0;
};

// Tiles: as many columns as fit the view at the delegate's tile width, each column widened to fill the row, so the grid
// never leaves an empty strip on the right and the columns follow the panel width (models: the pictures grow with it).
class TileGridFit : public QObject {
public:
    TileGridFit(QListView* view, AssetTileDelegate* delegate) : QObject(view), view_(view), delegate_(delegate) {}
    void fit() {
        // A few pixels of slack: cells adding up to the full viewport width wrapped the last one (the list keeps a
        // small margin), so three columns showed as two.
        const int w = view_->viewport()->width() - 6;
        if (w <= 0) return;
        const int cols = std::max(1, w / delegate_->minTileW());
        delegate_->fitCell(w / cols);
        const QSize grid(w / cols, delegate_->tileH() + 4);
        if (view_->gridSize() != grid) {
            view_->setGridSize(grid);
            view_->doItemsLayout();
        }
    }

protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        if (e->type() == QEvent::Resize) fit();
        return QObject::eventFilter(obj, e);
    }

private:
    QListView* view_;
    AssetTileDelegate* delegate_;
};

// A row's own image file (a custom material's PNG / TGA / JPG, an exported stock colour DDS), fitted to `side`. Any thread.
QImage decodeImageFile(const QString& file, int side) {
    if (file.isEmpty()) return {};
    if (!file.endsWith(".dds", Qt::CaseInsensitive)) return ThumbDecoder::fit(QImage(file), side);
    // The mip level the icon needs, not the whole image (texture_decode.h).
    app::Rgba8Image small;
    if (!app::loadDdsPreview(file.toStdString(), side, small)) return {};
    return ThumbDecoder::fit(QImage(small.pixels.data(), small.width, small.height, small.width * 4, QImage::Format_RGBA8888).copy(), side);
}

// Item data: a material the availability audit found without a colour image (its swatch is final).
constexpr int kNoPicture = Qt::UserRole + 3;
// Rows made ahead while the panel is closed (its first page or two).
constexpr int kHiddenRows = 24;

QDir repoData() {
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 3; ++up, d.cdUp())
        if (QFileInfo::exists(d.filePath("data"))) return QDir(d.filePath("data"));
    return QDir(QCoreApplication::applicationDirPath());
}

// A filter proxy that can also restrict to a set of names (materials used in the open map).
class NameFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    std::set<QString> only;
    bool useOnly = false;
    std::set<QString> list;  // favourites or recent picks
    bool useList = false;
    std::set<QString> kit;   // names of the chosen kit
    bool useKit = false;
    QString group;           // the chip ("" = all)
    QString folder;          // effects: names under this folder ("" = all)
    bool loopsOnly = false;  // sounds: the Loops chip
    void refresh() { invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        const QString name = sourceModel()->index(row, 0, parent).data(Qt::DisplayRole).toString();
        if (useOnly && !only.count(name)) return false;
        if (useList && !list.count(name)) return false;
        if (useKit && !kit.count(name)) return false;
        const QModelIndex src = sourceModel()->index(row, 0, parent);
        if (!group.isEmpty() && src.data(kGroupRole).toString() != group) return false;
        if (!folder.isEmpty() && !name.startsWith(folder + "/")) return false;
        if (loopsOnly && !src.data(Qt::UserRole + 2).toBool()) return false;
        return QSortFilterProxyModel::filterAcceptsRow(row, parent);
    }
};

// The map's content as the "In this map" filter sees it (entity ids, item ids and revisions, the file): selection
// clicks also signal documentChanged but leave it unchanged.
quint64 contentSignature(const Document& doc) {
    quint64 h = 1469598103934665603ull;
    auto mix = [&h](quint64 v) { h = (h ^ v) * 1099511628211ull; };
    for (unsigned char c : doc.path()) mix(c);
    for (const Entity& e : doc.map().entities) {
        mix(e.id);
        for (const auto& item : e.items) mix(item.rev * 31 + item.id);
    }
    return h;
}

// focusListOnDown: Down in the search box hands the keyboard to the list.
class DownIntoList : public QObject {
public:
    DownIntoList(QAbstractItemView* list, QObject* parent) : QObject(parent), list_(list) {}

protected:
    bool eventFilter(QObject* watched, QEvent* e) override {
        if (e->type() == QEvent::KeyPress && list_) {
            const auto* ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Down && (ke->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
                list_->setFocus(Qt::TabFocusReason);
                if (!list_->currentIndex().isValid()) {
                    // Home in the list picks its first shown row the way the list itself does (hidden rows skipped).
                    QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
                    QCoreApplication::sendEvent(list_, &home);
                }
                return true;
            }
        }
        return QObject::eventFilter(watched, e);
    }

private:
    QPointer<QAbstractItemView> list_;
};

}  // namespace

void focusListOnDown(QLineEdit* search, QAbstractItemView* list) {
    if (search && list) search->installEventFilter(new DownIntoList(list, search));
}

AssetBrowser::AssetBrowser(EditorContext* ctx, Kind kind, QWidget* parent) : QWidget(parent), ctx_(ctx), kind_(kind) {
    // Flush (owner 2026-11-15, panel_chrome.h): the header line (name, count, favourites star, the dots menu), the search
    // across the panel, one row of filter chips, the list edge to edge, the selection's details strip at the bottom.
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    header_ = new PanelHeader(kind == Kind::Materials ? "Materials" : kind == Kind::Models ? "Models" : kind == Kind::Effects ? "Effects"
                              : kind == Kind::Sounds ? "Sounds" : "Images", this);
    favButton_ = header_->addButton(glyph::Star, "Favourites only (right-click an asset > Add to Favourites)", true, "assetFavourites");
    connect(favButton_, &QToolButton::toggled, this, [this](bool on) { setListModeTo(on ? 1 : 0, {}); });
    menu_ = new QMenu(this);
    header_->addMenuButton(menu_, "What the list shows, options, and the tools of this kind of asset", "assetTools");
    lay->addWidget(header_);
    auto* searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(6, 0, 6, 0);
    searchRow->setSpacing(4);
    search_ = new QLineEdit(this);
    search_->setObjectName(kind == Kind::Materials ? "materialSearch" : kind == Kind::Effects ? "effectSearch"
                           : kind == Kind::Sounds ? "soundSearch" : "modelSearch");
    search_->setPlaceholderText(kind == Kind::Images ? "Search images" : kind == Kind::Effects ? "Search effects"
                                : kind == Kind::Sounds ? "Search sounds" : kind == Kind::Materials ? "Search materials"
                                                                                                  : "Search models");
    search_->setToolTip(kind == Kind::Images ? "50k stock images (colour _c, normal _n, sky_, ...)"
                        : kind == Kind::Effects ? "1020 stock effects that load in any map (e.g. fire, smoke, spark)"
                        : kind == Kind::Sounds ? "11k stock sound aliases that load in any map (amb_, emt_ = ambient loops)"
                        : kind == Kind::Materials ? "46k stock IW8 materials (e.g. concrete, brick, plaster)"
                                                  : "Stock IW8 models (e.g. crate, barrel, car)");
    search_->setClearButtonEnabled(true);
    search_->addAction(glyphIcon(0xE721, 14, themeMix(0.55), "?"), QLineEdit::LeadingPosition);  // a magnifier
    searchRow->addWidget(search_, 1);
    QPushButton* use = nullptr;    // (kept for the wiring below; the strips have Apply / Place now)
    QPushButton* place = nullptr;  // Apply / Place in the details strip
    lay->addLayout(searchRow);
    // The chips: the panel's groups (one at a time, with counts), then toggles.
    chips_ = new ChipBar(this);
    chips_->setObjectName("assetChips");
    if (!groupsOf(kind).empty()) {
        chips_->addChip("all", "All", -1, "Everything listed");
        for (const Group& g : groupsOf(kind)) chips_->addChip(g.id, g.label, -1, g.tip, g.color);
    }
    if (kind == Kind::Sounds) chips_->addToggle("loops", "Loops", false, "Looping aliases only (ambience, engines, music beds)");
    chips_->addToggle("recent", "Recent", false, "The last 30 placed or applied");
    if (kind == Kind::Materials || kind == Kind::Models)
        chips_->addToggle("inmap", "In this map", false,
                          kind == Kind::Materials ? "Materials on the open map's faces" : "Models placed in the open map");
    connect(chips_, &ChipBar::currentChanged, this, [this](const QString& id) {
        static_cast<NameFilter*>(proxy_)->group = id == "all" ? QString() : id;
        static_cast<NameFilter*>(proxy_)->refresh();
        updateInfo();
    });
    connect(chips_, &ChipBar::toggled, this, [this](const QString& id, bool on) {
        auto* f = static_cast<NameFilter*>(proxy_);
        if (id == "recent") return setListModeTo(on ? 2 : 0, {});
        if (id == "loops") {
            f->loopsOnly = on;
            f->refresh();
            return updateInfo();
        }
        if (id == "inmap") {
            usedOnly_ = on;
            f->useOnly = on;
            refreshUsedFilter();
        }
    });
    if (kind == Kind::Materials || kind == Kind::Models) {
        // Kits: stock families from tools/build_asset_kits.py (excluded_* kits are left out of the list), read with the
        // catalogue on the worker thread; the box is shown once applyCatalog() has filled it.
        kit_ = new QComboBox(this);
        kit_->setObjectName("assetKit");
        kit_->addItem("All kits");
        kit_->setToolTip("Stock families (containers, foliage, furniture, ...) from data/asset_catalog/iw8_kits.tsv");
        kit_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        kit_->setMinimumContentsLength(12);  // long kit names are elided, not widening the panel
        kit_->setVisible(false);
        chips_->addWidget(kit_);
    }
    lay->addWidget(chips_);
    // Only what the count cannot say: that the catalogue is being read, or why the list is empty.
    info_ = new QLabel(this);
    info_->setObjectName("assetInfo");
    info_->setWordWrap(true);
    info_->setContentsMargins(6, 0, 6, 0);
    info_->setStyleSheet(QString("color: %1").arg(themeMix(0.6).name()));
    info_->setVisible(false);
    lay->addWidget(info_);
    // Lists with a small size hint: they stretch into the room there is, and a dock that sizes the panel by its hints
    // (a scroll area) keeps the details strip on screen (first shots: the effect strip ran past the bottom).
    class CompactList : public QListView {
    public:
        using QListView::QListView;
        QSize sizeHint() const override { return {200, 100}; }
    };
    // A list whose drags carry the asset (kind, name) so the camera and grid views can take the drop.
    class DragList : public CompactList {
    public:
        DragList(QWidget* parent, const char* kind) : CompactList(parent), kind_(kind) {}

    protected:
        void startDrag(Qt::DropActions) override {
            const QModelIndex idx = currentIndex();
            if (!idx.isValid()) return;
            auto* mime = new QMimeData;
            mime->setData(kAssetMimeType, (QString(kind_) + "\t" + idx.data(Qt::DisplayRole).toString() + "\t" +
                                           (idx.data(Qt::UserRole + 2).toBool() ? "1" : "0")).toUtf8());
            auto* drag = new QDrag(this);
            drag->setMimeData(mime);
            if (const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>(); !icon.isNull()) drag->setPixmap(icon.pixmap(64, 64));
            drag->exec(Qt::CopyAction);
        }

    private:
        const char* kind_;
    };
    const char* dragKind = kind_ == Kind::Materials ? "materials" : kind_ == Kind::Models ? "models"
                           : kind_ == Kind::Effects ? "effects" : kind_ == Kind::Sounds ? "sounds" : nullptr;
    view_ = dragKind ? static_cast<QListView*>(new DragList(this, dragKind)) : new CompactList(this);
    if (dragKind) view_->setDragEnabled(true);
    view_->setFrameShape(QFrame::NoFrame);  // edge to edge
    view_->setUniformItemSizes(true);
    view_->setTextElideMode(Qt::ElideMiddle);  // long asset paths keep both ends
    auto* delegate = new AssetTileDelegate(kind == Kind::Materials ? AssetTileDelegate::MaterialTiles
                                           : kind == Kind::Models  ? AssetTileDelegate::ModelTiles
                                           : kind == Kind::Images  ? AssetTileDelegate::ImageTiles
                                           : kind == Kind::Effects ? AssetTileDelegate::EffectRows
                                                                   : AssetTileDelegate::SoundRows,
                                           view_);
    delegate->facts = &facts_;
    delegate->kind = kind;
    delegate_ = delegate;
    view_->setItemDelegate(delegate);
    if (kind == Kind::Effects || kind == Kind::Sounds) {
        view_->setMouseTracking(true);  // the hover shade
        view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    if (kind == Kind::Sounds) delegate->onPlay = [this](const QModelIndex& idx) { playRow(idx); };
    if (kind == Kind::Materials || kind == Kind::Models || kind == Kind::Images) {
        // Tiles: the whole thumbnail fitted in its box, the short name (models: on two lines), the folder below
        // (owner: previews and names were cut off). The full name is the tooltip.
        view_->setViewMode(QListView::IconMode);
        view_->setMovement(QListView::Static);  // icon mode's free movement let drags shuffle tiles
        view_->setFlow(QListView::LeftToRight);
        view_->setWrapping(true);
        view_->setResizeMode(QListView::Adjust);
        view_->setWordWrap(false);
        view_->setSpacing(0);
        view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // a narrow column wraps the tiles, never scrolls sideways
        if (kind == Kind::Models) view_->setUniformItemSizes(false);  // 7k rows of a constant size hint: cheap, never stale
        gridFit_ = new TileGridFit(view_, delegate);
        view_->viewport()->installEventFilter(gridFit_);  // columns follow the panel width
    }
    if (kind == Kind::Models) {
        // Owner 2026-11-15: larger model pictures, with a size choice (kept in the settings).
        tileSizeBox_ = new QComboBox(this);
        tileSizeBox_->setObjectName("assetTileSize");
        tileSizeBox_->addItems({"Small", "Medium", "Large"});
        tileSizeBox_->setToolTip("Picture size of the model tiles");
        chips_->addWidget(tileSizeBox_);
        tileSize_ = TileSize(std::clamp(Settings::instance().value(settingsKey("tileSize"), 1).toInt(), 0, 2));
        applyTileSize();
        {
            const QSignalBlocker block(tileSizeBox_);
            tileSizeBox_->setCurrentIndex(int(tileSize_));
        }
        connect(tileSizeBox_, &QComboBox::currentIndexChanged, this, [this](int i) { setTileSize(TileSize(i)); });
        lay->addWidget(view_, 1);
        // The selected model in the strip (only while one is selected): a picture, its size and zone, Place, Preview in
        // 3D and Info.
        strip_ = new DetailsStrip(this);
        strip_->setObjectName("modelPreviewBox");
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        preview_ = new QLabel(strip_);
        preview_->setObjectName("modelPreview");
        preview_->setFixedSize(96, 96);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #202226; border: none");
        previewCaption_ = new QLabel("Select a model to preview it", strip_);
        previewCaption_->setWordWrap(true);
        previewCaption_->setStyleSheet(QString("color: %1").arg(themeMix(0.6).name()));
        previewCaption_->setMinimumWidth(0);  // a long unbroken name is elided, never widening the column
        previewCaption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        previewCaption_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        row->addWidget(preview_, 0, Qt::AlignTop);
        row->addWidget(previewCaption_, 1);
        strip_->body()->addLayout(row);
        place = strip_->addAction(glyph::Add, "Place", "Place a static misc_model at the view centre (or drag the tile into a view)", "modelPlace");
        QPushButton* open3d = strip_->addAction(glyph::Cube, "3D",
                                                "A textured 3D view of the model: orbit, pan and zoom, levels of detail, materials, collision hull",
                                                "modelPreview3d");
        connect(open3d, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (idx.isValid()) emit previewRequested(idx.data(Qt::DisplayRole).toString());
        });
        QPushButton* info = strip_->addAction(glyph::Info, "Info", "Model Info: levels of detail, materials, bounds", "modelInfo");
        connect(info, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (idx.isValid()) emit infoRequested(idx.data(Qt::DisplayRole).toString());
        });
        strip_->setVisible(false);
        previewBox_ = strip_;
        lay->addWidget(strip_);
    } else if (kind == Kind::Materials) {
        // The selected material in the strip: a larger picture, its type, how often the map uses it, its techset family;
        // Apply, Select Faces, Replace in Map, Info.
        lay->addWidget(view_, 1);
        strip_ = new DetailsStrip(this);
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        preview_ = new QLabel(strip_);
        preview_->setObjectName("materialPreview");
        preview_->setFixedSize(112, 112);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #202226; border: none");
        row->addWidget(preview_, 0, Qt::AlignTop);
        row->addStretch(1);
        strip_->body()->addLayout(row);
        place = strip_->addAction(glyph::Brush, "Apply", "Apply to the selected brushes or faces (Shift+M; or drag the tile onto a surface)",
                                  "materialApply");
        QPushButton* faces = strip_->addAction(glyph::Target, "Faces", "Select every brush in the map that uses this material", "materialSelectFaces");
        connect(faces, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (!idx.isValid()) return;
            const QString name = idx.data(Qt::DisplayRole).toString();
            const auto refs = findByMaterial(ctx_->doc.map(), name.toStdString(), false);
            ctx_->doc.deselectAll();
            for (const auto& r : refs) ctx_->doc.select(r, true);
            ctx_->documentEdited(QString("Selected %1 primitive(s) with %2").arg(refs.size()).arg(name));
        });
        QPushButton* replace = strip_->addAction(glyph::Redo, "Replace", "Replace this material everywhere in the map with another", "materialReplace");
        connect(replace, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (idx.isValid()) emit replaceRequested(idx.data(Qt::DisplayRole).toString());
        });
        QPushButton* info = strip_->addAction(glyph::Info, "Info", "Material Info: images, shader, surface", "materialInfo");
        connect(info, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (idx.isValid()) emit infoRequested(idx.data(Qt::DisplayRole).toString());
        });
        strip_->setVisible(false);
        lay->addWidget(strip_);
    } else if (kind == Kind::Images) {
        // A grid of thumbnails; the selected image large in the strip with its size and format.
        lay->addWidget(view_, 1);
        strip_ = new DetailsStrip(this);
        preview_ = new QLabel(strip_);
        preview_->setObjectName("modelPreview");
        preview_->setMinimumSize(96, 96);
        preview_->setMaximumHeight(220);
        preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        preview_->setAlignment(Qt::AlignCenter);
        preview_->setStyleSheet("background: #202226; border: none");
        // The caption is kept whole here (previewCaption()); the strip shows it as the title and facts.
        previewCaption_ = new QLabel(strip_);
        previewCaption_->setVisible(false);
        strip_->body()->addWidget(preview_);
        QPushButton* copy = strip_->addAction(glyph::Copy, "Copy name", "Copy the image's whole name", "imageCopyName");
        connect(copy, &QPushButton::clicked, this, [this] {
            const QModelIndex idx = view_->currentIndex();
            if (idx.isValid()) QGuiApplication::clipboard()->setText(idx.data(Qt::DisplayRole).toString());
        });
        strip_->setVisible(false);
        lay->addWidget(strip_);
    } else if (kind == Kind::Effects) {
        // The folders of the effects' paths above the list (a splitter between them), the selected effect playing in
        // the strip as a square (owner 2026-11-15) with Place fx_origin.
        class CompactTree : public QTreeWidget {
        public:
            using QTreeWidget::QTreeWidget;
            QSize sizeHint() const override { return {200, 90}; }
        };
        folders_ = new CompactTree(this);
        folders_->setObjectName("effectFolders");
        folders_->setHeaderHidden(true);
        folders_->setColumnCount(2);
        folders_->setFrameShape(QFrame::NoFrame);
        folders_->setIndentation(12);
        folders_->setUniformRowHeights(true);
        folders_->header()->setStretchLastSection(false);
        folders_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        folders_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        folders_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        folders_->setTextElideMode(Qt::ElideMiddle);
        folders_->setToolTip("The folders of the effects' paths: pick one to list the effects under it");
        connect(folders_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
            if (it) setFolder(it->data(0, Qt::UserRole).toString());
        });
        auto* split = new QSplitter(Qt::Vertical, this);
        split->setObjectName("effectSplit");
        split->setHandleWidth(4);
        split->addWidget(folders_);
        split->addWidget(view_);
        split->setCollapsible(1, false);
        split->setStretchFactor(0, 0);
        split->setStretchFactor(1, 1);
        split->setSizes({110, 400});
        folders_->setMinimumHeight(48);  // the panel's minimum stays small: the strip below needs the room
        view_->setMinimumHeight(64);
        lay->addWidget(split, 1);
        strip_ = new DetailsStrip(this);
        fxPreview_ = new VfxPreview(strip_);
        fxPreview_->setObjectName("effectPreview");
        fxPreview_->setVisible(false);  // a square that follows the column's width (VfxPreview::heightForWidth)
        fxCaption_ = new QLabel(strip_);
        fxCaption_->setObjectName("effectPreviewCaption");
        fxCaption_->setWordWrap(true);
        fxCaption_->setStyleSheet(QString("color: %1").arg(themeMix(0.6).name()));
        fxCaption_->setMinimumWidth(0);
        fxCaption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        strip_->body()->addWidget(fxPreview_);
        strip_->body()->addWidget(fxCaption_);
        place = strip_->addAction(glyph::Add, "Place fx_origin", "Place an fx_origin with this effect at the view centre (or drag the row into a view)",
                                  "effectPlace");
        strip_->setVisible(false);
        lay->addWidget(strip_);
    } else if (kind == Kind::Sounds) {
        // A play button on every row; the selected alias in the strip with its waveform, Play / Stop and Place speaker.
        lay->addWidget(view_, 1);
        strip_ = new DetailsStrip(this);
        wave_ = new WaveView(strip_);
        soundCaption_ = new QLabel("Select a sound, then Play", strip_);
        soundCaption_->setObjectName("soundPreviewCaption");
        soundCaption_->setWordWrap(true);
        soundCaption_->setStyleSheet(QString("color: %1").arg(themeMix(0.6).name()));
        soundCaption_->setMinimumWidth(0);
        soundCaption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        strip_->body()->addWidget(wave_);
        strip_->body()->addWidget(soundCaption_);
        soundPlay_ = strip_->addAction(glyph::Play, "Play",
                                       "Play the selected alias (a random variant, as the game picks one); click again to stop. The first "
                                       "time prepares the zone's sound banks with ACTS (about half a minute, once).",
                                       "soundPlay");
        soundPlay_->setEnabled(false);
        place = strip_->addAction(glyph::Add, "Place speaker",
                                  "Place a sound_emitter with this alias at the view centre (loops keep playing, one-shots repeat)", "soundPlace");
        connect(soundPlay_, &QPushButton::clicked, this, [this] {
            if (soundPlay_->property("playing").toBool()) emit soundStopRequested();
            else playRow(view_->currentIndex());
        });
        strip_->setVisible(false);
        lay->addWidget(strip_);
    } else {
        lay->addWidget(view_, 1);
    }
    // The shipped menu pack of this panel: thumbnails, pills, facts and previews made once (menu_pack.h).
    packKind_ = kind == Kind::Materials ? "materials" : kind == Kind::Models ? "models" : kind == Kind::Effects ? "effects"
                : kind == Kind::Sounds ? "sounds" : "images";
    if (const auto pk = pack()) packFacts_ = pk->facts();
    model_ = new QStandardItemModel(this);
    auto* filter = new NameFilter(this);
    proxy_ = filter;
    proxy_->setSourceModel(model_);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    view_->setModel(proxy_);
    if (const auto pk = pack())
        for (auto [n, pill] : pk->pills().asKeyValueRange()) facts_.insert(n, pill);
    if (kind == Kind::Effects) {
        // What earlier previews found (loop / one-shot length), kept in the settings.
        const QVariantMap kept = Settings::instance().value(settingsKey("facts")).toMap();
        for (auto it = kept.begin(); it != kept.end(); ++it) facts_.insert(it.key(), it.value().toString());
    }

    connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) { setFilter(t); });
    refillListModes();  // tag list modes (4.22)
    if (kit_) connect(kit_, &QComboBox::currentIndexChanged, this, [this] { setKit(kit_->currentData().toString()); });
    auto choose = [this](const QModelIndex& idx) {
        if (!idx.isValid()) return;
        const QString name = idx.data(Qt::DisplayRole).toString();
        noteRecent(name);
        if (kind_ == Kind::Materials) emit materialChosen(name);
        else if (kind_ == Kind::Effects) emit effectChosen(name);
        else if (kind_ == Kind::Sounds) emit soundChosen(name, idx.data(Qt::UserRole + 2).toBool());
        else emit modelChosen(name);
    };
    // Double-click or Enter (activated covers both, so a double-click places once).
    connect(view_, &QListView::activated, this, choose);
    focusListOnDown(search_, view_);
    {
        // Images are previewed only: their menu has no Apply / Place, Info or usage entries.
        view_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view_, &QListView::customContextMenuRequested, this, [this, choose](const QPoint& at) {
            const QModelIndex idx = view_->indexAt(at);
            if (!idx.isValid()) return;
            const QString name = idx.data(Qt::DisplayRole).toString();
            QMenu menu(this);
            if (kind_ != Kind::Images)
                menu.addAction(kind_ == Kind::Materials ? "Apply to Selection" : "Place", this, [choose, idx] { choose(idx); });
            if (kind_ == Kind::Models) menu.addAction("Preview in 3D...", this, [this, name] { emit previewRequested(name); });
            if (kind_ == Kind::Sounds)
                menu.addAction(playingName_ == name && soundPlay_ && soundPlay_->property("playing").toBool() ? "Stop" : "Play", this,
                               [this, idx] { playRow(idx); });
            if (kind_ == Kind::Effects && folders_)
                menu.addAction("Show Its Folder", this, [this, name] { setFolder(name.section('/', 0, -2)); });
            if (kind_ == Kind::Materials || kind_ == Kind::Models || kind_ == Kind::Effects)
                menu.addAction(kind_ == Kind::Materials ? "Material Info..." : kind_ == Kind::Models ? "Model Info..." : "Effect Info...", this,
                               [this, name] { emit infoRequested(name); });
            const bool fav = isFavourite(name);
            menu.addAction(fav ? "Remove from Favourites" : "Add to Favourites", this, [this, name, fav] { setFavourite(name, !fav); });
            menu.addAction(QString("Tags... (%1)").arg(tagsOf(name).isEmpty() ? QString("none") : tagsOf(name).join(", ")), this, [this, name] {
                bool ok = false;
                const QString t = QInputDialog::getText(this, "Tags", "Tags for " + name + " (comma separated):", QLineEdit::Normal,
                                                        tagsOf(name).join(", "), &ok);
                if (ok) setTags(name, t.split(',', Qt::SkipEmptyParts));
            });
            if (kind_ != Kind::Images) {
                const int uses = usageCount(name);
                QAction* u = menu.addAction(QString("Used %1 time(s) in this map").arg(uses));
                u->setEnabled(false);
                if (uses > 0 && (kind_ == Kind::Materials || kind_ == Kind::Models))
                    menu.addAction("Replace Everywhere With...", this, [this, name] { emit replaceRequested(name); });
            }
            menu.addAction("Copy Name", this, [name] { QGuiApplication::clipboard()->setText(name); });
            // The only file a row knows: a material's colour image (exported stock assets, or a custom material's).
            if (const QString file = imageFiles_.value(name); !file.isEmpty() && QFileInfo::exists(file))
                menu.addAction("Show Image in Folder", this,
                               [file] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath())); });
            menu.exec(view_->viewport()->mapToGlobal(at));
        });
    }
    if (kind_ == Kind::Effects || kind_ == Kind::Sounds)
        connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& idx) {
            if (soundPlay_) soundPlay_->setEnabled(idx.isValid() || soundPlay_->property("playing").toBool());
            updateStrip();
            if (kind_ == Kind::Effects && idx.isValid()) emit effectHighlighted(idx.data(Qt::DisplayRole).toString());
        });
    if (kind_ == Kind::Materials)
        connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { updateStrip(); });
    if (kind_ == Kind::Models || kind_ == Kind::Images)
        connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& idx) {
            if (!idx.isValid()) {
                if (previewBox_) previewBox_->setVisible(false);  // models: the preview shows only while one is selected
                updateStrip();
                return;
            }
            updateStrip();  // before the preview: the main window may set it right away
            if (kind_ == Kind::Images) emit imageHighlighted(idx.data(Qt::DisplayRole).toString());
            else emit modelHighlighted(idx.data(Qt::DisplayRole).toString());
        });
    if (use) connect(use, &QPushButton::clicked, this, [this, choose] { choose(view_->currentIndex()); });
    if (place) connect(place, &QPushButton::clicked, this, [this, choose] { choose(view_->currentIndex()); });
    connect(ctx_, &EditorContext::documentChanged, this, [this, filter] {
        // Collected again only when the map's content changed: selection clicks signal a change too, and walking every
        // face of a large map and refiltering 46k rows for a click is wasted work.
        if (filter->useOnly && contentSignature(ctx_->doc) != usedSignature_) refreshUsedFilter();
    });
    // Thumbnails of the rows on screen and around them: stored pictures come from the decoder (worker threads) as soon
    // as the list moves (thumbPass, no wait); rows without one go to the thumbnail source after scrolling settles
    // (slowPass: it may decode a DDS, render or request an export).
    fast_ = new QTimer(this);
    fast_->setSingleShot(true);
    fast_->setInterval(0);
    connect(fast_, &QTimer::timeout, this, [this] { thumbPass(); });
    auto* thumbs = new QTimer(this);
    thumbs_ = thumbs;
    thumbs->setSingleShot(true);
    thumbs->setInterval(120);
    connect(thumbs, &QTimer::timeout, this, [this] { slowPass(); });
    connect(view_->verticalScrollBar(), &QScrollBar::valueChanged, fast_, qOverload<>(&QTimer::start));
    connect(view_->verticalScrollBar(), &QScrollBar::rangeChanged, fast_, qOverload<>(&QTimer::start));
    connect(proxy_, &QSortFilterProxyModel::layoutChanged, fast_, qOverload<>(&QTimer::start));
    connect(proxy_, &QSortFilterProxyModel::modelReset, fast_, qOverload<>(&QTimer::start));
    connect(proxy_, &QSortFilterProxyModel::rowsInserted, fast_, qOverload<>(&QTimer::start));
    connect(proxy_, &QSortFilterProxyModel::rowsRemoved, fast_, qOverload<>(&QTimer::start));
    load();
    fast_->start();
}

// The catalogue as the worker thread read it: plain values, made into list rows on the GUI thread (applyCatalog).
struct AssetBrowser::Catalog {
    struct Row {
        QString name, tip;
        int tone = 0;  // 0 normal, 1 builds grey (no colour image), 2 not available in a build, 3 a looping sound
        bool loop = false;
        QString group;  // the chip group (groupsOf)
        int dist = 0;   // sounds: audible to (units)
    };
    std::vector<Row> rows;
    int hiddenStreamOnly = 0, hiddenMissing = 0;
    QString fileName;
    QHash<QString, QStringList> kits;
    QHash<QString, QString> imageFiles;
};

// Worker thread: reads files only (no widget, no QCoreApplication call: `dataDir` comes from the GUI thread).
std::shared_ptr<AssetBrowser::Catalog> AssetBrowser::readCatalog(Kind kind, bool buildableOnly, const QString& dataDir, bool wantKits) {
    auto c = std::make_shared<Catalog>();
    const QDir data(dataDir);
    // Availability catalog: name -> (status, reason).
    QHash<QString, QPair<QString, QString>> status;
    if (kind == Kind::Materials || kind == Kind::Models || kind == Kind::Effects || kind == Kind::Images) {
        // Every status file of the kind (an audit can run as two halves: iw8_<kind>_status.tsv and ..._status_b.tsv).
        const QDir cat(data.filePath("asset_catalog"));
        const QString pattern = kind == Kind::Materials ? "iw8_materials_status*.tsv"
                                : kind == Kind::Effects ? "iw8_effects_status*.tsv"
                                : kind == Kind::Images  ? "iw8_images_status*.tsv"
                                                        : "iw8_models_status*.tsv";
        for (const QString& fn : cat.entryList({pattern}, QDir::Files)) {
            QFile sf(cat.filePath(fn));
            if (!sf.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            QTextStream in(&sf);
            while (!in.atEnd()) {
                const QString l = in.readLine();
                if (l.startsWith('#')) continue;
                const QStringList f = l.split('\t');
                if (f.size() >= 2 && f[1] != "ok") status.insert(f[0], {f[1], f.value(2)});
            }
        }
    }
    const QString file = data.filePath(kind == Kind::Materials ? "asset_catalog/iw8_materials.tsv"
                                       : kind == Kind::Effects ? "asset_catalog/iw8_common_vfx.tsv"
                                       : kind == Kind::Images  ? "asset_catalog/iw8_images.tsv"
                                       : kind == Kind::Sounds  ? "asset_catalog/iw8_common_sounds.tsv"
                                                               : "asset_catalog/iw8_models.tsv");
    c->fileName = QFileInfo(file).fileName();
    QFile f(file);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        in.readLine();  // header (names themselves may start with '#')
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const int tab = line.indexOf('\t');
            if (tab <= 0) continue;
            Catalog::Row row;
            row.name = line.left(tab);
            if (kind == Kind::Sounds) {
                // alias<TAB>loop<TAB>distMax<TAB>zones
                const QStringList s = line.split('\t');
                if (line.startsWith('#') || s.size() < 4) continue;
                row.loop = s[1] == "1";
                row.dist = s[2].toInt();
                row.tip = QString("%1\n%2, audible to %3 units\nzones: %4").arg(s[0], row.loop ? "loop" : "one-shot", s[2], s[3]);
                row.tone = row.loop ? 3 : 0;
                c->rows.push_back(std::move(row));
                continue;
            }
            if (kind == Kind::Models) {
                // Weapons, attachments and cosmetics that only the streaming zone lists (8.9k of 15.6k) are not map
                // props and ACTS cannot export them from there: left out so every listed model can be placed and built.
                bool streamOnly = true;
                for (const QString& z : line.mid(tab + 1).split(',', Qt::SkipEmptyParts))
                    streamOnly &= z.trimmed() == "global_stream_mp" || z.trimmed() == "techsets_global_stream_mp";
                if (streamOnly) {
                    ++c->hiddenStreamOnly;
                    continue;
                }
            }
            if (const auto st = status.constFind(row.name); st != status.constEnd()) {
                if (st->first == "missing" && buildableOnly) {
                    ++c->hiddenMissing;
                    continue;
                }
                row.tone = st->first == "grey" ? 1 : 2;
                row.tip = row.name + "\nzones: " + line.mid(tab + 1).left(300) +
                          (st->first == "grey" ? "\nBUILDS GREY: no colour image in Replay (" : "\nNOT AVAILABLE in a build (") +
                          st->second.left(200) + ")";
                c->rows.push_back(std::move(row));
                continue;
            }
            row.tip = row.name + "\nzones: " + line.mid(tab + 1).left(300);
            c->rows.push_back(std::move(row));
        }
    }
    // The chip group of every row (here, on the worker thread, with this thread's expressions).
    if (const auto& groups = groupsOf(kind); !groups.empty()) {
        const std::vector<QRegularExpression> res = groupExpressions(groups);
        for (Catalog::Row& r : c->rows) r.group = groupOfName(groups, res, r.name.toLower());
    }
    // Colour images from ACTS exports (data/stock_assets/<zone>/index.json).
    if (kind == Kind::Materials) {
        const QDir assets(data.filePath("stock_assets"));
        for (const QString& zone : assets.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile idx(assets.filePath(zone + "/index.json"));
            if (!idx.open(QIODevice::ReadOnly)) continue;
            const QJsonArray mats = QJsonDocument::fromJson(idx.readAll()).object().value("materials").toArray();
            for (const auto& v : mats) {
                const QJsonObject m = v.toObject();
                const QString img = m.value("color_image_file").toString();
                if (!img.isEmpty()) c->imageFiles.insert(m.value("name").toString(), assets.filePath(zone + "/" + img));
            }
        }
    }
    if (wantKits) {
        QFile kf(data.filePath("asset_catalog/iw8_kits.tsv"));
        const QString want = kind == Kind::Models ? "model" : "material";
        if (kf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&kf);
            while (!in.atEnd()) {
                const QStringList k = in.readLine().split('\t');
                if (k.size() < 3 || k[0].startsWith('#') || k[0] == "kit" || k[1] != want || k[0].startsWith("excluded")) continue;
                c->kits[k[0]] << k[2];
            }
        }
    }
    return c;
}

void AssetBrowser::load() {
    const int gen = ++loadGen_;
    const bool buildable = buildableOnly_;
    const QString dataDir = repoData().absolutePath();
    const bool wantKits = kit_ && kitNames_.isEmpty();
    const Kind kind = kind_;
    if (model_->rowCount() == 0) {
        info_->setText("Reading the catalogue...");
        info_->setVisible(true);
    }
    QPointer<AssetBrowser> self(this);
    std::thread(guardedWorker(__FILE__, __LINE__, [self, gen, kind, buildable, dataDir, wantKits] {
        std::shared_ptr<const Catalog> cat = readCatalog(kind, buildable, dataDir, wantKits);
        QMetaObject::invokeMethod(qApp, [self, gen, cat] {
            if (self && gen == self->loadGen_) self->applyCatalog(*cat);  // a newer load() replaces this one
        }, Qt::QueuedConnection);
    })).detach();
}

QStandardItem* AssetBrowser::customItem(const QString& name, const QString& image) const {
    auto* item = new QStandardItem(name);
    item->setEditable(false);
    item->setData(true, Qt::UserRole + 1);
    item->setData(QString("own"), kGroupRole);
    item->setToolTip(name + "\ncustom material: " + image);
    return item;
}

void AssetBrowser::applyCatalog(const Catalog& c) {
    loadedGen_ = loadGen_;
    hiddenStreamOnly_ = c.hiddenStreamOnly;
    hiddenMissing_ = c.hiddenMissing;
    QList<QStandardItem*> items;
    items.reserve(qsizetype(customMaterials_.size()) + qsizetype(c.rows.size()));
    for (const auto& [name, image] : customMaterials_) items.append(customItem(name, image));  // the custom ones first
    for (const Catalog::Row& r : c.rows) {
        auto* item = new QStandardItem(r.name);
        item->setEditable(false);
        item->setToolTip(r.tip);
        if (kind_ == Kind::Sounds) item->setData(r.loop, Qt::UserRole + 2);
        if (!r.group.isEmpty()) item->setData(r.group, kGroupRole);
        if (r.dist > 0) item->setData(r.dist, kDistRole);
        if (r.tone == 1) item->setForeground(QColor(200, 160, 90));
        if (r.tone == 1 && kind_ == Kind::Materials) item->setData(true, kNoPicture);
        else if (r.tone == 2) item->setForeground(QColor(130, 130, 130));
        // tone 3 (a looping sound): the row's "loop" pill says it
        items.append(item);
    }
    model_->clear();
    iconOrder_.clear();  // the old items are gone; their icons stay in icons_ for the new ones
    items_.clear();
    items_.reserve(items.size());
    for (QStandardItem* it : items) items_.insert(it->text(), it);
    model_->invisibleRootItem()->appendRows(items);  // one insert (a row at a time made the proxy filter each)
    for (auto it = c.imageFiles.constBegin(); it != c.imageFiles.constEnd(); ++it) imageFiles_.insert(it.key(), it.value());
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
        chips_->chip("loops")->setText(QString("Loops\t%1").arg(thousands(loops)));
    }
    fillFolders();
    if (kit_ && kitNames_.isEmpty() && !c.kits.isEmpty()) {
        kitNames_ = c.kits;
        QStringList names = kitNames_.keys();
        names.sort();
        const QSignalBlocker block(kit_);
        for (const QString& k : names) kit_->addItem(QString(k).replace('_', ' '), k);
        kit_->setVisible(true);
    }
    if (kind_ == Kind::Effects) {
        // Narrow column: the summary drops the source file name (the search tooltip says what the list is), so it stays on one line.
        catalogInfo_ = QString("%1 stock effects (Replay 1.20)").arg(c.rows.size())
                       + (hiddenMissing_ ? QString("; %1 not buildable hidden").arg(hiddenMissing_) : QString());
    } else {
        catalogInfo_ = (QString("%1 stock %2 (Replay 1.20, %3)%4")
                           .arg(c.rows.size())
                           .arg(kind_ == Kind::Materials ? "materials" : kind_ == Kind::Images ? "images"
                                : kind_ == Kind::Sounds ? "sound aliases" : "models")
                           .arg(c.fileName)
                           .arg(kind_ == Kind::Materials ? QString(", %1 with thumbnails").arg(imageFiles_.size())
                                : kind_ == Kind::Models && hiddenStreamOnly_
                                    ? QString("; %1 weapon/attachment models of the streaming zone not listed").arg(hiddenStreamOnly_)
                                    : QString())
                           + (hiddenMissing_ ? QString("; %1 not buildable hidden").arg(hiddenMissing_) : QString()));
    }
    updateInfo();
    emit catalogReady();
    if (fast_) fast_->start();  // the first rows' stored pictures are decoded now, before the panel is opened
}

void AssetBrowser::setCustomMaterials(const QList<QPair<QString, QString>>& materials) {
    customMaterials_ = materials;  // kept for a catalogue (re)load that lands later
    // Drop the previous custom rows (flagged with UserRole + 1), then insert the new ones at the top.
    for (int r = model_->rowCount() - 1; r >= 0; --r)
        if (model_->item(r)->data(Qt::UserRole + 1).toBool()) {
            items_.remove(model_->item(r)->text());
            model_->removeRow(r);
        }
    for (int i = materials.size() - 1; i >= 0; --i) {
        QStandardItem* item = customItem(materials[i].first, materials[i].second);
        model_->insertRow(0, item);
        items_.insert(materials[i].first, item);
        imageFiles_.insert(materials[i].first, materials[i].second);
        icons_.remove(materials[i].first);
        peekMissed_.remove(materials[i].first);
    }
    updateInfo();
}

QIcon AssetBrowser::placeholderIcon(const QString& name) const {
    // The material's hashed viewport colour, so the swatch matches the 3D view.
    const uint32_t c = app::colorForName(name.toStdString());
    QPixmap px(iconSide(), iconSide());
    px.fill(QColor(int(c & 0xff), int((c >> 8) & 0xff), int((c >> 16) & 0xff)));
    return QIcon(px);
}

QIcon AssetBrowser::thumbnail(const QString& name) {
    if (auto it = icons_.find(name); it != icons_.end()) return *it;
    QIcon icon;
    if (const auto pk = pack(); pk && !imageFiles_.contains(name))
        if (const QImage img = pk->thumb(name); !img.isNull()) {
            // The shipped pack: no cache, no export.
            icon = QIcon(QPixmap::fromImage(ThumbDecoder::fit(img, iconSide())));
            realThumbs_.insert(name);
            icons_.insert(name, icon);
            return icon;
        }
    const QString file = imageFiles_.value(name);
    if (file.isEmpty() && thumbSource_) {
        bool pending = false;
        const QImage img = thumbSource_(name, &pending);
        if (!img.isNull()) {
            icon = QIcon(QPixmap::fromImage(ThumbDecoder::fit(img, iconSide())));
            if (pending) {  // an interim picture (models: the grey one while the textured one is made)
                pendingThumbs_.insert(name);
                return icon;
            }
            pendingThumbs_.remove(name);
            peekMissed_.remove(name);  // stored now: the decoder finds it next time
            realThumbs_.insert(name);
            icons_.insert(name, icon);
            return icon;
        }
        if (pending) pendingThumbs_.insert(name);
        else pendingThumbs_.remove(name);
    }
    if (!file.isEmpty())
        if (const QImage q = decodeImageFile(file, iconSide()); !q.isNull()) icon = QIcon(QPixmap::fromImage(q));
    if (!icon.isNull()) realThumbs_.insert(name);
    if (icon.isNull()) {
        icon = placeholderIcon(name);
    }
    if (!pendingThumbs_.contains(name)) icons_.insert(name, icon);  // pending: asked again on refreshThumbnails()
    return icon;
}

void AssetBrowser::refreshThumbnails() {
    // A cache answered: the rows waiting for it are asked again (the slow pass), and rows the packs had nothing for may
    // have a stored picture now (the decoder).
    for (const QString& n : pendingThumbs_) icons_.remove(n);
    peekMissed_.clear();
    if (thumbs_) thumbs_->start();
    if (fast_) fast_->start();
}

int AssetBrowser::thumbnailsLoaded() const { return int(realThumbs_.size()); }

void AssetBrowser::forgetThumbnails() {
    for (auto it = items_.constBegin(); it != items_.constEnd(); ++it) it.value()->setIcon(QIcon());
    icons_.clear();
    iconOrder_.clear();
    realThumbs_.clear();
    peekMissed_.clear();
    pendingThumbs_.clear();
    if (fast_) fast_->start();
}

int AssetBrowser::picturedRows(int count) const {
    int n = 0;
    for (int r = 0; r < std::min(count, proxy_->rowCount()); ++r) {
        const QStandardItem* item = model_->itemFromIndex(proxy_->mapToSource(proxy_->index(r, 0)));
        n += item && !item->icon().isNull() && realThumbs_.contains(item->text());
    }
    return n;
}

void AssetBrowser::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    fitEffectSquare();
}

void AssetBrowser::fitEffectSquare() {
    // The effect square takes at most 2/5 of the height the panel is shown in, so the list keeps its room. The visible
    // height, not the panel's own: a dock may hold the panel in a scroll area that grows with the panel's minimum, and
    // a square following that height grew the panel past the screen (first shots: the strip scrolled off).
    if (!fxPreview_) return;
    int avail = height();
    for (QWidget* p = parentWidget(); p; p = p->parentWidget())
        if (auto* sa = qobject_cast<QAbstractScrollArea*>(p)) {
            avail = sa->viewport()->height();
            break;
        }
    fxPreview_->setMaxSide(std::clamp(avail * 2 / 5, 160, 420));
}

void AssetBrowser::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!shownOnce_) {
        shownOnce_ = true;
        emit firstShown();
    }
    fitEffectSquare();
    emptyPasses_ = 0;
    if (gridFit_) static_cast<TileGridFit*>(gridFit_)->fit();
    if (fast_) fast_->start();
}

bool AssetBrowser::previewShown() const { return previewBox_ && previewBox_->isVisible(); }

int AssetBrowser::tileWidth() const { return view_->gridSize().width(); }

int AssetBrowser::iconSide() const {
    if (kind_ == Kind::Models && delegate_) return static_cast<AssetTileDelegate*>(delegate_)->maxIcon();
    return AssetTileDelegate::kIcon;
}

void AssetBrowser::applyTileSize() {
    auto* d = static_cast<AssetTileDelegate*>(delegate_);
    if (!d || d->style() != AssetTileDelegate::ModelTiles) return;
    // Preset side and the most a picture grows to fill the row: about 3-4 small, 2 medium, 1-2 large tiles across
    // a 300-420 px column.
    static constexpr int kSides[3][2] = {{72, 96}, {120, 156}, {184, 240}};
    const int i = std::clamp(int(tileSize_), 0, 2);
    d->setModelSize(kSides[i][0], kSides[i][1]);
    view_->setIconSize(QSize(kSides[i][1], kSides[i][1]));
    if (gridFit_) static_cast<TileGridFit*>(gridFit_)->fit();
}

void AssetBrowser::setTileSize(TileSize t) {
    if (kind_ != Kind::Models) return;
    const bool changed = t != tileSize_;
    tileSize_ = t;
    Settings::instance().setValue(settingsKey("tileSize"), int(t));
    if (tileSizeBox_ && tileSizeBox_->currentIndex() != int(t)) {
        const QSignalBlocker block(tileSizeBox_);
        tileSizeBox_->setCurrentIndex(int(t));
    }
    if (!changed) return;
    applyTileSize();
    // The pictures are decoded again at the new side (from the packs: a page takes a frame or two).
    for (const QString& n : iconOrder_)
        if (QStandardItem* item = items_.value(n)) item->setIcon(QIcon());
    for (auto it = icons_.begin(); it != icons_.end(); ++it)
        if (QStandardItem* item = items_.value(it.key())) item->setIcon(QIcon());
    icons_.clear();
    iconOrder_.clear();
    view_->doItemsLayout();
    if (fast_) fast_->start();
}

void AssetBrowser::setThumbnailPeek(ThumbPeek fn) {
    // IW8R_THUMB_DECODER=0: every picture loads on the GUI thread as before (thumb_bench.h compares the two).
    if (qEnvironmentVariable("IW8R_THUMB_DECODER") == "0") return;
    QPointer<AssetBrowser> self(this);
    decoder_ = std::make_unique<ThumbDecoder>(
        [peek = std::move(fn), kind = packKind_](const QString& key) -> QImage {
            // "file:<path>": a row's own image file; else the shipped pack's picture, then a stored one by name. The
            // decoder fits it to the tile.
            if (key.startsWith("file:")) return decodeImageFile(key.mid(5), 256);
            if (const auto pack = MenuPack::shared(kind))
                if (QImage img = pack->thumb(key); !img.isNull()) return img;
            return peek ? peek(key) : QImage();
        },
        [self](std::vector<ThumbDecoder::Result> results) {
            if (self) self->applyDecoded(std::move(results));
        },
        this, 3);
    peekMissed_.clear();
    if (fast_) fast_->start();
}

QString AssetBrowser::decoderKey(const QString& name) {
    const QString file = imageFiles_.value(name);
    if (file.isEmpty()) return name;
    const QString key = "file:" + file;
    fileKeys_.insert(key, name);
    return key;
}

std::pair<int, int> AssetBrowser::visibleRange() const {
    if (!view_->isVisible() || proxy_->rowCount() == 0) return {0, -1};
    const QRect vis = view_->viewport()->rect();
    // Start at the row under the top-left of the viewport and stop below its bottom: the list has up to 46k rows, and
    // asking every one for its rectangle on each pass is what made scrolling stutter. Rows are laid out top to bottom.
    int r = -1;
    for (const QPoint probe : {QPoint(4, 4), QPoint(vis.width() / 2, 4), QPoint(4, vis.height() / 2), QPoint(vis.width() / 2, vis.height() / 2)})
        if (const QModelIndex at = view_->indexAt(probe); at.isValid()) {
            r = std::max(0, at.row() - 64);  // a few rows back: the probe may sit below a partly visible row
            break;
        }
    if (r < 0) {
        // No probe on a tile (gaps between them): the row the scroll position implies, a few rows back.
        const QSize g = view_->gridSize();
        if (view_->viewMode() == QListView::IconMode && g.width() > 0 && g.height() > 0) {
            const int cols = std::max(1, vis.width() / g.width());
            r = std::max(0, view_->verticalScrollBar()->value() / g.height() * cols - 2 * cols);
        } else {
            r = 0;
        }
    }
    int first = -1, last = -1;
    for (; r < proxy_->rowCount(); ++r) {
        const QRect rect = view_->visualRect(proxy_->index(r, 0));
        if (rect.top() > vis.bottom()) break;
        if (!rect.intersects(vis)) continue;
        if (first < 0) first = r;
        last = r;
    }
    return first < 0 ? std::pair<int, int>{0, -1} : std::pair<int, int>{first, last};
}

void AssetBrowser::thumbPass() {
    if (kind_ == Kind::Effects || kind_ == Kind::Sounds) return;  // effects and sounds have no image
    auto [first, last] = visibleRange();
    if (last < first) {
        if (view_->isVisible() && proxy_->rowCount() > 0 && ++emptyPasses_ < 20) {
            fast_->start(30);  // shown but not laid out yet: once it is
            return;
        }
        // Not on screen: the first rows are made ahead (stored pictures decoded, the others asked of the source as
        // before), so the panel opens with its pictures.
        if (view_->isVisible() || proxy_->rowCount() == 0) return;
        first = 0;
        last = std::min(proxy_->rowCount(), kHiddenRows) - 1;
    }
    emptyPasses_ = 0;
    fast_->setInterval(0);
    const int page = last - first + 1;
    QStringList want;
    bool slow = false;
    const auto consider = [&](int r, bool onScreen) {
        if (r < 0 || r >= proxy_->rowCount()) return;
        QStandardItem* item = model_->itemFromIndex(proxy_->mapToSource(proxy_->index(r, 0)));
        if (!item) return;
        const QString name = item->text();
        if (pendingThumbs_.contains(name)) {
            slow |= onScreen;  // its cache was asked: the source answers it (refreshThumbnails)
            return;
        }
        if (!item->icon().isNull()) return;
        if (const auto it = icons_.constFind(name); it != icons_.constEnd()) {
            item->setIcon(*it);
            return;
        }
        if (decoder_ && !peekMissed_.contains(name)) want << decoderKey(name);
        else slow |= onScreen;
    };
    // The rows on screen first, then the next two pages, then the page above.
    for (int r = first; r <= last; ++r) consider(r, true);
    for (int r = last + 1; r <= last + 2 * page; ++r) consider(r, false);
    for (int r = first - page; r < first; ++r) consider(r, false);
    if (decoder_) decoder_->want(want, iconSide());  // replaces what an older position asked for
    if (slow) thumbs_->start();  // restarted while the list moves: the caches are asked once it stops
    evictIcons(first - page, last + 2 * page);
}

void AssetBrowser::applyDecoded(std::vector<ThumbDecoder::Result> results) {
    bool slow = false;
    for (ThumbDecoder::Result& r : results) {
        const QString name = r.name.startsWith("file:") ? fileKeys_.value(r.name) : r.name;
        if (name.isEmpty() || r.side != iconSide()) continue;  // decoded for another tile size: asked again
        QStandardItem* item = items_.value(name);
        if (!item) continue;
        if (r.image.isNull()) {
            if (item->data(kNoPicture).toBool()) {
                // The availability audit found no colour image in the game ("builds grey"): its colour swatch for good,
                // with no export asked for.
                setRowIcon(item, name, placeholderIcon(name));
                continue;
            }
            peekMissed_.insert(name);  // nothing stored: the thumbnail source makes or requests it
            slow = true;
            continue;
        }
        pendingThumbs_.remove(name);
        realThumbs_.insert(name);
        setRowIcon(item, name, QIcon(QPixmap::fromImage(r.image)));
        markStatus(item);
    }
    if (slow && !thumbs_->isActive()) thumbs_->start();
}

void AssetBrowser::setRowIcon(QStandardItem* item, const QString& name, const QIcon& icon) {
    item->setIcon(icon);
    if (!icons_.contains(name)) iconOrder_.push_back(name);
    icons_.insert(name, icon);
}

void AssetBrowser::markStatus(QStandardItem* item) {
    if (!statusSource_ || unavailable_.contains(item->text())) return;
    const QString why = statusSource_(item->text());
    if (why.isEmpty()) return;
    unavailable_.insert(item->text());
    item->setForeground(QColor(130, 130, 130));
    item->setToolTip(item->toolTip() + "\nNOT AVAILABLE in a build: " + why.left(300));
}

void AssetBrowser::evictIcons(int keepFrom, int keepTo) {
    // About 96 MB of pictures per browser (6,000 material tiles, 400 large model tiles): the oldest ones away from the
    // view are dropped and decoded again from the packs when they come back on screen.
    const int side = iconSide();
    const size_t cap = std::max<size_t>(240, size_t(96) * 1024 * 1024 / (size_t(side) * side * 4));
    if (iconOrder_.size() <= cap) return;
    std::vector<QString> keep;
    size_t at = 0;
    for (; at < iconOrder_.size() && iconOrder_.size() - at + keep.size() > cap; ++at) {
        const QString& n = iconOrder_[at];
        QStandardItem* item = items_.value(n);
        const int row = item ? proxy_->mapFromSource(item->index()).row() : -1;
        if (row >= keepFrom && row <= keepTo) {
            keep.push_back(n);  // near the view: kept
            continue;
        }
        icons_.remove(n);
        if (item) item->setIcon(QIcon());
    }
    keep.insert(keep.end(), iconOrder_.begin() + qsizetype(at), iconOrder_.end());
    iconOrder_ = std::move(keep);
}

void AssetBrowser::slowPass() {
    if (kind_ == Kind::Effects || kind_ == Kind::Sounds) return;
    auto [first, last] = visibleRange();
    if (last < first && !view_->isVisible()) first = 0, last = std::min(proxy_->rowCount(), kHiddenRows) - 1;  // see thumbPass
    int budget = 48;  // rows made per pass (rendering a model or decoding a DDS costs a few ms)
    for (int r = first; r <= last && budget > 0; ++r) {
        QStandardItem* item = model_->itemFromIndex(proxy_->mapToSource(proxy_->index(r, 0)));
        if (!item) continue;
        const QString name = item->text();
        const bool pending = pendingThumbs_.contains(name);
        if (!item->icon().isNull() && !pending) continue;
        if (decoder_ && !peekMissed_.contains(name) && !pending) continue;  // the decoder brings its stored picture
        const bool had = icons_.contains(name);
        const QIcon icon = thumbnail(name);
        item->setIcon(icon);
        if (!had && icons_.contains(name)) iconOrder_.push_back(name);  // kept: counted for the memory bound
        markStatus(item);
        // A row that is still waiting for its cache costs a lookup only: every visible row is asked for in the first
        // pass, so the caches get one large request instead of 48 names at a time.
        if (!pendingThumbs_.contains(name)) --budget;
    }
    if (budget <= 0) thumbs_->start();  // more visible rows are waiting: continue after the event loop has run
}

QScrollBar* AssetBrowser::scrollBar() const { return view_->verticalScrollBar(); }

VisibleThumbs AssetBrowser::visibleThumbState() const {
    VisibleThumbs s;
    if (kind_ == Kind::Effects || kind_ == Kind::Sounds || !view_->isVisible()) return s;
    const QRect vis = view_->viewport()->rect();
    const auto [first, last] = visibleRange();
    for (int r = first; r <= last; ++r) {
        const QModelIndex idx = proxy_->index(r, 0);
        if (!view_->visualRect(idx).intersects(vis)) continue;
        ++s.visible;
        const QStandardItem* item = model_->itemFromIndex(proxy_->mapToSource(idx));
        const QString n = idx.data(Qt::DisplayRole).toString();
        if (!item || item->icon().isNull() || pendingThumbs_.contains(n)) ++s.waiting;
        else if (realThumbs_.contains(n)) ++s.real;
    }
    return s;
}

void AssetBrowser::refreshUsedFilter() {
    auto* filter = static_cast<NameFilter*>(proxy_);
    usedSignature_ = contentSignature(ctx_->doc);
    filter->only.clear();
    for (const auto& e : ctx_->doc.map().entities) {
        if (kind_ == Kind::Models) {
            if (const std::string* m = e.value("model"); m && !m->empty()) filter->only.insert(QString::fromStdString(*m));
            continue;
        }
        for (const auto& it : e.items)
            if (const Primitive* p = it.prim()) {
                if (p->kind == PrimKind::Patch) filter->only.insert(QString::fromStdString(p->patchMaterial));
                for (const auto& face : p->faces) filter->only.insert(QString::fromStdString(face.material()));
            }
    }
    filter->refresh();
    updateInfo();
}

void AssetBrowser::updateInfo() {
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
}

int AssetBrowser::usageCount(const QString& qname) const {
    const std::string name = qname.toStdString();
    int n = 0;
    for (const auto& e : ctx_->doc.map().entities) {
        if (kind_ == Kind::Materials) {
            for (const auto& it : e.items)
                if (const Primitive* p = it.prim()) {
                    if (p->kind == PrimKind::Patch && p->patchMaterial == name) ++n;
                    for (const auto& face : p->faces) n += face.material() == name;
                }
            continue;
        }
        const char* key = kind_ == Kind::Models ? "model" : kind_ == Kind::Effects ? "fx" : "soundalias";
        if (const std::string* v = e.value(key); v && *v == name) ++n;
    }
    return n;
}

QStringList AssetBrowser::tagsOf(const QString& name) const {
    return Settings::instance().value(settingsKey("tags")).toMap().value(name).toStringList();
}

void AssetBrowser::setTags(const QString& name, const QStringList& tags) {
    QVariantMap all = Settings::instance().value(settingsKey("tags")).toMap();
    QStringList clean;
    for (QString t : tags) {
        t = t.trimmed().toLower();
        if (!t.isEmpty() && !clean.contains(t)) clean << t;
    }
    if (clean.isEmpty()) all.remove(name);
    else all.insert(name, clean);
    Settings::instance().setValue(settingsKey("tags"), all);
    refillListModes();
}

QStringList AssetBrowser::allTags() const {
    QStringList out;
    const QVariantMap all = Settings::instance().value(settingsKey("tags")).toMap();
    for (auto it = all.begin(); it != all.end(); ++it)
        for (const QString& t : it.value().toStringList())
            if (!out.contains(t)) out << t;
    out.sort();
    return out;
}

void AssetBrowser::refillListModes() {
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
}

void AssetBrowser::setFilter(const QString& text) {
    filterText_ = text;
    proxy_->setFilterFixedString(text);
    updateInfo();
}

QString AssetBrowser::settingsKey(const char* what) const {
    const char* k = kind_ == Kind::Materials ? "materials" : kind_ == Kind::Models ? "models" : kind_ == Kind::Effects ? "effects"
                    : kind_ == Kind::Sounds ? "sounds" : "images";
    return QString("assets/%1/%2").arg(k, what);
}

bool AssetBrowser::isFavourite(const QString& name) const {
    return Settings::instance().value(settingsKey("favourites")).toStringList().contains(name);
}

void AssetBrowser::setFavourite(const QString& name, bool on) {
    QStringList f = Settings::instance().value(settingsKey("favourites")).toStringList();
    f.removeAll(name);
    if (on) f.prepend(name);
    Settings::instance().setValue(settingsKey("favourites"), f);
    applyListMode();
}

QStringList AssetBrowser::recent() const { return Settings::instance().value(settingsKey("recent")).toStringList(); }

void AssetBrowser::noteRecent(const QString& name) {
    QStringList r = recent();
    r.removeAll(name);
    r.prepend(name);
    while (r.size() > 30) r.removeLast();
    Settings::instance().setValue(settingsKey("recent"), r);
    if (listMode_ == 2) applyListMode();
}

QStringList AssetBrowser::kits() const {
    QStringList k = kitNames_.keys();
    k.sort();
    return k;
}

void AssetBrowser::setKit(const QString& kit) {
    auto* filter = static_cast<NameFilter*>(proxy_);
    filter->useKit = !kit.isEmpty() && kitNames_.contains(kit);
    filter->kit.clear();
    if (filter->useKit)
        for (const QString& n : kitNames_.value(kit)) filter->kit.insert(n);
    if (kit_ && kit_->currentData().toString() != kit) {
        const int i = kit_->findData(kit);
        kit_->blockSignals(true);
        kit_->setCurrentIndex(i < 0 ? 0 : i);
        kit_->blockSignals(false);
    }
    filter->refresh();
    updateInfo();
}

void AssetBrowser::setListMode(ListMode m) { setListModeTo(int(m), {}); }

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
    }
    filter->list.insert(names.begin(), names.end());
    filter->refresh();
    updateInfo();
}
int AssetBrowser::assetCount() const { return model_->rowCount(); }
int AssetBrowser::visibleCount() const { return proxy_->rowCount(); }
QString AssetBrowser::visibleName(int row) const { return proxy_->index(row, 0).data(Qt::DisplayRole).toString(); }

void AssetBrowser::setPreview(const QImage& image, const QString& caption) {
    if (!preview_) return;
    // Fitted into the preview box (keeping its shape) instead of shown at full size and cut off.
    QPixmap pm = image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    // Images: the strip may not be laid out yet (shown with this preview), so the box is the panel's width.
    const QSize box = kind_ == Kind::Images ? QSize(std::max(96, width() - 16), 210) : preview_->size() - QSize(4, 4);
    if (!pm.isNull() && (pm.width() > box.width() || pm.height() > box.height()))
        pm = pm.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    preview_->setPixmap(pm);
    // Models: shown while a model is selected. A preview arriving later (its picture or export finished) for a model
    // that is no longer selected updates the box but does not bring it back.
    if (previewBox_) previewBox_->setVisible(view_->currentIndex().isValid());
    if (kind_ == Kind::Images || kind_ == Kind::Models) {
        // The name (the first line) is elided in the middle to the column width; the full caption is the tooltip. The
        // text itself stays whole, so previewCaption() still returns it.
        const QStringList lines = caption.split('\n');
        const int width = std::max(120, previewCaption_->width() - 8);
        previewCaption_->setText(lines.isEmpty() ? QString()
                                 : QFontMetrics(previewCaption_->font()).elidedText(lines.first(), Qt::ElideMiddle, width) + caption.mid(lines.first().size()));
        previewCaption_->setToolTip(caption);
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
    previewCaption_->setText(caption);
}

void AssetBrowser::setEffectPreview(const VfxSimDef* def, const QString& caption) {
    if (!fxPreview_) return;
    if (def) fxPreview_->setEffect(*def);
    fxPreview_->setVisible(def != nullptr);
    // The strip's title is the effect; this line says what the preview shows, or why there is none.
    const QString status = caption.section('\n', 1);
    fxCaption_->setText(status.isEmpty() ? caption : status);
    fxCaption_->setToolTip(caption);
}

void AssetBrowser::setSoundStatus(const QString& text, bool playing) {
    if (!soundPlay_) return;
    soundPlay_->setProperty("playing", playing);
    soundPlay_->setText(playing ? "Stop" : "Play");
    soundPlay_->setIcon(glyphIcon(playing ? glyph::Stop : glyph::Play, 14));
    soundPlay_->setEnabled(playing || view_->currentIndex().isValid());
    soundCaption_->setText(text);
    soundCaption_->setToolTip(text);
    // The row of the alias that plays shows Stop; the waveform's playhead stops with it.
    static_cast<AssetTileDelegate*>(delegate_)->playing = playing ? playingName_ : QString();
    if (!playing && wave_) static_cast<WaveView*>(wave_)->setPlaying(false);
    view_->viewport()->update();
}

QString AssetBrowser::soundStatus() const { return soundCaption_ ? soundCaption_->text() : QString(); }

void AssetBrowser::setToolActions(const QList<QAction*>& actions) {
    // The Tools menu commands of this kind of asset (the same actions as the menu bar's), at the end of the dots menu.
    toolActions_ = actions;
    toolActions_.removeAll(nullptr);
    rebuildMenu();
}

QString AssetBrowser::previewCaption() const {
    // The whole caption (the label may show its first line elided; the tooltip keeps it whole).
    if (!previewCaption_) return {};
    return previewCaption_->toolTip().isEmpty() ? previewCaption_->text() : previewCaption_->toolTip();
}

bool AssetBrowser::previewHasImage() const { return preview_ && !preview_->pixmap().isNull(); }

void AssetBrowser::selectRow(int row) { view_->setCurrentIndex(proxy_->index(row, 0)); }

std::shared_ptr<const MenuPack> AssetBrowser::pack() const { return MenuPack::shared(packKind_); }

void AssetBrowser::reloadPack() {
    const auto pk = pack();
    packFacts_ = pk ? pk->facts() : QHash<QString, QString>();
    if (pk)
        for (auto [n, pill] : pk->pills().asKeyValueRange()) facts_.insert(n, pill);
    forgetThumbnails();  // the pictures are read again (from the new pack first)
    view_->viewport()->update();
    updateStrip();
}

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
        if (const QString pf = packFacts_.value(name); !pf.isEmpty()) facts << pf;
    } else if (kind_ == Kind::Sounds) {
        facts << (idx.data(Qt::UserRole + 2).toBool() ? "loop" : "one-shot");
        if (const QString f = facts_.value(name); !f.isEmpty()) facts += f.split(factSep(), Qt::SkipEmptyParts);
        if (const int d = idx.data(kDistRole).toInt(); d > 0) facts << QString("audible to %1 u").arg(thousands(d));
        if (!group.isEmpty()) facts << group;
        if (wave_ && playingName_ != name) static_cast<WaveView*>(wave_)->clear();  // the waveform is of what plays
    } else if (kind_ == Kind::Materials || kind_ == Kind::Models) {
        if (!group.isEmpty()) facts << group;
        if (const QString pf = packFacts_.value(name); !pf.isEmpty() && kind_ == Kind::Materials) facts << pf;
        const int uses = usageCount(name);
        facts << (uses ? QString("used %1 time%2 in this map").arg(uses).arg(uses == 1 ? "" : "s") : QString("not used in this map"));
        if (kind_ == Kind::Materials && name.contains('/')) facts << "techset family " + name.section('/', 0, 0);
        if (QPushButton* r = strip_->findChild<QPushButton*>("materialReplace")) r->setEnabled(uses > 0);
        if (QPushButton* f = strip_->findChild<QPushButton*>("materialSelectFaces")) f->setEnabled(uses > 0);
        if (kind_ == Kind::Materials && preview_) {
            // A larger picture than the tile's: the colour image again at the strip's size, else the tile's picture.
            QPixmap pm;
            if (const auto pk = pack())
                if (const QImage pv = pk->preview(name); !pv.isNull()) pm = QPixmap::fromImage(pv.scaled(112, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            if (const QString file = imageFiles_.value(name); pm.isNull() && !file.isEmpty())
                if (const QImage img = decodeImageFile(file, 112); !img.isNull()) pm = QPixmap::fromImage(img);
            if (pm.isNull()) pm = idx.data(Qt::DecorationRole).value<QIcon>().pixmap(112, 112);
            preview_->setPixmap(pm);
        }
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

}  // namespace iw8r::qt
