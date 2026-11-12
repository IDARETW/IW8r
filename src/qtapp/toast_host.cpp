#include "toast_host.h"

#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include "settings.h"

namespace iw8r::qt {

namespace {
constexpr int kWidth = 360;
constexpr int kMargin = 16;
constexpr int kMaxText = 320;

const char* levelColor(LogLevel level) {
    switch (level) {
    case LogLevel::Info: return "#5fb37a";
    case LogLevel::Warning: return "#e8b04b";
    case LogLevel::Error: return "#e66a6a";
    }
    return "#5fb37a";
}
}  // namespace

ToastHost::ToastHost(QWidget* window) : QWidget(window) {
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    setObjectName("toastHost");
    lay_ = new QVBoxLayout(this);
    lay_->setContentsMargins(0, 0, 0, 0);
    lay_->setSpacing(6);
    setFixedWidth(kWidth);
    enabled_ = Settings::instance().value("view/toasts", true).toBool();
    hide();
    if (window) window->installEventFilter(this);
    connect(&EditorLog::instance(), &EditorLog::added, this, [this](const LogEntry& e) {
        if (e.level == LogLevel::Info) return;
        showToast(e.level, e.text, e.source);
    }, Qt::QueuedConnection);
}

void ToastHost::showToast(LogLevel level, const QString& text, const QString& source) {
    if (!enabled_) return;
    while (lay_->count() >= kMaxVisible) {
        if (QWidget* oldest = lay_->itemAt(0)->widget()) dismiss(oldest);
        else break;
    }
    auto* frame = new QFrame(this);
    frame->setObjectName("toast");
    frame->setAttribute(Qt::WA_TransparentForMouseEvents);
    frame->setFocusPolicy(Qt::NoFocus);
    frame->setFixedWidth(kWidth);
    frame->setStyleSheet(QString("QFrame#toast { background: rgba(40, 42, 46, 238); border: 1px solid #55585e;"
                                 " border-left: 4px solid %1; border-radius: 4px; }"
                                 " QLabel { color: #dcdee2; background: transparent; }")
                             .arg(levelColor(level)));
    auto* row = new QVBoxLayout(frame);
    row->setContentsMargins(10, 6, 10, 6);
    row->setSpacing(2);
    auto* head = new QLabel(QString("<b style='color:%1'>%2</b>%3")
                                .arg(levelColor(level), EditorLog::levelName(level).toHtmlEscaped(),
                                     source.isEmpty() ? QString() : QString(" &nbsp;<span style='color:#9a9ea6'>%1</span>").arg(source.toHtmlEscaped())),
                            frame);
    head->setTextFormat(Qt::RichText);
    QString body = text.trimmed();
    if (body.size() > kMaxText) body = body.left(kMaxText) + "...";
    auto* msg = new QLabel(body.toHtmlEscaped(), frame);
    msg->setWordWrap(true);
    msg->setTextFormat(Qt::PlainText);
    msg->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(head);
    row->addWidget(msg);
    lay_->addWidget(frame);
    frame->show();

    QPointer<QWidget> guard = frame;
    QTimer::singleShot(kLifetimeMs, this, [this, guard] {
        if (guard) dismiss(guard.data());
    });
    setVisible(true);
    relayout();
    raise();
}

void ToastHost::dismiss(QWidget* toast) {
    if (!toast) return;
    lay_->removeWidget(toast);
    toast->hide();
    toast->deleteLater();
    if (lay_->count() == 0) hide();
    else relayout();
}

void ToastHost::setToastsEnabled(bool on) {
    enabled_ = on;
    if (on) return;
    while (lay_->count() > 0) {
        if (QWidget* w = lay_->itemAt(0)->widget()) dismiss(w);
        else break;
    }
    hide();
}

int ToastHost::visibleCount() const { return lay_->count(); }

void ToastHost::relayout() {
    const QWidget* win = parentWidget();
    if (!win) return;
    adjustSize();
    int bottom = win->height() - kMargin;
    if (const auto* mw = qobject_cast<const QMainWindow*>(win)) {
        if (mw->statusBar() && mw->statusBar()->isVisible()) bottom -= mw->statusBar()->height();
    }
    move(win->width() - width() - kMargin, bottom - height());
}

bool ToastHost::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == parentWidget() && ev->type() == QEvent::Resize && isVisible()) relayout();
    return QWidget::eventFilter(obj, ev);
}

}  // namespace iw8r::qt
