import io

# GUI test of numeric_drag.h (owner 2026-11-15: "numeric values should be able to be dragged to change the value, with
# constant scrolling after a certain distance").

ROOT = 'E:/IW8-Radiant/src/'


def rw(p, pairs):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = s.replace('\r\n', '\n')
    for a, b in pairs:
        if b in s and a not in s:
            continue
        assert s.count(a) == 1, (p, s.count(a), a[:90])
        s = s.replace(a, b)
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace('\n', nl))


rw('qtapp/qt_gui_test_sidebar.cpp', [
(r'''#include <QComboBox>
#include <QDir>''', r'''#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMouseEvent>'''),
(r'''#include "main_window.h"
#include "panel_chrome.h"''', r'''#include "main_window.h"
#include "numeric_drag.h"
#include "panel_chrome.h"'''),
(r'''void runSidebarChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, MainWindow& w,
                      const QString& outDir) {''', r'''void runSidebarChecks(std::function<void(const QString&)> section, std::function<void(bool, const QString&)> check, MainWindow& w,
                      const QString& outDir) {
    section("number fields: drag to change");
    {
        // A field whose changes edit the map (as the panels' fields do): dragged sideways, it follows the mouse; past the
        // zone it keeps scrolling while held; back inside it stops; the whole drag is one undo step.
        EditorContext& ctx = w.context();
        const Id world = ctx.doc.map().entities.empty() ? kNoId : ctx.doc.map().entities.front().id;
        QWidget host;
        host.setAttribute(Qt::WA_DontShowOnScreen);
        auto* form = new QFormLayout(&host);
        auto* spin = new QDoubleSpinBox(&host);
        spin->setRange(-100000, 100000);
        spin->setSingleStep(2);
        form->addRow("Test amount", spin);
        host.resize(260, 40);
        host.show();
        QTest::qWait(30);
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &host, [&ctx, world](double v) {
            ctx.doc.setKeyValue(world, "iw8r_drag_test", QString::number(v).toStdString());
        });
        const size_t undoBefore = ctx.doc.undoPosition();
        QLineEdit* le = spin->findChild<QLineEdit*>();
        const QPoint at = le ? QPoint(le->width() / 2, le->height() / 2) : QPoint();
        const auto send = [&](QEvent::Type t, int dx, Qt::MouseButtons buttons) {
            if (!le) return;
            const QPoint local = at + QPoint(dx, 0);
            QMouseEvent ev(t, QPointF(local), QPointF(le->mapToGlobal(local)), t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons,
                           Qt::NoModifier);
            QApplication::sendEvent(le, &ev);
        };
        send(QEvent::MouseButtonPress, 0, Qt::LeftButton);
        send(QEvent::MouseMove, 6, Qt::LeftButton);   // past the drag distance: the drag starts here
        send(QEvent::MouseMove, 46, Qt::LeftButton);  // 40 px = 10 steps of 2
        const double followed = spin->value();
        const bool started = numeric_drag::dragging();
        send(QEvent::MouseMove, 6 + numeric_drag::kZone + 80, Qt::LeftButton);  // past the zone
        const double atZone = spin->value();
        QTest::qWait(300);
        const double later = spin->value();
        QTest::qWait(200);
        const double later2 = spin->value();
        send(QEvent::MouseMove, 46, Qt::LeftButton);  // back inside: it follows the mouse again
        const double back = spin->value();
        QTest::qWait(200);
        const bool stopped = spin->value() == back;
        send(QEvent::MouseButtonRelease, 46, Qt::NoButton);
        QTest::qWait(20);
        const Entity* ws = ctx.doc.findEntity(world);
        const std::string* key = ws ? ws->value("iw8r_drag_test") : nullptr;
        const bool oneStep = ctx.doc.undoPosition() == undoBefore + 1 && key && QString::fromStdString(*key).toDouble() == spin->value();
        const QString stepName = ctx.doc.undoPosition() > 0 ? QString::fromStdString(ctx.doc.history()[ctx.doc.undoPosition() - 1].name) : QString();
        check(le && started && followed == 20 && atZone == 100 && later > atZone + 10 && later2 > later && back > followed && stopped &&
                  !numeric_drag::dragging(),
              QString("a number field follows a sideways drag (40 px: %1), keeps scrolling past %2 px (%3 -> %4 -> %5) and stops back inside (%6)")
                  .arg(followed).arg(numeric_drag::kZone).arg(atZone).arg(later).arg(later2).arg(back));
        check(oneStep && stepName == "Drag Test amount",
              QString("one drag is one undo step named after the field (\"%1\", %2 step(s))").arg(stepName).arg(ctx.doc.undoPosition() - undoBefore));
        while (ctx.doc.undoPosition() > undoBefore) ctx.doc.undo();
        ctx.documentEdited();
        // A click that does not move gives the caret (typing), not a drag.
        send(QEvent::MouseButtonPress, 0, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, 0, Qt::NoButton);
        check(!numeric_drag::dragging() && ctx.doc.undoPosition() == undoBefore, "a click on a number field without moving is a plain click");
    }
'''),
])
print('ok')
