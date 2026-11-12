#include "settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace iw8r::qt {

Settings& Settings::instance() {
    static Settings s;
    return s;
}

Settings::Settings()
    : s_(std::make_unique<QSettings>(QDir(QCoreApplication::applicationDirPath()).filePath("IW8Radiant.ini"), QSettings::IniFormat)) {}

QVariant Settings::value(const QString& key, const QVariant& def) const {
    if (volatile_) return mem_.contains(key) ? mem_.value(key) : def;
    return s_->value(key, def);
}

void Settings::setValue(const QString& key, const QVariant& v) {
    if (volatile_) mem_.insert(key, v);
    else s_->setValue(key, v);
}

bool Settings::hasShortcut(const QString& command) const { return !volatile_ && s_->contains("shortcuts/" + command); }

QKeySequence Settings::shortcut(const QString& command) const {
    return QKeySequence(s_->value("shortcuts/" + command).toString(), QKeySequence::PortableText);
}

void Settings::setShortcut(const QString& command, const QKeySequence& seq) {
    if (!volatile_) s_->setValue("shortcuts/" + command, seq.toString(QKeySequence::PortableText));
}

QStringList Settings::recentFiles() const { return volatile_ ? QStringList() : s_->value("recentFiles").toStringList(); }

void Settings::addRecentFile(const QString& path) {
    if (volatile_) return;
    QStringList r = recentFiles();
    r.removeAll(path);
    r.prepend(path);
    while (r.size() > 10) r.removeLast();
    s_->setValue("recentFiles", r);
}

}  // namespace iw8r::qt
