// IW8 Radiant Suite (Qt shell) - First-run setup (roadmap 9.1): Replay game folder and exe, ACTS, Oodle and x64-zt.
// Help > First-Run Setup, and at startup when the Replay exe is missing. Detect fills the fields from
// iw8r::candidateGameDirs / gameExesIn / oodleIn; Save writes the same Settings keys as Tools > Tool Paths.
#pragma once

#include <QDialog>

#include <functional>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;

namespace iw8r::qt {

class SetupWizard : public QDialog {
    Q_OBJECT
public:
    // Current tool paths by key ("acts", "replay", "oodle", "zonetool", "launch"): MainWindow::toolPath.
    using ToolPathFn = std::function<QString(const QString&)>;

    // `driveRoots` empty = every existing drive (tests pass a fake folder). Constructing never writes settings.
    SetupWizard(ToolPathFn current, std::vector<std::string> driveRoots, QWidget* parent = nullptr);

    QString replayFolder() const;
    QString replayExe() const;  // selected Replay exe (full path); empty = none
    QString launchExe() const;  // empty = same as the Replay exe
    QString actsPath() const;
    QString oodlePath() const;
    QString zonetoolPath() const;
    QString replayStatus() const;  // e.g. "found, stock exe", "missing"

public slots:
    void detect();
    void accept() override;  // writes the tools/* settings, then closes

private:
    void rebuildExeLists();
    void selectExe(QComboBox* combo, const QString& path);
    void updateStatus();
    void browseFolder();
    void browseReplayExe();
    void browseLaunchExe();
    void browseFile(QLineEdit* edit, const QString& title, const QString& filter);

    ToolPathFn current_;
    std::vector<std::string> driveRoots_;
    QLineEdit* folder_ = nullptr;
    QComboBox* replayCombo_ = nullptr;
    QComboBox* launchCombo_ = nullptr;
    QLineEdit* acts_ = nullptr;
    QLineEdit* oodle_ = nullptr;
    QLineEdit* zonetool_ = nullptr;
    QLabel* folderStatus_ = nullptr;
    QLabel* replayStatus_ = nullptr;
    QLabel* launchStatus_ = nullptr;
    QLabel* actsStatus_ = nullptr;
    QLabel* oodleStatus_ = nullptr;
    QLabel* zonetoolStatus_ = nullptr;
    QLabel* summary_ = nullptr;
};

// Runs the wizard modally. Returns true when Save wrote the settings.
bool runFirstRunSetup(QWidget* parent, SetupWizard::ToolPathFn current);

}  // namespace iw8r::qt
