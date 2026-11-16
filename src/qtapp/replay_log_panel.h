// IW8 Radiant (Qt shell) - Replay Log dock: tails the proxy / client logs in the Replay folder (read only)
// so a mapper sees why a custom map was refused or failed to load (docs/research/19 §1).
#pragma once

#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTimer;

namespace iw8r::qt {

class ReplayLogPanel : public QWidget {
    Q_OBJECT
public:
    using GameDirFn = std::function<QString()>;
    using MapIdFn = std::function<QString()>;
    ReplayLogPanel(GameDirFn gameDir, MapIdFn mapId, QWidget* parent = nullptr);

    QStringList logFiles() const;  // existing log files, newest first
    void refresh();                // re-read the selected file's tail now
    QString shownText() const;
    static bool isProblemLine(const QString& line);
    // 7.13: a double-clicked line is handed here (the main window jumps to the script line or selects what it names).
    std::function<void(const QString& line)> lineActivated;

protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

private:
    void rescanFiles();
    GameDirFn gameDir_;
    MapIdFn mapId_;
    QComboBox* file_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QCheckBox* problemsOnly_ = nullptr;
    QCheckBox* mapOnly_ = nullptr;
    QLabel* info_ = nullptr;
    QPlainTextEdit* text_ = nullptr;
    QTimer* timer_ = nullptr;
    qint64 lastSize_ = -1;
    QString lastFile_;
};

}  // namespace iw8r::qt
