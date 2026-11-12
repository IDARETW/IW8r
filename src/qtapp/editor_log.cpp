#include "editor_log.h"

#include <QMutexLocker>
#include <cstdio>

namespace iw8r::qt {

namespace {
QtMessageHandler g_previousHandler = nullptr;

void editorLogMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (type == QtWarningMsg) EditorLog::instance().add(LogLevel::Warning, "qt", msg);
    else if (type == QtCriticalMsg || type == QtFatalMsg) EditorLog::instance().add(LogLevel::Error, "qt", msg);
    if (g_previousHandler) {
        g_previousHandler(type, ctx, msg);
    } else if (type != QtDebugMsg && type != QtInfoMsg) {
        // Qt's built-in output is not reachable once a handler is installed; keep warnings visible on stderr.
        std::fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    }
}
}  // namespace

EditorLog::EditorLog() {
    qRegisterMetaType<LogEntry>("iw8r::qt::LogEntry");
}

EditorLog& EditorLog::instance() {
    static EditorLog* log = new EditorLog();  // never destroyed: Qt messages may arrive during shutdown
    return *log;
}

void EditorLog::installQtMessageHandler() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    g_previousHandler = qInstallMessageHandler(editorLogMessageHandler);
}

QString EditorLog::levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Info: return "Info";
    case LogLevel::Warning: return "Warning";
    case LogLevel::Error: return "Error";
    }
    return "Info";
}

void EditorLog::add(LogLevel level, const QString& source, const QString& text) {
    LogEntry e;
    e.time = QDateTime::currentDateTime();
    e.level = level;
    e.source = source;
    e.text = text;
    {
        QMutexLocker lock(&mutex_);
        buffer_.append(e);
        while (buffer_.size() > kCapacity) buffer_.removeFirst();
    }
    emit added(e);
}

QList<LogEntry> EditorLog::entries() const {
    QMutexLocker lock(&mutex_);
    return buffer_;
}

int EditorLog::count() const {
    QMutexLocker lock(&mutex_);
    return int(buffer_.size());
}

void EditorLog::clear() {
    {
        QMutexLocker lock(&mutex_);
        buffer_.clear();
    }
    emit cleared();
}

}  // namespace iw8r::qt
