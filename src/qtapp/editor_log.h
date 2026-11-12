// IW8 Radiant Suite (Qt shell) - process-wide log: status messages, compile output and Qt warnings in one ring buffer.
// Feeds the Console dock (log_panel.h) and the toast notifications (toast_host.h). Thread-safe: add() may be called
// from any thread; listeners get `added` through queued connections, so a Qt warning raised inside a widget's paint
// code never re-enters the UI.
#pragma once

#include <QDateTime>
#include <QMutex>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

namespace iw8r::qt {

enum class LogLevel { Info, Warning, Error };

struct LogEntry {
    QDateTime time;
    LogLevel level = LogLevel::Info;
    QString source;  // "status", "compile", "qt"
    QString text;
};

class EditorLog : public QObject {
    Q_OBJECT
public:
    static constexpr int kCapacity = 5000;  // oldest entries are dropped beyond this

    static EditorLog& instance();
    // Routes qWarning / qCritical / qFatal into the log (source "qt") and chains to the previous message handler.
    // Call once from main() after QApplication exists; a second call does nothing.
    static void installQtMessageHandler();
    static QString levelName(LogLevel level);

    void add(LogLevel level, const QString& source, const QString& text);
    QList<LogEntry> entries() const;  // copy, oldest first
    int count() const;
    void clear();  // empties the buffer and emits cleared()

signals:
    void added(const iw8r::qt::LogEntry& entry);
    void cleared();

private:
    EditorLog();
    mutable QMutex mutex_;
    QList<LogEntry> buffer_;
};

}  // namespace iw8r::qt

Q_DECLARE_METATYPE(iw8r::qt::LogEntry)
