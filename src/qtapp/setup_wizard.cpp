#include "setup_wizard.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "setup_detect.h"
#include "settings.h"

namespace iw8r::qt {

namespace {

constexpr const char* kGood = "color: #6cc070;";
constexpr const char* kWarn = "color: #e0a84a;";
constexpr const char* kBad = "color: #e06c6c;";

QString norm(const QString& p) { return QDir::fromNativeSeparators(p.trimmed()); }

QString folderOf(const QString& file) { return file.isEmpty() ? QString() : norm(QFileInfo(file).absolutePath()); }

bool sameFolder(const QString& a, const QString& b) {
    return !a.isEmpty() && !b.isEmpty() && QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
}

// `path` when it lies in `folder`, else empty (a folder change drops an exe from the old folder).
QString keepIfIn(const QString& path, const QString& folder) { return sameFolder(folderOf(path), folder) ? path : QString(); }

struct Status {
    QString text;
    const char* style;
};

Status exeStatus(const QString& path) {
    if (path.isEmpty()) return {"not selected", kBad};
    switch (classifyExe(path.toStdString())) {
        case ExeKind::Missing: return {"missing", kBad};
        case ExeKind::NotPe: return {"not a PE file", kBad};
        case ExeKind::Stock: return {"found, stock exe", kGood};
        case ExeKind::Recompiled: return {"found, recompiled (RPLYCMP2)", kWarn};
    }
    return {"missing", kBad};
}

Status fileStatus(const QString& path) {
    if (path.isEmpty()) return {"not set", kBad};
    return isExistingFile(path.toStdString()) ? Status{"found", kGood} : Status{"missing", kBad};
}

void setStatusLabel(QLabel* label, const Status& s) {
    label->setText(s.text);
    label->setStyleSheet(s.style);
}

}  // namespace

QString SetupWizard::replayFolder() const { return folder_->text().trimmed(); }
QString SetupWizard::replayExe() const { return replayCombo_->currentData().toString(); }
QString SetupWizard::launchExe() const { return launchCombo_->currentData().toString(); }
QString SetupWizard::actsPath() const { return acts_->text().trimmed(); }
QString SetupWizard::oodlePath() const { return oodle_->text().trimmed(); }
QString SetupWizard::zonetoolPath() const { return zonetool_->text().trimmed(); }
QString SetupWizard::replayStatus() const { return replayStatus_->text(); }

SetupWizard::SetupWizard(ToolPathFn current, std::vector<std::string> driveRoots, QWidget* parent)
    : QDialog(parent), current_(std::move(current)), driveRoots_(driveRoots.empty() ? windowsDriveRoots() : std::move(driveRoots)) {
    if (!current_) current_ = [](const QString&) { return QString(); };
    setWindowTitle("First-run setup");
    setMinimumWidth(820);

    auto* lay = new QVBoxLayout(this);
    auto* intro = new QLabel(
        "Point the editor at the Replay game folder and the tools. Detect looks for an IW8\\Builds\\<name> folder holding "
        "game_dx12_ship_replay*.exe and the Oodle DLL there; Browse sets any path by hand. Save writes the same settings as "
        "Tools > Tool Paths.",
        this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto* grid = new QGridLayout;
    lay->addLayout(grid);
    int r = 0;
    auto addRow = [&](const char* label, QWidget* field, const char* browseText, QLabel* status) {
        auto* browse = new QPushButton(browseText, this);
        grid->addWidget(new QLabel(label, this), r, 0);
        grid->addWidget(field, r, 1);
        grid->addWidget(browse, r, 2);
        grid->addWidget(status, r, 3);
        ++r;
        return browse;
    };

    folder_ = new QLineEdit(this);
    folder_->setMinimumWidth(340);
    folderStatus_ = new QLabel(this);
    connect(addRow("Replay game folder", folder_, "Browse...", folderStatus_), &QPushButton::clicked, this, &SetupWizard::browseFolder);

    replayCombo_ = new QComboBox(this);
    replayCombo_->setMinimumWidth(340);
    replayStatus_ = new QLabel(this);
    connect(addRow("Replay exe (Deploy, Launch)", replayCombo_, "Browse...", replayStatus_), &QPushButton::clicked, this,
            &SetupWizard::browseReplayExe);

    launchCombo_ = new QComboBox(this);
    launchCombo_->setMinimumWidth(340);
    launchStatus_ = new QLabel(this);
    connect(addRow("Launch exe", launchCombo_, "Browse...", launchStatus_), &QPushButton::clicked, this, &SetupWizard::browseLaunchExe);

    acts_ = new QLineEdit(this);
    actsStatus_ = new QLabel(this);
    connect(addRow("ACTS (acts.exe)", acts_, "Browse...", actsStatus_), &QPushButton::clicked, this, [this] {
        browseFile(acts_, "ACTS (acts.exe)", "acts.exe (acts.exe);;Executables (*.exe);;All files (*)");
    });

    oodle_ = new QLineEdit(this);
    oodleStatus_ = new QLabel(this);
    connect(addRow("Oodle (oo2core_*_win64.dll)", oodle_, "Browse...", oodleStatus_), &QPushButton::clicked, this, [this] {
        browseFile(oodle_, "Oodle DLL", "Oodle (oo2core_*_win64.dll);;DLL (*.dll);;All files (*)");
    });

    zonetool_ = new QLineEdit(this);
    zonetoolStatus_ = new QLabel(this);
    connect(addRow("x64-zt (zonetool.exe)", zonetool_, "Browse...", zonetoolStatus_), &QPushButton::clicked, this, [this] {
        browseFile(zonetool_, "x64-zt (zonetool.exe)", "zonetool.exe (zonetool.exe);;Executables (*.exe);;All files (*)");
    });

    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    lay->addWidget(summary_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Save");
    auto* detectButton = buttons->addButton("Detect", QDialogButtonBox::ActionRole);
    connect(detectButton, &QPushButton::clicked, this, &SetupWizard::detect);
    connect(buttons, &QDialogButtonBox::accepted, this, &SetupWizard::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);

    // Signals are connected after every widget exists; the initial values below then fill the lists.
    connect(folder_, &QLineEdit::textChanged, this, [this] { rebuildExeLists(); });
    connect(replayCombo_, &QComboBox::currentIndexChanged, this, [this] { updateStatus(); });
    connect(launchCombo_, &QComboBox::currentIndexChanged, this, [this] { updateStatus(); });
    for (QLineEdit* edit : {acts_, oodle_, zonetool_}) connect(edit, &QLineEdit::textChanged, this, [this] { updateStatus(); });

    const QString replay = norm(current_("replay"));
    const QString launch = norm(current_("launch"));
    acts_->setText(current_("acts"));
    oodle_->setText(current_("oodle"));
    zonetool_->setText(current_("zonetool"));
    folder_->setText(folderOf(replay));
    rebuildExeLists();
    if (!replay.isEmpty()) selectExe(replayCombo_, replay);
    if (!launch.isEmpty()) selectExe(launchCombo_, launch);
    updateStatus();
    // Nothing usable configured yet: look for the game folder now (the user can still change every field).
    if (!isExistingFile(replay.toStdString())) detect();
}

void SetupWizard::rebuildExeLists() {
    const QString folder = replayFolder();
    const QString keepReplay = keepIfIn(replayExe(), folder);
    const QString keepLaunch = keepIfIn(launchExe(), folder);
    replayCombo_->clear();
    launchCombo_->clear();
    launchCombo_->addItem("(same as the Replay exe)", QString());
    for (const std::string& exe : gameExesIn(folder.toStdString())) {
        const QString full = QString::fromStdString(exe);
        replayCombo_->addItem(QFileInfo(full).fileName(), full);
        launchCombo_->addItem(QFileInfo(full).fileName(), full);
    }
    if (!keepReplay.isEmpty()) selectExe(replayCombo_, keepReplay);
    else if (replayCombo_->count() > 0) replayCombo_->setCurrentIndex(0);
    if (!keepLaunch.isEmpty()) selectExe(launchCombo_, keepLaunch);
    else launchCombo_->setCurrentIndex(0);
    updateStatus();
}

void SetupWizard::selectExe(QComboBox* combo, const QString& path) {
    const QString p = norm(path);
    int idx = combo->findData(p);
    if (idx < 0) {
        combo->addItem(QFileInfo(p).fileName(), p);
        idx = combo->count() - 1;
    }
    combo->setCurrentIndex(idx);
    combo->setToolTip(p);
}

void SetupWizard::updateStatus() {
    const QString folder = replayFolder();
    if (folder.isEmpty()) {
        setStatusLabel(folderStatus_, {"not set", kBad});
    } else if (!isExistingDir(folder.toStdString())) {
        setStatusLabel(folderStatus_, {"folder missing", kBad});
    } else {
        const size_t n = gameExesIn(folder.toStdString()).size();
        setStatusLabel(folderStatus_, n ? Status{QString("%1 game exe(s) here").arg(n), kGood} : Status{"no game_dx12_ship_replay*.exe here", kBad});
    }
    setStatusLabel(replayStatus_, exeStatus(replayExe()));
    const QString launch = launchExe();
    setStatusLabel(launchStatus_, launch.isEmpty() ? Status{"same as the Replay exe", kGood} : exeStatus(launch));
    setStatusLabel(actsStatus_, fileStatus(actsPath()));
    setStatusLabel(oodleStatus_, fileStatus(oodlePath()));
    setStatusLabel(zonetoolStatus_, fileStatus(zonetoolPath()));
}

void SetupWizard::detect() {
    QStringList notes;
    if (gameExesIn(replayFolder().toStdString()).empty()) {
        const auto dirs = candidateGameDirs(driveRoots_);
        if (dirs.empty()) {
            notes << "No IW8\\Builds game folder with game_dx12_ship_replay*.exe found on the drives; Browse to it.";
        } else {
            folder_->setText(QString::fromStdString(dirs.front()));  // textChanged rebuilds the exe lists
            notes << QString("Game folder: %1%2").arg(QString::fromStdString(dirs.front()))
                         .arg(dirs.size() > 1 ? QString(" (%1 candidates, the first is used)").arg(dirs.size()) : QString());
        }
    } else {
        notes << QString("Game folder kept: %1").arg(replayFolder());
    }
    if (!replayExe().isEmpty()) notes << QString("Replay exe: %1").arg(replayExe());

    if (!isExistingFile(oodlePath().toStdString())) {
        const auto dlls = oodleIn(replayFolder().toStdString());
        if (dlls.empty()) {
            notes << "No oo2core_*_win64.dll in the game folder (set Oodle by hand).";
        } else {
            oodle_->setText(QString::fromStdString(dlls.front()));
            notes << QString("Oodle: %1").arg(QFileInfo(oodlePath()).fileName());
        }
    }
    if (!isExistingFile(actsPath().toStdString())) notes << "ACTS (acts.exe) is not set or missing: Browse to it.";
    if (!isExistingFile(zonetoolPath().toStdString())) notes << "x64-zt zonetool.exe is not set: Browse to it (optional for the map editor).";
    summary_->setText(notes.join("\n"));
    updateStatus();
}

void SetupWizard::accept() {
    const QString exe = replayExe();
    const ExeKind kind = exe.isEmpty() ? ExeKind::Missing : classifyExe(exe.toStdString());
    if (kind == ExeKind::Missing || kind == ExeKind::NotPe) {
        const auto answer = QMessageBox::question(this, "First-run setup",
                                                  "The Replay exe is not set or is missing. Save anyway?");
        if (answer != QMessageBox::Yes) return;
    }
    if (!isExistingFile(actsPath().toStdString()) &&
        QMessageBox::question(this, "First-run setup",
                              "ACTS (acts.exe) is not set or is missing: stock maps, stock assets and build validation will not work "
                              "until it is (Tools > Tool Paths). Save anyway?") != QMessageBox::Yes)
        return;
    // The same keys Tools > Tool Paths writes; toolPath() reads them back.
    Settings& s = Settings::instance();
    s.setValue("tools/acts", actsPath());
    s.setValue("tools/replay", exe);
    s.setValue("tools/oodle", oodlePath());
    s.setValue("tools/zonetool", zonetoolPath());
    s.setValue("tools/launch", launchExe());
    QDialog::accept();
}

void SetupWizard::browseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Replay game folder", folder_->text());
    if (!dir.isEmpty()) folder_->setText(norm(dir));
}

void SetupWizard::browseReplayExe() {
    const QString f = QFileDialog::getOpenFileName(this, "Replay exe", replayFolder(),
                                                   "Replay exe (game_dx12_ship_replay*.exe);;Executables (*.exe);;All files (*)");
    if (f.isEmpty()) return;
    const QString p = norm(f);
    if (!sameFolder(folderOf(p), replayFolder())) folder_->setText(folderOf(p));  // rebuilds the lists for that folder
    selectExe(replayCombo_, p);
    updateStatus();
}

void SetupWizard::browseLaunchExe() {
    const QString f = QFileDialog::getOpenFileName(this, "Launch exe", replayFolder(), "Executables (*.exe);;All files (*)");
    if (f.isEmpty()) return;
    selectExe(launchCombo_, norm(f));
    updateStatus();
}

void SetupWizard::browseFile(QLineEdit* edit, const QString& title, const QString& filter) {
    const QString f = QFileDialog::getOpenFileName(this, title, edit->text(), filter);
    if (!f.isEmpty()) edit->setText(norm(f));
}

bool runFirstRunSetup(QWidget* parent, SetupWizard::ToolPathFn current) {
    SetupWizard dlg(std::move(current), {}, parent);
    return dlg.exec() == QDialog::Accepted;
}

}  // namespace iw8r::qt
