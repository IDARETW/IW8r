#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace iw8r::qt {

QStringList themeNames() { return {"Dark", "Light", "Midnight"}; }
QColor defaultAccent() { return QColor(64, 128, 220); }

void applyTheme(const QString& name, const QColor& accentIn) {
    qApp->setStyle(QStyleFactory::create("Fusion"));
    const QColor accent = accentIn.isValid() ? accentIn : defaultAccent();
    struct Colours {
        QColor base, alt, window, text, disabled;
    };
    Colours c;
    if (name == "Light") c = {QColor(252, 252, 253), QColor(240, 241, 244), QColor(233, 234, 238), QColor(28, 30, 34), QColor(150, 150, 158)};
    else if (name == "Midnight") c = {QColor(0, 0, 0), QColor(16, 16, 20), QColor(10, 10, 14), QColor(255, 255, 255), QColor(150, 150, 160)};
    else c = {QColor(37, 39, 43), QColor(42, 44, 49), QColor(45, 47, 52), QColor(220, 222, 226), QColor(120, 120, 128)};
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.alt);
    p.setColor(QPalette::ToolTipBase, c.window);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.window);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, accent.lightness() > 150 ? Qt::black : Qt::white);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::PlaceholderText, c.disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, c.disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.disabled);
    qApp->setPalette(p);
}

}  // namespace iw8r::qt
