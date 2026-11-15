// MainWindow: game mode presets (Map > Game Modes; docs/research/87_warzone_survey.md, core game_modes.*). Kept out of
// main_window.cpp, which is at the compiler's section limit.
#include "main_window.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QCheckBox>

#include "devmap_launch.h"
#include "game_modes.h"
#include "gametype_script.h"
#include "map_gametypes.h"

namespace iw8r::qt {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
// IW8R_GAME_MODES_DIR puts the user's presets elsewhere: the GUI test saves and deletes in a temporary folder, never
// in the owner's own list.
QString userModesDir() {
    const QString over = qEnvironmentVariable("IW8R_GAME_MODES_DIR");
    return !over.isEmpty() ? over : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/game_modes";
}
// A question whose button names what it does. Cancel is the default, so Enter does not delete or overwrite.
bool askFirst(QWidget* parent, const QString& text, const QString& action, QMessageBox::ButtonRole role) {
    QMessageBox box(QMessageBox::Question, "Game Modes", text, QMessageBox::Cancel, parent);
    box.setObjectName("gameModesConfirm");
    QPushButton* go = box.addButton(action, role);
    box.setDefaultButton(QMessageBox::Cancel);
    box.setEscapeButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == go;
}
}  // namespace

bool MainWindow::bakeGameMode(const GameModePreset& preset) {
    const Entity* world = nullptr;
    for (const Entity& e : ctx_.doc.map().entities)
        if (e.isWorldspawn()) {
            world = &e;
            break;
        }
    if (!world) return false;
    const Id id = world->id;
    ctx_.doc.beginGroup("Game Mode Settings");
    const std::string dvars = modeDvarsText(preset.dvars), br = modeBrText(preset);
    if (dvars.empty()) ctx_.doc.removeKey(id, "iw8r_mode_dvars");
    else ctx_.doc.setKeyValue(id, "iw8r_mode_dvars", dvars);
    if (br.empty()) ctx_.doc.removeKey(id, "iw8r_br");
    else ctx_.doc.setKeyValue(id, "iw8r_br", br);
    const std::string rules = modeRulesText(preset.rules);
    if (rules.empty()) ctx_.doc.removeKey(id, "iw8r_mode_rules");
    else ctx_.doc.setKeyValue(id, "iw8r_mode_rules", rules);
    // A hand-written game type list gets the preset's game type too; a detected list finds it by itself.
    if (const Entity* now = ctx_.doc.findEntity(id))
        if (const std::string* list = now->value("iw8r_gametypes")) {
            std::string joined = *list + " " + preset.gametype, clean;
            if (normalizeGameTypeList(joined, clean) && clean != *list) ctx_.doc.setKeyValue(id, "iw8r_gametypes", clean);
        }
    ctx_.doc.endGroup();
    ctx_.documentEdited(QString("Game mode '%1' is carried by the map: %2 dvar(s)%3 are set by the map script at start%4")
                            .arg(qs(preset.title)).arg(preset.dvars.size()).arg(br.empty() ? QString() : " and the Warzone switches (" + qs(br) + ")")
                            .arg(rules.empty() ? QString() : "; rules at every spawn: " + qs(rules)));
    return true;
}

QString MainWindow::newGameTypeScript(const QString& name, QString* err) {
    const std::string n = gameTypeNameOfScript(name.toStdString() + ".gsc");
    if (n.empty() || n != name.toStdString()) {
        if (err) *err = "The name must be 1 to 11 characters of a-z, 0-9 and _.";
        return {};
    }
    if (ctx_.doc.path().empty()) {
        if (err) *err = "Save the map first: the script is written beside it.";
        return {};
    }
    const QString dir = QFileInfo(qs(ctx_.doc.path())).absolutePath();
    const QString rel = "gametypes/" + name + ".gsc";
    const QString path = dir + "/" + rel;
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(dir + "/gametypes");
        QFile f(path);
        const std::string text = gameTypeScriptTemplate(n);
        if (!f.open(QIODevice::WriteOnly) || f.write(text.data(), qint64(text.size())) != qint64(text.size())) {
            if (err) *err = "Cannot write " + path;
            return {};
        }
    }
    for (const Entity& e : ctx_.doc.map().entities)
        if (e.isWorldspawn()) {
            ctx_.doc.setKeyValue(e.id, kGameTypeScriptKey, rel.toStdString());
            ctx_.documentEdited("Game type script " + rel + " is carried by the map (worldspawn " + QString(kGameTypeScriptKey) + ")");
            return path;
        }
    if (err) *err = "The open document has no worldspawn.";
    return {};
}

int MainWindow::insertWarzoneStart() {
    Vec3 c;
    c[ctx_.axisH()] = ctx_.gridCenterH;
    c[ctx_.axisV()] = ctx_.gridCenterV;
    const auto vec = [](const Vec3& v) { return QString("%1 %2 %3").arg(v.x).arg(v.y).arg(v.z).toStdString(); };
    ctx_.doc.beginGroup("Warzone Start");
    int added = 0;
    // The br script takes its starting spawns from mp_dm_spawn_start and its prematch areas from script_struct
    // entities named br_prematch_insertion_point (research 87, section 6). Eight spawns on a ring, facing outward.
    for (int i = 0; i < 8; ++i) {
        const double a = i * 3.14159265358979323846 / 4.0;
        const Vec3 o{c.x + std::cos(a) * 96.0, c.y + std::sin(a) * 96.0, c.z + 8.0};
        ctx_.doc.addEntity({{"classname", "mp_dm_spawn_start"}, {"origin", vec(o)}, {"angles", "0 " + std::to_string(i * 45) + " 0"}}, {},
                           "Warzone start spawn");
        ++added;
    }
    ctx_.doc.addEntity({{"classname", "script_struct"}, {"targetname", "br_prematch_insertion_point"}, {"origin", vec(Vec3{c.x, c.y, c.z + 64.0})},
                        {"radius", "512"}}, {}, "Warzone prematch area");
    ++added;
    ctx_.doc.endGroup();
    ctx_.documentEdited(QString("Warzone start: %1 entities added around the 2D view centre (8 mp_dm_spawn_start, 1 prematch area); "
                                "move the spawns onto the floor").arg(added));
    return added;
}

// The Game Modes dialog as a web form (plan 3.3): a preset and an action.
QJsonObject MainWindow::webGameModesForm() const {
    const std::vector<GameModePreset> modes = loadGameModes(userModesDir().toStdString(), nullptr);
    const QString f5 = DevmapRunner::modePresetSetting();
    QJsonArray ids, labels;
    QString current;
    for (const GameModePreset& m : modes) {
        ids.append(qs(m.id));
        labels.append(qs(m.title) + (m.builtin ? "" : "  (mine)") + (qs(m.id) == f5 ? "  [F5]" : "") + "  -  " + qs(m.gametype) +
                      (m.dvars.empty() ? QString() : QString(", %1 dvar(s)").arg(m.dvars.size())));
        if (qs(m.id) == f5) current = f5;
    }
    if (current.isEmpty() && !modes.empty()) current = qs(modes.front().id);
    const QJsonArray actions{"carry", "f5", "warzoneStart"};
    const QJsonArray actionLabels{"Carry in this map (the map's script sets the mode at every start)", "Use for F5 (sent when the editor starts the match)",
                                  "Insert the Warzone start entities into the map"};
    return QJsonObject{{"id", "gameModes"}, {"title", "Game Modes"}, {"sendAll", true},
                       {"fields", QJsonArray{QJsonObject{{"key", "preset"}, {"label", "Mode"}, {"type", "choice"}, {"value", current}, {"choices", ids},
                                                         {"choiceLabels", labels}, {"help", "A mode is a game type script plus dvars. Your own presets are made in the editor's Game Modes dialog"}},
                                             QJsonObject{{"key", "action"}, {"label", "Do"}, {"type", "choice"}, {"value", "carry"}, {"choices", actions},
                                                         {"choiceLabels", actionLabels}, {"help", "Not seen in game"}}}}};
}

bool MainWindow::applyWebGameModes(const QJsonObject& values, QString* why) {
    const auto no = [&](const QString& text) {
        if (why) *why = text;
        return false;
    };
    const QString action = values.value("action").toString();
    if (action == "warzoneStart") {
        const int n = insertWarzoneStart();
        return n >= 0 ? true : no("the Warzone start entities could not be inserted");
    }
    const QString id = values.value("preset").toString();
    for (const GameModePreset& m : loadGameModes(userModesDir().toStdString(), nullptr)) {
        if (qs(m.id) != id) continue;
        if (action == "f5") {
            DevmapRunner::setModePreset(qs(m.id), qs(m.gametype), qs(modeDvarsText(m.dvars)));
            ctx_.setStatus(QString("F5 starts '%1' (game type %2, %3 dvar(s) sent before the start)").arg(qs(m.title), qs(m.gametype)).arg(m.dvars.size()));
            return true;
        }
        if (action == "carry") return bakeGameMode(m) ? true : no("the map has no worldspawn");
        return no("the action is carry, f5 or warzoneStart");
    }
    return no("there is no game mode " + id);
}

void MainWindow::showGameModes() {
    QDialog dlg(this);
    dlg.setWindowTitle("Game Modes");
    dlg.setObjectName("gameModesDialog");
    dlg.resize(860, 560);
    auto* root = new QHBoxLayout(&dlg);
    auto* list = new QListWidget(&dlg);
    list->setObjectName("gameModesList");
    list->setMinimumWidth(300);
    root->addWidget(list, 1);
    auto* side = new QVBoxLayout;
    root->addLayout(side, 2);
    auto* form = new QFormLayout;
    side->addLayout(form);
    auto* idEdit = new QLineEdit(&dlg);
    idEdit->setObjectName("gameModeId");
    auto* titleEdit = new QLineEdit(&dlg);
    auto* typeEdit = new QComboBox(&dlg);
    typeEdit->setEditable(true);
    typeEdit->addItems({"war", "dm", "dom", "sd", "ctf", "koth", "conf", "br"});
    auto* dvarEdit = new QPlainTextEdit(&dlg);
    dvarEdit->setObjectName("gameModeDvars");
    dvarEdit->setPlaceholderText("one per line: name=value   (for example scr_br_teamsize=3)");
    dvarEdit->setMinimumHeight(150);
    auto* descEdit = new QPlainTextEdit(&dlg);
    descEdit->setMaximumHeight(70);
    auto* noCircle = new QCheckBox("no circle", &dlg);
    auto* noGulag = new QCheckBox("no gulag", &dlg);
    auto* skipDrop = new QCheckBox("no drop from the plane", &dlg);
    auto* brRow = new QHBoxLayout;
    brRow->addWidget(noCircle);
    brRow->addWidget(noGulag);
    brRow->addWidget(skipDrop);
    brRow->addStretch(1);
    form->addRow("Id (a-z, 0-9, _)", idEdit);
    form->addRow("Title", titleEdit);
    form->addRow("Game type script", typeEdit);
    form->addRow("Dvars", dvarEdit);
    form->addRow("Warzone on a small map", brRow);
    auto* rulesEdit = new QLineEdit(&dlg);
    rulesEdit->setObjectName("gameModeRules");
    rulesEdit->setPlaceholderText("health=50; speed=1.2; weapon=iw8_pi_golf21_mp; ammo=infinite   (applied at every spawn)");
    form->addRow("Rules", rulesEdit);
    form->addRow("Notes", descEdit);
    auto* info = new QLabel(&dlg);
    info->setWordWrap(true);
    info->setStyleSheet("color: gray");
    info->setText("A mode is a game type script plus dvars: Warzone Trios, Quads and Blood Money are all the script \"br\" with different "
                  "dvars. A private match applies none by itself, so: \"Use for F5\" sends them when the editor starts the match, and "
                  "\"Carry in this map\" makes the map's own script set them at every start. Rules change how the game type plays "
                  "(the map's script runs after the game type's own start, research 91); they work only carried in the map. Not seen in game.");
    side->addWidget(info);
    auto* buttons = new QHBoxLayout;
    side->addLayout(buttons);
    auto* useF5 = new QPushButton("Use for F5", &dlg);
    auto* bake = new QPushButton("Carry in this map", &dlg);
    auto* start = new QPushButton("Insert Warzone start entities", &dlg);
    auto* newType = new QPushButton("New game type script...", &dlg);
    newType->setToolTip("A game type of your own under a new name, carried in the map (research 91 / 92): writes a "
                        "template beside the map and points worldspawn iw8r_gametype_script at it");
    auto* save = new QPushButton("Save as my preset", &dlg);
    save->setObjectName("gameModeSave");
    auto* del = new QPushButton("Delete my preset", &dlg);
    del->setObjectName("gameModeDelete");
    for (QPushButton* b : {useF5, bake, start, newType, save, del}) buttons->addWidget(b);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    side->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto modes = std::make_shared<std::vector<GameModePreset>>();
    const auto reload = [modes, list, this](const QString& select) {
        std::vector<std::string> notes;
        *modes = loadGameModes(userModesDir().toStdString(), &notes);
        for (const std::string& n : notes) ctx_.setStatus("Game Modes: " + qs(n));
        list->clear();
        const QString f5 = DevmapRunner::modePresetSetting();
        int row = 0;
        for (size_t i = 0; i < modes->size(); ++i) {
            const GameModePreset& m = (*modes)[i];
            auto* item = new QListWidgetItem(qs(m.title) + (m.builtin ? "" : "  (mine)") + (qs(m.id) == f5 ? "  [F5]" : ""), list);
            item->setData(Qt::UserRole, qs(m.id));
            if (qs(m.id) == select) row = int(i);
        }
        list->setCurrentRow(row);
    };
    const auto fromFields = [=](GameModePreset& p, QString* err) {
        p = {};
        p.id = idEdit->text().trimmed().toStdString();
        p.title = titleEdit->text().trimmed().toStdString();
        p.gametype = typeEdit->currentText().trimmed().toLower().toStdString();
        p.description = descEdit->toPlainText().trimmed().toStdString();
        p.brNoCircle = noCircle->isChecked(), p.brNoGulag = noGulag->isChecked(), p.brSkipDrop = skipDrop->isChecked();
        std::vector<std::string> notes;
        p.dvars = parseModeDvars(dvarEdit->toPlainText().replace('\n', ';').toStdString(), &notes);
        p.rules = parseModeRules(rulesEdit->text().toStdString(), &notes);
        // Through the JSON form: the same checks a preset file gets.
        GameModePreset checked;
        std::string perr;
        if (!parseGameModePreset(gameModePresetJson(p), checked, &perr)) {
            if (err) *err = qs(perr);
            return false;
        }
        if (!notes.empty()) {
            if (err) *err = qs(notes.front());
            return false;
        }
        p = checked;
        return true;
    };
    connect(list, &QListWidget::currentRowChanged, &dlg, [=](int row) {
        if (row < 0 || row >= int(modes->size())) return;
        const GameModePreset& m = (*modes)[size_t(row)];
        idEdit->setText(qs(m.id));
        titleEdit->setText(qs(m.title));
        typeEdit->setCurrentText(qs(m.gametype));
        QStringList lines;
        for (const auto& [name, value] : m.dvars) lines << qs(name) + "=" + qs(value);
        dvarEdit->setPlainText(lines.join('\n'));
        descEdit->setPlainText(qs(m.description));
        noCircle->setChecked(m.brNoCircle);
        noGulag->setChecked(m.brNoGulag);
        skipDrop->setChecked(m.brSkipDrop);
        rulesEdit->setText(qs(modeRulesText(m.rules)));
        del->setEnabled(!m.builtin);
    });
    connect(useF5, &QPushButton::clicked, &dlg, [=, this] {
        GameModePreset p;
        QString err;
        if (!fromFields(p, &err)) return void(QMessageBox::warning(this, "Game Modes", err));
        DevmapRunner::setModePreset(qs(p.id), qs(p.gametype), qs(modeDvarsText(p.dvars)));
        ctx_.setStatus(QString("F5 starts '%1' (game type %2, %3 dvar(s) sent before the start)").arg(qs(p.title), qs(p.gametype)).arg(p.dvars.size()));
        reload(qs(p.id));
    });
    connect(bake, &QPushButton::clicked, &dlg, [=, this] {
        GameModePreset p;
        QString err;
        if (!fromFields(p, &err)) return void(QMessageBox::warning(this, "Game Modes", err));
        if (!bakeGameMode(p)) QMessageBox::warning(this, "Game Modes", "The open document has no worldspawn.");
    });
    connect(start, &QPushButton::clicked, &dlg, [this] { insertWarzoneStart(); });
    connect(newType, &QPushButton::clicked, &dlg, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "New Game Type Script", "Game type name (1 to 11 of a-z, 0-9, _; not a stock one):",
                                                   QLineEdit::Normal, "teamkills", &ok).trimmed().toLower();
        if (!ok || name.isEmpty()) return;
        QString err;
        const QString file = newGameTypeScript(name, &err);
        if (file.isEmpty()) return void(QMessageBox::warning(this, "New Game Type Script", err));
        ctx_.setStatus("Game type script " + file + ": edit it, build, then start a match with game type " + name +
                       " (Use for F5 with that game type). The build checks its calls.");
    });
    connect(save, &QPushButton::clicked, &dlg, [=, this] {
        GameModePreset p;
        QString err;
        if (!fromFields(p, &err)) return void(QMessageBox::warning(this, "Game Modes", err));
        QDir().mkpath(userModesDir());
        QFile f(userModesDir() + "/" + qs(p.id) + ".iw8mode.json");
        // The file name is the id: saving under the id of a saved preset overwrites it, and a built-in id hides the
        // built-in preset in the list. Both are asked first.
        if (f.exists()) {
            if (!askFirst(this, QString("Replace the preset %1?\n\nYour saved preset of that id is overwritten:\n%2")
                                    .arg(qs(p.id), QDir::toNativeSeparators(f.fileName())),
                          "Replace", QMessageBox::AcceptRole))
                return;
        } else if (findGameMode(builtinGameModes(), p.id)) {
            if (!askFirst(this, QString("Save your own %1?\n\nIt takes the place of the built-in preset %1 in the list; Delete my preset "
                                        "brings the built-in one back.").arg(qs(p.id)),
                          "Save", QMessageBox::AcceptRole))
                return;
        }
        const std::string json = gameModePresetJson(p);
        if (!f.open(QIODevice::WriteOnly) || f.write(json.data(), qint64(json.size())) != qint64(json.size()))
            return void(QMessageBox::warning(this, "Game Modes", "Cannot write " + f.fileName()));
        f.close();
        ctx_.setStatus("Game mode preset saved: " + f.fileName());
        reload(qs(p.id));
    });
    connect(del, &QPushButton::clicked, &dlg, [=, this] {
        const QString id = idEdit->text().trimmed();
        const QString file = userModesDir() + "/" + id + ".iw8mode.json";
        if (id.isEmpty() || !QFileInfo::exists(file))
            return void(QMessageBox::information(this, "Game Modes", "Only your own presets can be deleted."));
        // The file is removed for good (no undo), so ask; Cancel is the default.
        const bool builtin = findGameMode(builtinGameModes(), id.toStdString()) != nullptr;
        if (!askFirst(this, QString("Delete your preset %1?\n\nIts file is removed (this cannot be undone):\n%2%3")
                                .arg(id, QDir::toNativeSeparators(file), builtin ? "\n\nThe built-in preset " + id + " comes back." : QString()),
                      "Delete", QMessageBox::DestructiveRole))
            return;
        if (!QFile::remove(file)) return void(QMessageBox::warning(this, "Game Modes", "Cannot delete " + QDir::toNativeSeparators(file)));
        ctx_.setStatus("Game mode preset deleted: " + id);
        reload(QString());
    });
    reload(DevmapRunner::modePresetSetting());
    dlg.exec();
}

}  // namespace iw8r::qt
