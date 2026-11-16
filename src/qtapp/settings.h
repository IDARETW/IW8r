// IW8 Radiant (Qt shell) - persistent preferences in IW8Radiant.ini beside the executable.
#pragma once

#include <QByteArray>
#include <QKeySequence>
#include <QStringList>
#include <QVariant>
#include <memory>

class QSettings;

namespace iw8r::qt {

class Settings {
public:
    static Settings& instance();
    // Tests run with volatile settings so a mapper's IW8Radiant.ini is never touched: values written during the run
    // live in memory only (they read back within the run, nothing reaches the file).
    void setVolatile(bool v) { volatile_ = v; }

    QVariant value(const QString& key, const QVariant& def = {}) const;
    void setValue(const QString& key, const QVariant& v);
    bool hasShortcut(const QString& command) const;
    QKeySequence shortcut(const QString& command) const;
    void setShortcut(const QString& command, const QKeySequence& seq);
    QStringList recentFiles() const;
    void addRecentFile(const QString& path);

private:
    Settings();
    std::unique_ptr<QSettings> s_;
    bool volatile_ = false;
    QVariantMap mem_;  // volatile mode store
};

}  // namespace iw8r::qt
