// IW8 Radiant - UI themes (roadmap 9.2): Dark (default), Light and Midnight (high contrast), with an editable
// accent colour. Stored in Settings ui/theme and ui/accent; View > Theme switches live.
#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

namespace iw8r::qt {

QStringList themeNames();                                   // "Dark", "Light", "Midnight"
void applyTheme(const QString& name, const QColor& accent);  // Fusion style + palette for the whole application
QColor defaultAccent();

}  // namespace iw8r::qt
