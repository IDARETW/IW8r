#include "find_dialog.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <QFile>

#include <QColor>
#include "unlit_baseline.h"
#include "stock_scripted_catalog.h"
#include "scriptable_catalog.h"
#include "prefab_browser.h"
#include "settings.h"
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "entity_defs.h"
#include "gameplay_checks.h"
#include "atmosphere.h"
#include "game_look.h"
#include "map_gametypes.h"
#include "leak_check.h"
#include "map_queries.h"
#include "key_replace.h"
#include "collision_world.h"
#include "light_budget.h"
#include "link_graph.h"
#include "spawn_analysis.h"
#include "sun_keys.h"
#include "trigger_templates.h"

namespace iw8r::qt {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

FindReplaceDialog::FindReplaceDialog(EditorContext* ctx, QWidget* parent) : QDialog(parent), ctx_(ctx) {
    setWindowTitle("Find / Replace Materials");
    setObjectName("findReplaceDialog");
    resize(620, 520);
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    find_ = new QLineEdit(this);
    find_->setPlaceholderText("material name (e.g. tm2co/me_curb_stone_01)");
    replace_ = new QLineEdit(qs(ctx->currentMaterial), this);
    form->addRow("Find", find_);
    form->addRow("Replace with", replace_);
    lay->addLayout(form);
    auto* opts = new QHBoxLayout();
    substring_ = new QCheckBox("Match part of the name", this);
    selectionOnly_ = new QCheckBox("Selected primitives only", this);
    opts->addWidget(substring_);
    opts->addWidget(selectionOnly_);
    opts->addStretch(1);
    lay->addLayout(opts);
    auto* buttons = new QHBoxLayout();
    auto* select = new QPushButton("Select Matches", this);
    auto* replaceBtn = new QPushButton("Replace All", this);
    auto* close = new QPushButton("Close", this);
    buttons->addWidget(select);
    buttons->addWidget(replaceBtn);
    buttons->addStretch(1);
    buttons->addWidget(close);
    lay->addLayout(buttons);
    info_ = new QLabel(this);
    info_->setObjectName("findReplaceInfo");
    lay->addWidget(info_);
    usage_ = new QTableWidget(0, 3, this);
    usage_->setHorizontalHeaderLabels({"Material in this map", "Faces", "Primitives"});
    usage_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    usage_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    usage_->setSelectionBehavior(QAbstractItemView::SelectRows);
    usage_->verticalHeader()->hide();
    lay->addWidget(usage_, 1);

    // The buttons also frame what they selected (the matches are often off screen); selectMatches() alone does not.
    connect(select, &QPushButton::clicked, this, [this] {
        if (selectMatches()) ctx_->frameSelection();
    });
    connect(replaceBtn, &QPushButton::clicked, this, [this] { replaceAll(); });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(usage_, &QTableWidget::cellClicked, this, [this](int row, int) { find_->setText(usage_->item(row, 0)->text()); });
    connect(usage_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        find_->setText(usage_->item(row, 0)->text());
        if (selectMatches()) ctx_->frameSelection();
    });
    connect(ctx_, &EditorContext::documentChanged, this, [this] {
        if (isVisible()) refreshUsage();
    });
    refreshUsage();
}

void FindReplaceDialog::setFind(const QString& m) { find_->setText(m); }
void FindReplaceDialog::setReplace(const QString& m) { replace_->setText(m); }
void FindReplaceDialog::setSubstring(bool on) { substring_->setChecked(on); }
void FindReplaceDialog::setSelectionOnly(bool on) { selectionOnly_->setChecked(on); }

void FindReplaceDialog::refreshUsage() {
    const auto usage = materialUsage(ctx_->doc.map());
    usage_->setRowCount(int(usage.size()));
    for (int i = 0; i < int(usage.size()); ++i) {
        usage_->setItem(i, 0, new QTableWidgetItem(qs(usage[size_t(i)].material)));
        auto* f = new QTableWidgetItem;
        f->setData(Qt::DisplayRole, qulonglong(usage[size_t(i)].faces));
        auto* p = new QTableWidgetItem;
        p->setData(Qt::DisplayRole, qulonglong(usage[size_t(i)].primitives));
        usage_->setItem(i, 1, f);
        usage_->setItem(i, 2, p);
    }
}

KeyReplaceDialog::KeyReplaceDialog(EditorContext* ctx, QWidget* parent) : QDialog(parent), ctx_(ctx) {
    setWindowTitle("Find / Replace Key Values");
    setObjectName("keyReplaceDialog");
    resize(620, 520);
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    key_ = new QLineEdit("model", this);
    key_->setToolTip("Entity key: model replaces placed models; any other key (targetname, script_noteworthy, ...) works too");
    find_ = new QLineEdit(this);
    replace_ = new QLineEdit(this);
    form->addRow("Key", key_);
    form->addRow("Find value", find_);
    form->addRow("Replace with", replace_);
    lay->addLayout(form);
    auto* opts = new QHBoxLayout();
    substring_ = new QCheckBox("Match part of the value", this);
    selectionOnly_ = new QCheckBox("Selected entities only", this);
    opts->addWidget(substring_);
    opts->addWidget(selectionOnly_);
    opts->addStretch(1);
    lay->addLayout(opts);
    auto* buttons = new QHBoxLayout();
    auto* select = new QPushButton("Select Matches", this);
    auto* replaceBtn = new QPushButton("Replace All", this);
    auto* close = new QPushButton("Close", this);
    buttons->addWidget(select);
    buttons->addWidget(replaceBtn);
    buttons->addStretch(1);
    buttons->addWidget(close);
    lay->addLayout(buttons);
    info_ = new QLabel(this);
    info_->setObjectName("keyReplaceInfo");
    lay->addWidget(info_);
    usage_ = new QTableWidget(0, 2, this);
    usage_->setHorizontalHeaderLabels({"Value in this map", "Entities"});
    usage_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    usage_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    usage_->setSelectionBehavior(QAbstractItemView::SelectRows);
    usage_->verticalHeader()->hide();
    lay->addWidget(usage_, 1);
    // As in Find / Replace Materials, the buttons frame what they selected.
    connect(select, &QPushButton::clicked, this, [this] {
        if (selectMatches()) ctx_->frameSelection();
    });
    connect(replaceBtn, &QPushButton::clicked, this, [this] { replaceAll(); });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(key_, &QLineEdit::editingFinished, this, &KeyReplaceDialog::refreshUsage);
    connect(usage_, &QTableWidget::cellClicked, this, [this](int row, int) { find_->setText(usage_->item(row, 0)->text()); });
    connect(usage_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        find_->setText(usage_->item(row, 0)->text());
        if (selectMatches()) ctx_->frameSelection();
    });
    connect(ctx_, &EditorContext::documentChanged, this, [this] {
        if (isVisible()) refreshUsage();
    });
    refreshUsage();
}

void KeyReplaceDialog::set(const QString& key, const QString& find, const QString& replace, bool substring, bool selectionOnly) {
    key_->setText(key);
    find_->setText(find);
    replace_->setText(replace);
    substring_->setChecked(substring);
    selectionOnly_->setChecked(selectionOnly);
    refreshUsage();
}

void KeyReplaceDialog::refreshUsage() {
    const auto usage = keyValueUsage(ctx_->doc.map(), key_->text().trimmed().toStdString());
    usage_->setRowCount(int(usage.size()));
    for (int i = 0; i < int(usage.size()); ++i) {
        usage_->setItem(i, 0, new QTableWidgetItem(qs(usage[size_t(i)].first)));
        usage_->setItem(i, 1, new QTableWidgetItem(QString::number(usage[size_t(i)].second)));
    }
}

size_t KeyReplaceDialog::selectMatches() {
    // An empty key or value matches nothing; without this the selection was cleared with no word why.
    if (key_->text().trimmed().isEmpty() || find_->text().trimmed().isEmpty()) {
        info_->setText("Type the key and the value to find first (or click a value in the list).");
        return 0;
    }
    const auto ids = entitiesWithValue(ctx_->doc.map(), key_->text().trimmed().toStdString(), find_->text().trimmed().toStdString(),
                                       substring_->isChecked());
    ctx_->doc.deselectAll();
    for (Id id : ids) ctx_->doc.select({id, kNoId}, true);
    info_->setText(QString("%1 entit%2 with %3 = %4").arg(ids.size()).arg(ids.size() == 1 ? "y" : "ies", key_->text(), find_->text()));
    ctx_->documentEdited(QString("Selected %1 entities with %2 %3").arg(ids.size()).arg(key_->text(), find_->text()));
    return ids.size();
}

size_t KeyReplaceDialog::replaceAll() {
    // An empty replacement is allowed here (it clears the value, or cuts the found part out of it), except for classname.
    if (key_->text().trimmed().isEmpty() || find_->text().trimmed().isEmpty()) {
        info_->setText("Type the key and the value to find first (or click a value in the list).");
        return 0;
    }
    if (key_->text().trimmed() == "classname" && replace_->text().trimmed().isEmpty()) {
        info_->setText("Type the replacement text first: an entity needs a classname.");
        return 0;
    }
    const size_t n = replaceKeyValues(ctx_->doc, key_->text().trimmed().toStdString(), find_->text().trimmed().toStdString(),
                                      replace_->text().trimmed().toStdString(), substring_->isChecked(), selectionOnly_->isChecked());
    info_->setText(QString("Replaced %1 value(s) (one undo step)").arg(n));
    if (n) ctx_->documentEdited(QString("Replaced %1 %2 value(s)").arg(n).arg(key_->text()));
    return n;
}

size_t FindReplaceDialog::selectMatches() {
    const std::string m = find_->text().trimmed().toStdString();
    if (m.empty()) {
        info_->setText("Type the material to find first (or click one in the list).");
        return 0;
    }
    const auto refs = findByMaterial(ctx_->doc.map(), m, substring_->isChecked());
    ctx_->doc.deselectAll();
    for (const auto& r : refs) ctx_->doc.select(r, true);
    info_->setText(QString("%1 primitive(s) use %2").arg(refs.size()).arg(find_->text()));
    ctx_->documentEdited(QString("Selected %1 primitive(s) with %2").arg(refs.size()).arg(find_->text()));
    return refs.size();
}

size_t FindReplaceDialog::replaceAll() {
    const std::string f = find_->text().trimmed().toStdString(), r = replace_->text().trimmed().toStdString();
    // Every face needs a material, so an empty replacement is never valid (replaceMaterial refuses it): say why
    // nothing happened instead of leaving the last result on screen.
    if (f.empty()) {
        info_->setText("Type the material to find first (or click one in the list).");
        return 0;
    }
    if (r.empty()) {
        info_->setText("Type the replacement text first: every face needs a material.");
        return 0;
    }
    if (r.find_first_of(" \t\r\n") != std::string::npos) {
        info_->setText("A material name has no spaces: check the replacement text.");
        return 0;
    }
    const size_t n = replaceMaterial(ctx_->doc, f, r, substring_->isChecked(), selectionOnly_->isChecked());
    info_->setText(QString("Replaced %1 face(s)").arg(n));
    if (n) ctx_->documentEdited(QString("Replaced %1 face(s) of %2 with %3 (one undo step)").arg(n).arg(find_->text(), replace_->text()));
    return n;
}

CheckMapDialog::CheckMapDialog(EditorContext* ctx, QWidget* parent) : QDialog(parent), ctx_(ctx) {
    setWindowTitle("Check Map");
    setObjectName("checkMapDialog");
    resize(640, 420);
    auto* lay = new QVBoxLayout(this);
    info_ = new QLabel(this);
    info_->setWordWrap(true);
    lay->addWidget(info_);
    list_ = new QListWidget(this);
    lay->addWidget(list_, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* again = buttons->addButton("Check Again", QDialogButtonBox::ActionRole);
    auto* fixSel = buttons->addButton("Fix Selected", QDialogButtonBox::ActionRole);
    auto* fixAllB = buttons->addButton("Fix All", QDialogButtonBox::ActionRole);
    fixSel->setObjectName("checkMapFixSelected");
    fixAllB->setObjectName("checkMapFixAll");
    fixSel->setToolTip("Apply the automatic fix of the selected [fix] lines (one undo step)");
    fixAllB->setToolTip("Apply every automatic fix (one undo step): spawn angles, spawns onto the floor, dangling targets, broken primitives");
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lay->addWidget(buttons);
    connect(again, &QPushButton::clicked, this, &CheckMapDialog::recheck);
    connect(fixSel, &QPushButton::clicked, this, [this] { fixSelected(); });
    connect(fixAllB, &QPushButton::clicked, this, [this] { fixAll(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    recheck();
}

QStringList CheckMapDialog::itemTexts() const {
    QStringList out;
    for (int i = 0; i < list_->count(); ++i) out << list_->item(i)->text();
    return out;
}

int CheckMapDialog::problemCount() const { return list_->count(); }

namespace {
const EntityDefs* g_defs = nullptr;
}
void setCheckMapEntityDefs(const EntityDefs* defs) { g_defs = defs; }
const EntityDefs* checkMapEntityDefs() { return g_defs; }

size_t CheckMapDialog::fixAll() {
    if (fixes_.empty()) return 0;
    const size_t n = applyDoctorFixes(ctx_->doc, fixes_, "Map Doctor: fix all");
    if (n) ctx_->documentEdited(QString("Map Doctor fixed %1 finding(s) (one undo step)").arg(n));
    recheck();
    return n;
}

size_t CheckMapDialog::fixSelected() {
    std::vector<DoctorFix> pick;
    for (const QListWidgetItem* it : list_->selectedItems()) {
        const int row = list_->row(it);
        if (row >= 0 && size_t(row) < fixes_.size()) pick.push_back(fixes_[size_t(row)]);
    }
    if (pick.empty()) return 0;
    const size_t n = applyDoctorFixes(ctx_->doc, pick, "Map Doctor: fix");
    if (n) ctx_->documentEdited(QString("Map Doctor fixed %1 finding(s) (one undo step)").arg(n));
    recheck();
    return n;
}

QStringList checkMapLines(EditorContext* ctx_, std::vector<DoctorFix>& fixes_, QString* summary) {
    QStringList out;
    // Map Doctor (9.12): fixable findings first, one row each, in the same order as fixes_.
    fixes_ = diagnoseMap(ctx_->doc.map());
    for (const DoctorFix& f : fixes_) out << "[fix] " + qs(f.description);
    const auto problems = mapProblems(ctx_->doc.map());
    for (const auto& p : problems) out << (qs(p));
    const auto gameplay = gameplayIssues(ctx_->doc.map());
    for (const auto& g : gameplay)
        out << (QString("Gameplay: %1 (%2 at %3 %4 %5)")
                           .arg(qs(g.message), qs(g.classname))
                           .arg(g.at.x)
                           .arg(g.at.y)
                           .arg(g.at.z));
    size_t defIssues = 0;
    if (g_defs)
        for (size_t i = 0; i < ctx_->doc.map().entities.size(); ++i)
            for (const auto& msg : g_defs->validate(ctx_->doc.map().entities[i])) {
                out << (QString("Entity %1 (%2): %3").arg(i).arg(qs(ctx_->doc.map().entities[i].classname()), qs(msg)));
                ++defIssues;
            }
    // Links (6.25), light budget (5.15) and spawn sight lines / spacing (6.18).
    size_t extra = 0;
    {
        const MapFile& m = ctx_->doc.map();
        for (const LinkIssue& i : checkLinks(m, buildLinkGraph(m))) {
            out << (QString("Links%1: %2 (entity %3)").arg(i.error ? "" : " (warning)", qs(i.message)).arg(i.entity));
            ++extra;
        }
        for (const auto& e : m.entities)
            if (e.isWorldspawn()) {
                for (const std::string& w : checkSunKeys(e)) {
                    out << ("Sun (warning): " + qs(w));
                    ++extra;
                }
                break;
            }
        {
            // 6.13 (research 53): entities shaped like a stock scripted object, and whether its stock script runs in a
            // custom map (a reading of the stock scripts, not a game test).
            static const ScriptedObjectCatalog cat = [] {
                ScriptedObjectCatalog c;
                QDir d(QCoreApplication::applicationDirPath());
                for (int up = 0; up < 4; ++up, d.cdUp())
                    if (QFileInfo::exists(d.filePath("data/asset_catalog/iw8_scripted_objects.tsv"))) {
                        std::string err;
                        c.load(d.filePath("data/asset_catalog/iw8_scripted_objects.tsv").toStdString(), &err);
                        break;
                    }
                return c;
            }();
            std::map<std::string, int> seen;  // kind -> entities, so one line per kind
            std::map<std::string, const ScriptedObjectKind*> rowOf;
            for (const auto& e : m.entities) {
                if (e.isWorldspawn()) continue;
                std::map<std::string, std::string> keys;
                keys["classname"] = e.classname();
                for (const char* k : {"targetname", "script_noteworthy", "script_gameobjectname", "script_label", "script_bombmode_original",
                                      "script_linkname", "script_exploder", "target"})
                    if (const std::string* v = e.value(k)) keys[k] = *v;
                const auto hits = cat.match(keys);
                if (hits.empty()) continue;
                ++seen[hits.front()->kind];
                rowOf[hits.front()->kind] = hits.front();
            }
            for (const auto& [kind, n] : seen) {
                const ScriptedObjectKind* k = rowOf[kind];
                const QString works = k->worksInCustom == "yes" ? "runs in a custom map"
                                      : k->worksInCustom == "no" ? "does NOT run in a custom map"
                                                                 : "unknown in a custom map (game-mode objects work only in their mode)";
                out << (QString("Stock scripted objects (note): %1 x %2 (%3): %4; handled by %5")
                                   .arg(n).arg(qs(kind), qs(k->classname), works, qs(k->handledBy.empty() ? "?" : k->handledBy)));
                ++extra;
            }
        }
        for (const UnlitBaselineIssue& i : checkUnlitBaseline(m)) {  // 1.8 (research 55): no bake, so the sun is the light
            out << (QString("Lighting%1: %2").arg(i.error ? "" : " (warning)", qs(i.message)));
            ++extra;
        }
        {
            // 6.14: scriptable entities the build would leave out (research 86): unknown definition, a definition that
            // lives only in stock map zones, or one whose build numbers are not all known.
            const ScriptableCatalog& catalog = ScriptableCatalog::shared(QCoreApplication::applicationDirPath().toStdString());
            for (size_t i = 0; i < m.entities.size(); ++i) {
                const Entity& e = m.entities[i];
                if (e.classname() != "scriptable") continue;
                const std::string* name = e.value("scriptable");
                if (!name || name->empty()) name = e.value("definition");
                QString why;
                if (!name || name->empty()) why = "has no scriptable key (the stock definition's name)";
                else if (catalog.size() == 0) why = "cannot be checked: data/asset_catalog/iw8_scriptables.tsv was not found";
                else if (const ScriptableDefinition* def = catalog.find(*name); !def)
                    why = QString("names '%1', which is not a stock scriptable definition").arg(qs(*name));
                else if (!def->verified)
                    why = QString("names '%1', whose build numbers are not all known").arg(qs(def->name));
                else if (!def->alwaysLoaded && !def->carryable())
                    why = QString("names '%1', which no stock map zone holds (%2)").arg(qs(def->name), qs(def->zones));
                else if (!def->alwaysLoaded && !PrefabBrowser::realScriptables())
                    why = QString("names '%1', which lives only in stock map zones (%2); turn on Edit > Preferences > Real stock "
                                  "scripted objects to carry it into the map")
                              .arg(qs(def->name), qs(def->zones));
                if (why.isEmpty()) continue;
                out << (QString("Scriptables: entity %1 %2; it is left out of the build").arg(i).arg(why));
                ++extra;
            }
        }
        {
            // 6.15: placed models (dynamic_prop clutter included) whose stock model the availability audit marks missing
            // in Replay 1.20 (data/asset_catalog/iw8_models_status.tsv); they would build without the model.
            static const std::map<std::string, std::string> missingModels = [] {
                std::map<std::string, std::string> out;
                QDir d(QCoreApplication::applicationDirPath());
                for (int up = 0; up < 4; ++up, d.cdUp())
                    if (QFile f(d.filePath("data/asset_catalog/iw8_models_status.tsv")); f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        while (!f.atEnd()) {
                            const QStringList c = QString::fromUtf8(f.readLine()).trimmed().split('\t');
                            if (c.size() >= 2 && c[1] == "missing") out[c[0].toStdString()] = c.size() >= 3 ? c[2].toStdString() : std::string();
                        }
                        break;
                    }
                return out;
            }();
            for (size_t i = 0; i < m.entities.size(); ++i) {
                const Entity& e = m.entities[i];
                const std::string cls = e.classname();
                if (cls != "misc_model" && cls != "script_model" && cls != "dynamic_prop") continue;
                const std::string* model = e.value("model");
                if (!model || model->empty()) {
                    if (cls == "dynamic_prop") {
                        out << (QString("Models: entity %1 (dynamic_prop) has no model").arg(i));
                        ++extra;
                    }
                    continue;
                }
                const auto it = missingModels.find(*model);
                if (it == missingModels.end()) continue;
                out << (QString("Models: entity %1 (%2) uses %3, which is not available in Replay (%4); it builds without the model")
                                   .arg(i).arg(qs(cls), qs(*model), qs(it->second.empty() ? std::string("export failed") : it->second)));
                ++extra;
            }
        }
        // Which game modes the game will offer for this map (written to map.json by the build), and what is missing
        // for a mode the mapper has started on.
        for (const std::string& w : mapGameTypes(m).notes) {
            out << ("Game modes (note): " + qs(w));
            ++extra;
        }
        for (const std::string& w : killstreakNotes(m)) {
            out << ("Killstreaks (note): " + qs(w));
            ++extra;
        }
        for (const auto& e : m.entities) {
            if (!e.isWorldspawn()) continue;
            // The build's sky check (core skyHazeWarning): the haze under iw8r_vision, its ground at the spawns' median
            // height, as the build places it.
            std::vector<double> heights;
            for (const auto& s : m.entities)
                if (s.classname().find("spawn") != std::string::npos)
                    if (auto o = s.origin()) heights.push_back(o->z);
            double groundZ = 0;
            if (!heights.empty()) {
                std::nth_element(heights.begin(), heights.begin() + long(heights.size() / 2), heights.end());
                groundZ = std::round(heights[heights.size() / 2]);
            }
            const Atmosphere atmo = readAtmosphere(e);
            const std::string haze = atmo.haze ? hazeVisionText(*atmo.haze, 1.0, Vec3{0, 0, 1}, groundZ) : std::string();
            const std::string* vision = e.value(kVisionKey);
            if (const std::string w = skyHazeWarning(mergeVisionText(haze, vision ? *vision : std::string()), groundZ); !w.empty()) {
                out << ("Sky (warning): " + qs(w));
                ++extra;
            }
            break;
        }
        for (const std::string& w : checkCallouts(m)) {
            out << ("Callouts (warning): " + qs(w));
            ++extra;
        }
        for (const LightBudgetIssue& i : checkLightBudget(collectLights(m))) {
            out << (QString("Lights%1: %2").arg(i.error ? "" : " (warning)", qs(i.message)));
            ++extra;
        }
        const auto spawns = collectSpawns(m);
        if (spawns.size() >= 2) {
            CollisionWorld world;
            world.build(m);
            for (const std::string& w : spawnWarnings(spawns, analyseSpawns(spawns, world))) {
                out << ("Spawns (warning): " + qs(w));
                ++extra;
            }
        }
    }
    const LeakResult leak = checkLeak(ctx_->doc.map());
    if (!leak.error.empty()) out << ("Leak check: " + qs(leak.error));
    else if (!leak.sealed)
        out << (QString("Leak: %1 (entity %2) reaches the void; Tools > Leak Check draws the path").arg(qs(leak.classname)).arg(leak.leakingEntity));
    // What the IW8 build will do with this map (the compiler's own warnings, without writing anything).
    int compileNotes = 0;
    {
        const MapFile& m = ctx_->doc.map();
        bool allies = false, axis = false;
        int tdm = 0;
        for (const auto& e : m.entities) {
            const std::string c = e.classname();
            allies |= c == "mp_tdm_spawn_allies_start";
            axis |= c == "mp_tdm_spawn_axis_start";
            tdm += c == "mp_tdm_spawn";
        }
        auto note = [&](const QString& s) {
            out << ("IW8 build: " + s);
            ++compileNotes;
        };
        if (!allies) note("no mp_tdm_spawn_allies_start (needed for a playable match)");
        if (!axis) note("no mp_tdm_spawn_axis_start (needed for a playable match)");
        if (tdm < 2) note(QString("%1 mp_tdm_spawn (two or more needed)").arg(tdm));
    }
    const size_t total = problems.size() + gameplay.size() + defIssues + extra + (leak.sealed ? 0 : 1);
    if (summary) *summary = (!total && !compileNotes && fixes_.empty()
                       ? QString("No problems found.")
                       : QString("%1 problem(s) (map, gameplay, links, lights, spawns, leak), %2 IW8 build note(s); %3 with an automatic fix "
                                 "([fix] lines: Fix Selected or Fix All).")
                             .arg(total).arg(compileNotes).arg(fixes_.size()));
    return out;
}

void CheckMapDialog::recheck() {
    list_->clear();
    QString summary;
    for (const QString& l : checkMapLines(ctx_, fixes_, &summary)) {
        auto* item = new QListWidgetItem(l);
        if (l.startsWith("[fix] ")) item->setForeground(QColor(120, 200, 255));
        list_->addItem(item);
    }
    info_->setText(summary);
}

}  // namespace iw8r::qt
