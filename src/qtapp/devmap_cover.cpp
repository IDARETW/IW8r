#include "devmap_cover.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace iw8r::qt {

DevmapCover::DevmapCover(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus |
                          Qt::WindowTransparentForInput) {
    setObjectName("devmapCover");
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::NoFocus);
}

void DevmapCover::setMap(const QString& id, const QString& gametype) {
    map_ = id;
    gametype_ = gametype;
    update();
}

void DevmapCover::addLine(const QString& text) {
    if (text.isEmpty()) return;
    lines_ << text;
    while (lines_.size() > kMaxLines) lines_.removeFirst();
    update();
}

void DevmapCover::setElapsed(double seconds) {
    // Repaint ten times a second at most: the card shows tenths.
    if (int(seconds * 10) == int(elapsed_ * 10)) return;
    elapsed_ = seconds;
    update();
}

void DevmapCover::coverRect(const QRect& physical) {
    if (physical.isNull()) return;
#ifdef _WIN32
    if (QGuiApplication::platformName() == "windows") {
        const HWND self = HWND(winId());
        if (!isVisible()) show();
        // Placed with Win32 in physical pixels (the game's rectangle comes from GetWindowRect), every tick: it
        // follows the game window and stays above it without being activated.
        SetWindowPos(self, HWND_TOPMOST, physical.x(), physical.y(), physical.width(), physical.height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
        covered_ = physical;
        return;
    }
#endif
    setGeometry(physical);  // a platform without real windows (the offscreen test platform)
    if (!isVisible()) show();
    covered_ = physical;
}

#ifdef _WIN32
namespace {
struct Find {
    DWORD pid;
    QRect best;
    qint64 bestArea;
};
BOOL CALLBACK onWindow(HWND h, LPARAM p) {
    auto* f = reinterpret_cast<Find*>(p);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != f->pid || !IsWindowVisible(h) || IsIconic(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT r;
    if (!GetWindowRect(h, &r)) return TRUE;
    const qint64 w = r.right - r.left, hgt = r.bottom - r.top;
    if (w < 200 || hgt < 150 || w * hgt <= f->bestArea) return TRUE;
    f->bestArea = w * hgt;
    f->best = QRect(r.left, r.top, int(w), int(hgt));
    return TRUE;
}
}  // namespace
#endif

QRect DevmapCover::gameWindowRect(quint32 pid) {
#ifdef _WIN32
    Find f{DWORD(pid), QRect(), 0};
    EnumWindows(onWindow, reinterpret_cast<LPARAM>(&f));
    return f.best;
#else
    Q_UNUSED(pid);
    return {};
#endif
}

int DevmapCover::runRealCheck(const QString& reportPath) {
    QStringList report;
    bool all = true;
    auto check = [&](bool ok, const QString& what) {
        report << QString(ok ? "ok   " : "FAIL ") + what;
        all = all && ok;
    };
#ifdef _WIN32
    const HWND before = GetForegroundWindow();
    RECT wa{0, 0, 1280, 720};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const QRect want(wa.right - 340, wa.bottom - 220, 320, 200);
    DevmapCover cover;
    cover.setMap("mp_cover_check", "war");
    cover.addLine("+0.0 s: cover check");
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 500) {
        cover.setElapsed(double(t.elapsed()) / 1000.0);
        cover.coverRect(want);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    const HWND self = HWND(cover.winId());
    RECT r{};
    GetWindowRect(self, &r);
    const LONG_PTR ex = GetWindowLongPtrW(self, GWL_EXSTYLE);
    check(QRect(r.left, r.top, r.right - r.left, r.bottom - r.top) == want,
          QString("the card sits on the rectangle asked for (%1,%2 %3x%4)").arg(r.left).arg(r.top).arg(r.right - r.left).arg(r.bottom - r.top));
    check(IsWindowVisible(self) != 0, "the card is visible");
    check((ex & WS_EX_TOPMOST) != 0, "the card is a topmost window");
    check((ex & WS_EX_TRANSPARENT) != 0, "the card lets mouse input through (WS_EX_TRANSPARENT)");
    POINT centre{want.center().x(), want.center().y()};
    check(WindowFromPoint(centre) != self, "a click at the card's centre reaches the window under it");
    check(GetForegroundWindow() == before && GetForegroundWindow() != self, "the foreground window did not change (the game keeps the keyboard)");
    check(GetFocus() != self && GetActiveWindow() != self, "the card is not the active window of the editor either");
    cover.hide();
#else
    check(false, "the cover check needs Windows");
#endif
    report << QString(all ? "cover check: passed" : "cover check: FAILED");
    if (!reportPath.isEmpty()) {
        QDir().mkpath(QFileInfo(reportPath).absolutePath());
        QFile f(reportPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(report.join(QChar(10)).toUtf8() + QByteArray(1, char(10)));
    }
    return all ? 0 : 1;
}

void DevmapCover::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(9, 11, 14));
    const int w = width(), h = height();
    const int margin = qMax(24, w / 22);
    p.setRenderHint(QPainter::TextAntialiasing);

    QFont title = font();
    title.setPixelSize(qBound(20, h / 14, 64));
    title.setBold(true);
    p.setFont(title);
    p.setPen(QColor(236, 240, 244));
    const QString head = map_.isEmpty() ? QStringLiteral("Loading") : "Loading " + map_;
    QRect titleRect(margin, h / 5, w - 2 * margin, title.pixelSize() + 12);
    p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(head, Qt::ElideRight, titleRect.width()));

    QFont sub = font();
    sub.setPixelSize(qBound(12, h / 40, 24));
    p.setFont(sub);
    p.setPen(QColor(150, 162, 174));
    const QString mode = gametype_.isEmpty() ? QString() : gametype_ + "   ";
    p.drawText(QRect(margin, titleRect.bottom() + 6, w - 2 * margin, sub.pixelSize() + 8), Qt::AlignLeft | Qt::AlignVCenter,
               mode + QString::number(elapsed_, 'f', 1) + " s");

    // The accent bar: a plain moving block (no claim about how far the boot is).
    const int barY = titleRect.bottom() + sub.pixelSize() + 26;
    p.fillRect(QRect(margin, barY, w - 2 * margin, 3), QColor(34, 40, 48));
    const int span = w - 2 * margin, block = qMax(40, span / 6);
    const int at = int(elapsed_ * 220) % (span + block) - block;
    const int x0 = qMax(0, at), x1 = qMin(span, at + block);
    if (x1 > x0) p.fillRect(QRect(margin + x0, barY, x1 - x0, 3), QColor(255, 153, 51));

    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(qBound(11, h / 54, 18));
    p.setFont(mono);
    const int lineH = mono.pixelSize() + 6;
    int y = barY + 28;
    for (int i = 0; i < lines_.size() && y + lineH < h - margin - lineH; ++i, y += lineH) {
        p.setPen(i == lines_.size() - 1 ? QColor(214, 222, 230) : QColor(112, 124, 136));
        p.drawText(QRect(margin, y, w - 2 * margin, lineH), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(lines_[i], Qt::ElideRight, w - 2 * margin));
    }

    p.setFont(sub);
    p.setPen(QColor(92, 102, 112));
    p.drawText(QRect(margin, h - margin - sub.pixelSize() - 6, w - 2 * margin, sub.pixelSize() + 8), Qt::AlignLeft | Qt::AlignVCenter,
               "IW8 Radiant: the game is starting behind this card. It closes when the map begins to load.");
}

}  // namespace iw8r::qt
