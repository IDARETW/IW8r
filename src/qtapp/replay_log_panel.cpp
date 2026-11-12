#include "replay_log_panel.h"
#include <QTextCursor>
#include <QTextBlock>
#include <QMouseEvent>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

namespace iw8r::qt {

namespace {
constexpr qint64 kTailBytes = 512 * 1024;  // replay-dw-client.log reaches tens of MB
}

ReplayLogPanel::ReplayLogPanel(GameDirFn gameDir, MapIdFn mapId, QWidget* parent)
    : QWidget(parent), gameDir_(std::move(gameDir)), mapId_(std::move(mapId)) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    auto* top = new QHBoxLayout();
    file_ = new QComboBox(this);
    file_->setObjectName("replayLogFile");
    file_->setMinimumWidth(260);
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText("filter text");
    filter_->setClearButtonEnabled(true);
    problemsOnly_ = new QCheckBox("Warnings / errors", this);
    mapOnly_ = new QCheckBox("This map / [Maps]", this);
    auto* rescan = new QPushButton("Rescan", this);
    top->addWidget(file_);
    top->addWidget(filter_, 1);
    top->addWidget(problemsOnly_);
    top->addWidget(mapOnly_);
    top->addWidget(rescan);
    lay->addLayout(top);
    info_ = new QLabel(this);
    info_->setStyleSheet("color: gray");
    lay->addWidget(info_);
    text_ = new QPlainTextEdit(this);
    text_->setObjectName("replayLogText");
    text_->setReadOnly(true);
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    text_->setMaximumBlockCount(5000);
    text_->viewport()->installEventFilter(this);
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    text_->setFont(mono);
    lay->addWidget(text_, 1);

    timer_ = new QTimer(this);
    timer_->setInterval(2000);
    connect(timer_, &QTimer::timeout, this, [this] {
        // Only re-read when the file changed (size or truncation at the next launch).
        const QFileInfo fi(file_->currentData().toString());
        if (fi.filePath() != lastFile_ || fi.size() != lastSize_) refresh();
    });
    auto again = [this] {
        lastSize_ = -1;
        refresh();
    };
    connect(file_, &QComboBox::currentIndexChanged, this, again);
    connect(filter_, &QLineEdit::textChanged, this, again);
    connect(problemsOnly_, &QCheckBox::toggled, this, again);
    connect(mapOnly_, &QCheckBox::toggled, this, again);
    connect(rescan, &QPushButton::clicked, this, [this] { rescanFiles(); });
    rescanFiles();
}

QStringList ReplayLogPanel::logFiles() const {
    const QDir dir(gameDir_());
    if (!dir.exists()) return {};
    // Proxy logs (docs/research/19 §1.1), the DW client log, and the newest crash archive.
    QFileInfoList found = dir.entryInfoList({"mw120rproxy.log", "mw120rproxy.engine.log", "mw120rproxy.exceptions.log",
                                             "mw120rproxy.trace.log", "replay-dw-client.log", "mw120r-diag.log"},
                                            QDir::Files);
    QFileInfoList crashes = dir.entryInfoList({"mw120rproxy.crash.*.log"}, QDir::Files, QDir::Time);
    if (!crashes.isEmpty()) found << crashes.front();
    std::sort(found.begin(), found.end(), [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); });
    QStringList out;
    for (const QFileInfo& fi : found) out << fi.absoluteFilePath();
    return out;
}

void ReplayLogPanel::rescanFiles() {
    const QString keep = file_->currentData().toString();
    file_->blockSignals(true);
    file_->clear();
    for (const QString& f : logFiles()) {
        const QFileInfo fi(f);
        file_->addItem(QString("%1  (%2, %3 KB)").arg(fi.fileName(), fi.lastModified().toString("MM-dd HH:mm")).arg(fi.size() / 1024), f);
    }
    const int i = file_->findData(keep);
    file_->setCurrentIndex(i >= 0 ? i : 0);
    file_->blockSignals(false);
    lastSize_ = -1;
    refresh();
}

bool ReplayLogPanel::isProblemLine(const QString& line) {
    static const QRegularExpression re("\\[(WARN|ERR )|\\bFAILED\\b|\\berror\\b|exception|unavailable|refused|rejected",
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(line).hasMatch();
}

void ReplayLogPanel::refresh() {
    const QString path = file_->currentData().toString();
    if (path.isEmpty()) {
        info_->setText("No Replay logs found in " + gameDir_());
        text_->clear();
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        info_->setText("Cannot read " + path);
        return;
    }
    const qint64 size = f.size();
    lastFile_ = path;
    lastSize_ = size;
    if (size > kTailBytes) f.seek(size - kTailBytes);
    QStringList lines = QString::fromLocal8Bit(f.readAll()).split('\n');
    if (size > kTailBytes && !lines.isEmpty()) lines.removeFirst();  // partial first line
    const QString needle = filter_->text().trimmed();
    const QString id = mapId_ ? mapId_() : QString();
    QStringList shown;
    int problems = 0;
    for (QString l : lines) {
        l = l.trimmed();
        if (l.isEmpty()) continue;
        const bool bad = isProblemLine(l);
        problems += bad;
        if (problemsOnly_->isChecked() && !bad) continue;
        if (mapOnly_->isChecked() && !l.contains("[Maps]") && (id.isEmpty() || !l.contains(id))) continue;
        if (!needle.isEmpty() && !l.contains(needle, Qt::CaseInsensitive)) continue;
        shown << l;
    }
    text_->setPlainText(shown.join('\n'));
    text_->moveCursor(QTextCursor::End);
    info_->setText(QString("%1 - %2 lines shown, %3 warning/error lines in the last %4 KB (read only, refreshes every 2 s)")
                       .arg(QFileInfo(path).fileName())
                       .arg(shown.size())
                       .arg(problems)
                       .arg(std::min(size, kTailBytes) / 1024));
}

QString ReplayLogPanel::shownText() const { return text_->toPlainText(); }

void ReplayLogPanel::showEvent(QShowEvent*) {
    rescanFiles();
    timer_->start();
}

void ReplayLogPanel::hideEvent(QHideEvent*) { timer_->stop(); }

bool ReplayLogPanel::eventFilter(QObject* o, QEvent* e) {
    if (text_ && o == text_->viewport() && e->type() == QEvent::MouseButtonDblClick && lineActivated) {
        const QTextCursor c = text_->cursorForPosition(static_cast<QMouseEvent*>(e)->position().toPoint());
        lineActivated(c.block().text());
        return true;
    }
    return QWidget::eventFilter(o, e);
}

}  // namespace iw8r::qt
