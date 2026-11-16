// IW8 Radiant (Qt shell) - the shared parts of the side bar panels (owner 2026-11-15: "each upper section sidebar
// menu needs their own individual revamp. they need to have more flush menus and actually useful UIs").
//
// Every panel is built the same way, flush to the panel's edges:
//   PanelHeader   one line: the panel's name, a count ("37 of 1,020"), icon buttons on the right (favourites star,
//                 the dots menu with the panel's options and tools);
//   a search field across the panel;
//   ChipBar       one wrapping row of filter chips (an exclusive group, "All" first, with counts; toggle chips);
//   the list, edge to edge;
//   DetailsStrip  the selection's details and actions at the bottom, shown while something is selected.
// Icons are glyphs of Windows' icon font (Segoe MDL2 Assets), drawn in the theme's text colour.
#pragma once

#include <QColor>
#include <QFrame>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QLabel;
class QMenu;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace iw8r::qt {

class FlowLayout;

// Glyphs of Segoe MDL2 Assets used by the panels.
namespace glyph {
constexpr char16_t Star = 0xE734, StarFilled = 0xE735, More = 0xE712, Play = 0xE768, Stop = 0xE71A, Pin = 0xE718,
                   Add = 0xE710, Folder = 0xF12B, Back = 0xE72B, Clear = 0xE894, History = 0xE81C, Location = 0xE707,
                   Target = 0xE81D, Sound = 0xE767, Effects = 0xE945, Picture = 0xEB9F, Filter = 0xE71C, Refresh = 0xE72C,
                   Cube = 0xF158, Brush = 0xE790, Tag = 0xE8EC, Info = 0xE946, Copy = 0xE8C8, Undo = 0xE7A7, Redo = 0xE7A6,
                   Edit = 0xE70F, Delete = 0xE74D, View = 0xE890, Settings = 0xE713, Map = 0xE707, People = 0xE716,
                   Check = 0xE73E, Repair = 0xE90F, Light = 0xE793, Ruler = 0xECC6, Package = 0xE7B8;
}

// The icon font's family when Windows has it, else empty (the glyphs fall back to text).
QString glyphFont();
// A glyph as an icon (`side` px, two resolutions), in `color` (the window text colour when invalid); where the font is
// missing, `fallback` (a letter or two) is drawn instead.
QIcon glyphIcon(char16_t glyph, int side = 16, const QColor& color = QColor(), const QString& fallback = QString());
// A blend of the theme's text and window colours: k = 1 is the text colour, 0 the background (secondary text, rules).
QColor themeMix(double k);
// " · ", between the facts of a row or a strip.
QString factSep();

// The panel's header line.
class PanelHeader : public QWidget {
    Q_OBJECT
public:
    explicit PanelHeader(const QString& title, QWidget* parent = nullptr);
    void setTitle(const QString& title);
    void setCount(const QString& text, const QString& tip = QString());
    QString count() const;
    // An icon button at the right end (added left to right).
    QToolButton* addButton(char16_t glyph, const QString& tip, bool checkable = false, const QString& objectName = QString());
    // The dots button: its menu opens on click.
    QToolButton* addMenuButton(QMenu* menu, const QString& tip, const QString& objectName = QString());

protected:
    void changeEvent(QEvent* e) override;

private:
    void restyle();
    QLabel* title_;
    QLabel* count_;
    QHBoxLayout* buttons_;
};

// The panel's filter: one dropdown (owner 2026-11-15: "make the buttons into a dropdown list"). Its button spans the
// panel and names what is shown; its list has the categories with their counts (one at a time; the first added is
// selected) and, under a rule, switches that combine with them (Recent, Loops, In this map). Widgets added with
// addWidget (a kit box, a size box) sit beside it.
class ChipBar : public QWidget {
    Q_OBJECT
public:
    explicit ChipBar(QWidget* parent = nullptr);
    void clear();
    // A category; the first one added is selected. count < 0 shows no count.
    QAction* addChip(const QString& id, const QString& label, int count = -1, const QString& tip = QString(), const QColor& dot = QColor());
    // A switch that is on or off by itself.
    QAction* addToggle(const QString& id, const QString& label, bool on = false, const QString& tip = QString());
    void addWidget(QWidget* w);
    void setCount(const QString& id, int count);
    void setCurrent(const QString& id);  // no signal
    QString current() const { return current_; }
    bool isOn(const QString& id) const;
    void setOn(const QString& id, bool on);  // no signal
    QStringList chipIds() const;
    QAction* chip(const QString& id) const;  // the list entry
    QPushButton* button() const { return button_; }
    QString shownText() const;               // what the button says (tests)

signals:
    void currentChanged(const QString& id);
    void toggled(const QString& id, bool on);

protected:
    void changeEvent(QEvent* e) override;

private:
    void restyle();
    void updateButton();
    QString entryText(const QString& id) const;
    QHBoxLayout* row_;
    QPushButton* button_;
    QMenu* menu_;
    QAction* rule_ = nullptr;  // the separator above the switches
    QString current_;
    struct Chip {
        QString id, label, tip;
        int count = -1;
        bool exclusive = true;
        QAction* action = nullptr;
        QIcon dot;
    };
    QList<Chip> chips_;
    QList<QWidget*> extras_;
};

// The strip at the bottom of a panel: a rule above it, its content flush with the panel's sides.
class DetailsStrip : public QFrame {
    Q_OBJECT
public:
    explicit DetailsStrip(QWidget* parent = nullptr);
    QVBoxLayout* body() const { return body_; }
    // The selection's name (bold, elided in the middle; the whole name is the tooltip) and a grey line of facts.
    void setTitle(const QString& name, const QString& facts = QString());
    QString titleText() const;
    QString factsText() const;
    // An action button of the strip (icon and text), added to its button row.
    QPushButton* addAction(char16_t glyph, const QString& text, const QString& tip = QString(), const QString& objectName = QString());
    void evenButtons();  // the strip's buttons share one width (the widest one's)

protected:
    void resizeEvent(QResizeEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    void restyle();
    void elide();
    QVBoxLayout* body_;
    QLabel* name_;
    QLabel* facts_;
    FlowLayout* actions_;
    QString fullName_;
};

}  // namespace iw8r::qt
