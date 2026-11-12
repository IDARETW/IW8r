// IW8 Radiant Suite - F5 cold start: a card that sits exactly over the game window from the moment the editor starts
// the game until the map begins to load, so the first thing seen of the game is the map loading screen (owner request
// 2026-11-11; docs/research/77). The boot screens, the mode select, the Multiplayer menu and the lobby run behind it.
//
// The card never takes focus and passes every click through, so the game window keeps the keyboard and the mouse. It
// is an editor window: nothing is done to the game's own window. When the game runs in exclusive full screen the card
// cannot be seen (the game then shows its own screens as before).
#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace iw8r::qt {

class DevmapCover : public QWidget {
    Q_OBJECT
public:
    explicit DevmapCover(QWidget* parent = nullptr);

    void setMap(const QString& id, const QString& gametype);
    void addLine(const QString& text);   // a timeline line; the latest kMaxLines are shown
    void setElapsed(double seconds);
    QStringList lines() const { return lines_; }

    // Shows the card over a screen rectangle given in physical pixels: topmost, not activated.
    void coverRect(const QRect& physical);
    QRect coveredRect() const { return covered_; }

    // The largest visible top-level window of a process in physical pixels; a null rect while it has none.
    static QRect gameWindowRect(quint32 pid);

    static constexpr int kMaxLines = 14;

    // IW8Radiant.exe --cover-check [--out <file>]: shows a small card in a corner of the real desktop for half a
    // second and reports whether it sat where asked, was topmost, let the mouse through and left the foreground
    // window alone. Returns 0 when all hold. (The GUI test runs on Qt's offscreen platform, which has no real windows.)
    static int runRealCheck(const QString& reportPath);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString map_, gametype_;
    QStringList lines_;
    double elapsed_ = 0;
    QRect covered_;
};

}  // namespace iw8r::qt
